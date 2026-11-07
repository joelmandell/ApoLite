// ApoLite - registry helpers for the Windows setup tool.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "win_registry.h"

#include <windows.h>

#include <aclapi.h>

#include <cstddef>
#include <cwchar>

namespace apolite {
namespace setup {

namespace {

std::wstring g_lastError;

HKEY openKey(const std::wstring& subkey, REGSAM access) {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey.c_str(), 0, access | KEY_WOW64_64KEY, &hkey) !=
        ERROR_SUCCESS) {
        return nullptr;
    }
    return hkey;
}

void setError(const wchar_t* what) {
    g_lastError = std::wstring(what) + L" (error " + std::to_wstring(GetLastError()) + L")";
}

// Grants the built-in Administrators group full control of an existing key.
bool grantAdministratorsFullControl(HKEY hkey) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        setError(L"OpenProcessToken failed");
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, SE_TAKE_OWNERSHIP_NAME, &luid)) {
        setError(L"LookupPrivilegeValue failed");
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr)) {
        setError(L"AdjustTokenPrivileges failed");
        CloseHandle(token);
        return false;
    }

    PSID admins = nullptr;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    bool ok = false;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) {
        // Take ownership first, otherwise a later security-descriptor write is
        // itself denied.
        PSECURITY_DESCRIPTOR ownerSd = LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
        if (ownerSd != nullptr) {
            if (InitializeSecurityDescriptor(ownerSd, SECURITY_DESCRIPTOR_REVISION) &&
                SetSecurityDescriptorOwner(ownerSd, admins, FALSE)) {
                RegSetKeySecurity(hkey, OWNER_SECURITY_INFORMATION, ownerSd);
            }
            LocalFree(ownerSd);
        }

        // Grant Administrators full control on the DACL.
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = KEY_ALL_ACCESS;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
        access.Trustee.ptstrName = static_cast<LPWSTR>(admins);

        PACL dacl = nullptr;
        if (SetEntriesInAclW(1, &access, nullptr, &dacl) == ERROR_SUCCESS) {
            PSECURITY_DESCRIPTOR daclSd = LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
            if (daclSd != nullptr) {
                if (InitializeSecurityDescriptor(daclSd, SECURITY_DESCRIPTOR_REVISION) &&
                    SetSecurityDescriptorDacl(daclSd, TRUE, dacl, FALSE)) {
                    ok = RegSetKeySecurity(hkey, DACL_SECURITY_INFORMATION, daclSd) ==
                         ERROR_SUCCESS;
                }
                LocalFree(daclSd);
            }
            LocalFree(dacl);
        }
        FreeSid(admins);
    }

    privileges.Privileges[0].Attributes = 0;
    AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    CloseHandle(token);

    if (!ok) setError(L"could not grant access to the registry key");
    return ok;
}

}  // namespace

const std::wstring& lastError() { return g_lastError; }

bool keyExists(const std::wstring& subkey) {
    HKEY hkey = openKey(subkey, KEY_READ);
    if (hkey == nullptr) return false;
    RegCloseKey(hkey);
    return true;
}

std::vector<std::wstring> subkeys(const std::wstring& subkey) {
    std::vector<std::wstring> result;
    HKEY hkey = openKey(subkey, KEY_READ);
    if (hkey == nullptr) return result;

    wchar_t name[512];
    for (DWORD index = 0;; ++index) {
        DWORD length = static_cast<DWORD>(std::size(name));
        const LSTATUS status = RegEnumKeyExW(hkey, index, name, &length, nullptr, nullptr, nullptr,
                                             nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) break;
        result.emplace_back(name, length);
    }
    RegCloseKey(hkey);
    return result;
}

RegValue readValue(const std::wstring& subkey, const std::wstring& name) {
    RegValue result;
    HKEY hkey = openKey(subkey, KEY_READ);
    if (hkey == nullptr) return result;

    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(hkey, name.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return result;
    }

    std::vector<BYTE> buffer(size + sizeof(wchar_t) * 2, 0);
    if (RegQueryValueExW(hkey, name.c_str(), nullptr, &type, buffer.data(), &size) !=
        ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return result;
    }
    RegCloseKey(hkey);

    if (type == REG_DWORD && size >= sizeof(DWORD)) {
        result.kind = ValueKind::Dword;
        result.dwordValue = *reinterpret_cast<const DWORD*>(buffer.data());
    } else if (type == REG_SZ) {
        result.kind = ValueKind::String;
        result.stringValue = reinterpret_cast<const wchar_t*>(buffer.data());
    } else if (type == REG_MULTI_SZ) {
        result.kind = ValueKind::MultiString;
        const wchar_t* p = reinterpret_cast<const wchar_t*>(buffer.data());
        // The list ends at an empty string; a double NUL terminates the value.
        while (*p != L'\0') {
            result.multiValue.emplace_back(p);
            p += std::wcslen(p) + 1;
        }
    }
    return result;
}

bool createKeyWritable(const std::wstring& subkey) {
    HKEY hkey = nullptr;
    DWORD disposition = 0;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey.c_str(), 0, nullptr, 0, KEY_WRITE,
                                     nullptr, &hkey, &disposition);
    if (status == ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return true;
    }

    // An existing device key is owned by TrustedInstaller, so an elevated write
    // is refused. Take ownership, then retry.
    if (status != ERROR_ACCESS_DENIED) {
        setError(L"could not create the registry key");
        return false;
    }

    HKEY existing = openKey(subkey, READ_CONTROL | WRITE_DAC | WRITE_OWNER);
    if (existing == nullptr) {
        setError(L"could not open the registry key to change its access");
        return false;
    }
    const bool granted = grantAdministratorsFullControl(existing);
    RegCloseKey(existing);
    if (!granted) return false;

    status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                             &hkey, &disposition);
    if (status != ERROR_SUCCESS) {
        setError(L"could not create the registry key after taking ownership");
        return false;
    }
    RegCloseKey(hkey);
    return true;
}

bool writeString(const std::wstring& subkey, const std::wstring& name, const std::wstring& value) {
    HKEY hkey = openKey(subkey, KEY_SET_VALUE);
    if (hkey == nullptr) {
        setError(L"could not open the registry key for writing");
        return false;
    }
    const LSTATUS status =
        RegSetValueExW(hkey, name.c_str(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);
    if (status != ERROR_SUCCESS) {
        setError(L"could not write the registry value");
        return false;
    }
    return true;
}

bool writeMultiString(const std::wstring& subkey, const std::wstring& name,
                      const std::wstring& value) {
    HKEY hkey = openKey(subkey, KEY_SET_VALUE);
    if (hkey == nullptr) {
        setError(L"could not open the registry key for writing");
        return false;
    }
    // REG_MULTI_SZ: the string, then two terminating NULs.
    std::vector<wchar_t> data(value.begin(), value.end());
    data.push_back(L'\0');
    data.push_back(L'\0');
    const LSTATUS status =
        RegSetValueExW(hkey, name.c_str(), 0, REG_MULTI_SZ,
                       reinterpret_cast<const BYTE*>(data.data()),
                       static_cast<DWORD>(data.size() * sizeof(wchar_t)));
    RegCloseKey(hkey);
    if (status != ERROR_SUCCESS) {
        setError(L"could not write the registry value");
        return false;
    }
    return true;
}

bool writeDword(const std::wstring& subkey, const std::wstring& name, unsigned long value) {
    HKEY hkey = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &hkey, nullptr) != ERROR_SUCCESS) {
        setError(L"could not create the registry key");
        return false;
    }
    const DWORD data = static_cast<DWORD>(value);
    const LSTATUS status = RegSetValueExW(hkey, name.c_str(), 0, REG_DWORD,
                                          reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(hkey);
    if (status != ERROR_SUCCESS) {
        setError(L"could not write the registry value");
        return false;
    }
    return true;
}

bool deleteValue(const std::wstring& subkey, const std::wstring& name) {
    HKEY hkey = openKey(subkey, KEY_SET_VALUE);
    if (hkey == nullptr) return false;
    const LSTATUS status = RegDeleteValueW(hkey, name.c_str());
    RegCloseKey(hkey);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool deleteKey(const std::wstring& subkey) {
    const LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, subkey.c_str());
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, size, &size) &&
                          elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

}  // namespace setup
}  // namespace apolite
