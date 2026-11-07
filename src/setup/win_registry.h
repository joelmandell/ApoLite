// ApoLite - registry helpers for the Windows setup tool.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
//
// Device keys under MMDevices are owned by TrustedInstaller and deny writes even
// to an elevated administrator, so writing an endpoint's FxProperties key needs
// the ownership dance in createKeyWritable(). Everything here works on
// HKEY_LOCAL_MACHINE and the 64-bit view.
#pragma once

#include <string>
#include <vector>

namespace apolite {
namespace setup {

enum class ValueKind { Missing, String, MultiString, Dword };

struct RegValue {
    ValueKind kind = ValueKind::Missing;
    std::wstring stringValue;
    std::vector<std::wstring> multiValue;
    unsigned long dwordValue = 0;

    bool present() const { return kind != ValueKind::Missing; }
};

// Subkeys of an HKLM path; empty when the key does not exist.
std::vector<std::wstring> subkeys(const std::wstring& subkey);

// True when the key exists.
bool keyExists(const std::wstring& subkey);

// Reads a value; kind is Missing when the key or value is absent.
RegValue readValue(const std::wstring& subkey, const std::wstring& name);

bool writeString(const std::wstring& subkey, const std::wstring& name, const std::wstring& value);
bool writeMultiString(const std::wstring& subkey, const std::wstring& name,
                      const std::wstring& value);
bool writeDword(const std::wstring& subkey, const std::wstring& name, unsigned long value);
bool deleteValue(const std::wstring& subkey, const std::wstring& name);
bool deleteKey(const std::wstring& subkey);

// Creates the key. When the existing key denies write access, takes ownership
// and grants Administrators full control first. This is what makes writing an
// endpoint's FxProperties key possible.
bool createKeyWritable(const std::wstring& subkey);

// True when the current process holds an elevated token.
bool isElevated();

// Text for the most recent failure.
const std::wstring& lastError();

}  // namespace setup
}  // namespace apolite
