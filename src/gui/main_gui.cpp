// ApoLite - the editor window.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
//
// One window with the handful of controls a voice boost actually needs: the
// bands that matter, a response curve, and a plain-language summary of what the
// current settings will do. No text editing required.
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "apolite/settings.h"
#include "curve.h"

namespace {

using apolite::Target;
using apolite::VoiceSettings;

constexpr wchar_t kWindowClass[] = L"ApoLiteEditorWindow";
constexpr wchar_t kWindowTitle[] = L"ApoLite";

// Control identifiers.
enum : int {
    kIdPreamp = 100,
    kIdHighPass,
    kIdHighPassHz,
    kIdDeMud,
    kIdDeMudGain,
    kIdPresence,
    kIdPresenceGain,
    kIdAir,
    kIdAirGain,
    kIdDeEss,
    kIdDeEssGain,
    kIdLowPass,
    kIdLowPassHz,
    kIdCompressor,
    kIdThreshold,
    kIdTarget,
    kIdSave,
    kIdReset,
    kIdStatus,
    // Value boxes carry an id offset from their checkbox so WM_COMMAND can tell
    // the two apart.
    kValueOffset = 1000,
};

constexpr int kMargin = 14;
constexpr int kRowHeight = 27;
constexpr int kCurveHeight = 200;
constexpr int kSummaryHeight = 78;

double readDouble(HWND edit, double fallback) {
    wchar_t buffer[64] = {};
    GetWindowTextW(edit, buffer, 64);
    wchar_t* end = nullptr;
    const double value = wcstod(buffer, &end);
    return (end == buffer) ? fallback : value;
}

void writeDouble(HWND edit, double value, int decimals) {
    wchar_t buffer[64] = {};
    swprintf(buffer, 64, L"%.*f", decimals, value);
    SetWindowTextW(edit, buffer);
}

// Read a whole file, or an empty string. A configuration is never more than a
// few kilobytes.
std::string readFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size = {};
    std::string text;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20)) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) {
            text.clear();
        } else {
            text.resize(read);
        }
    }
    CloseHandle(file);
    return text;
}

// One editable band: an optional checkbox, and a value box with a spinner.
struct Row {
    int id = 0;
    HWND label = nullptr;
    HWND check = nullptr;
    HWND edit = nullptr;
    HWND spin = nullptr;
};

class EditorWindow {
public:
    bool create(HINSTANCE instance);
    int runMessageLoop();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    HWND addCheck(int id, const wchar_t* label);
    void addRow(int id, const wchar_t* label, bool withCheck, double min, double max);
    void buildControls();
    void layout(int width, int height);

    void loadFromDisk();
    void collectFromControls();
    void pushToControls();
    void apply();
    void resetToDefaults();
    void setStatus(const std::wstring& text, bool isError = false);
    void updateRowEnabling();

    void paintCurve(HDC dc);
    void paintSummary(HDC dc);
    void onPaint();

    int scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH background_ = nullptr;
    UINT dpi_ = 96;

    std::vector<Row> rows_;
    HWND targetLabel_ = nullptr;
    HWND targetCombo_ = nullptr;
    HWND status_ = nullptr;
    HWND saveButton_ = nullptr;
    HWND resetButton_ = nullptr;

    RECT curveRect_ = {};
    RECT summaryRect_ = {};

    VoiceSettings settings_;
    std::wstring configPath_;
    bool statusIsError_ = false;

    // Lines the loaded file contained that this editor cannot express. They are
    // written back on Apply, so a hand-written file is not silently gutted.
    std::vector<std::string> preservedLines_;
};

// ---------------------------------------------------------------- controls

HWND EditorWindow::addCheck(int id, const wchar_t* label) {
    HWND box = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0,
                               10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               nullptr, nullptr);
    SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return box;
}

void EditorWindow::addRow(int id, const wchar_t* label, bool withCheck, double min, double max) {
    Row row;
    row.id = id;
    if (withCheck) {
        row.check = addCheck(id, label);
    } else {
        row.label = CreateWindowExW(0, L"STATIC", label, WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10,
                                    10, hwnd_, nullptr, nullptr, nullptr);
        SendMessageW(row.label, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    row.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_AUTOHSCROLL, 0, 0, 10, 10,
                               hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id + kValueOffset)),
                               nullptr, nullptr);
    SendMessageW(row.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendMessageW(row.edit, EM_SETLIMITTEXT, 12, 0);

    row.spin = CreateWindowExW(0, UPDOWN_CLASSW, L"",
                               WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_SETBUDDYINT |
                                   UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                               0, 0, 10, 10, hwnd_, nullptr, nullptr, nullptr);
    SendMessageW(row.spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(row.edit), 0);
    // Spinners are integral, so the range is scaled by ten for one decimal place.
    SendMessageW(row.spin, UDM_SETRANGE32, static_cast<WPARAM>(std::lround(min * 10.0)),
                 static_cast<LPARAM>(std::lround(max * 10.0)));
    SendMessageW(row.spin, UDM_SETPOS32, 0, 0);

    rows_.push_back(row);
}

void EditorWindow::buildControls() {
    // The bands, in signal-path order.
    addRow(kIdPreamp, L"Output trim", false, -24.0, 12.0);
    addRow(kIdHighPass, L"Remove rumble", true, 20.0, 400.0);
    addRow(kIdDeMud, L"Reduce muddiness", true, -12.0, 6.0);
    addRow(kIdPresence, L"Voice presence", true, -12.0, 12.0);
    addRow(kIdAir, L"Clarity", true, -12.0, 12.0);
    addRow(kIdDeEss, L"Tame harshness", true, -12.0, 6.0);
    addRow(kIdLowPass, L"Remove hiss", true, 8000.0, 22000.0);
    addRow(kIdCompressor, L"Even out loud and quiet", true, -60.0, 0.0);

    targetLabel_ = CreateWindowExW(0, L"STATIC", L"Applies to", WS_CHILD | WS_VISIBLE | SS_LEFT, 0,
                                   0, 10, 10, hwnd_, nullptr, nullptr, nullptr);
    SendMessageW(targetLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

    targetCombo_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10,
                                   10, hwnd_, reinterpret_cast<HMENU>(kIdTarget), nullptr, nullptr);
    SendMessageW(targetCombo_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendMessageW(targetCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Microphone (what you send to Zoom)"));
    SendMessageW(targetCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Playback (what you hear)"));

    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
                              hwnd_, reinterpret_cast<HMENU>(kIdStatus), nullptr, nullptr);
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

    saveButton_ = CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                  0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(kIdSave), nullptr,
                                  nullptr);
    SendMessageW(saveButton_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    resetButton_ = CreateWindowExW(0, L"BUTTON", L"Reset to voice preset", WS_CHILD | WS_VISIBLE, 0,
                                   0, 10, 10, hwnd_, reinterpret_cast<HMENU>(kIdReset), nullptr,
                                   nullptr);
    SendMessageW(resetButton_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

    // Every control has a known position, so the window is sized to fit and then
    // left fixed.
    RECT desired{0, 0, scale(700), scale(560)};
    AdjustWindowRectEx(&desired, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, FALSE, 0);
    SetWindowPos(hwnd_, nullptr, 0, 0, desired.right - desired.left, desired.bottom - desired.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

// ------------------------------------------------------------------ layout

void EditorWindow::layout(int width, int height) {
    (void)height;
    const int m = scale(kMargin);
    const int rowH = scale(kRowHeight);
    const int labelW = scale(190);
    const int editW = scale(80);
    const int controlH = scale(21);

    int y = m;
    for (Row& row : rows_) {
        HWND label = row.check ? row.check : row.label;
        if (label != nullptr) MoveWindow(label, m, y + scale(3), labelW, scale(20), TRUE);
        MoveWindow(row.edit, m + labelW + scale(8), y, editW, controlH, TRUE);
        MoveWindow(row.spin, m + labelW + scale(8) + editW, y, scale(18), controlH, TRUE);
        y += rowH;
    }

    y += scale(6);
    MoveWindow(targetLabel_, m, y + scale(3), labelW, scale(20), TRUE);
    MoveWindow(targetCombo_, m + labelW + scale(8), y, width - 2 * m - labelW - scale(8),
               scale(200), TRUE);
    y += rowH + scale(10);

    const int inner = width - 2 * m;
    curveRect_ = RECT{m, y, m + inner, y + scale(kCurveHeight)};
    y += scale(kCurveHeight) + scale(8);

    summaryRect_ = RECT{m, y, m + inner, y + scale(kSummaryHeight)};
    y += scale(kSummaryHeight) + scale(10);

    const int buttonH = scale(28);
    MoveWindow(saveButton_, m, y, scale(110), buttonH, TRUE);
    MoveWindow(resetButton_, m + scale(118), y, scale(170), buttonH, TRUE);
    y += buttonH + scale(10);

    MoveWindow(status_, m, y, inner, scale(40), TRUE);
}

// ----------------------------------------------------------- load and store

void EditorWindow::pushToControls() {
    const VoiceSettings& s = settings_;
    writeDouble(GetDlgItem(hwnd_, kIdPreamp + kValueOffset), s.preampDb, 1);

    auto check = [&](int id, bool on) {
        SendDlgItemMessageW(hwnd_, id, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    check(kIdHighPass, s.highPassEnabled);
    check(kIdDeMud, s.deMudEnabled);
    check(kIdPresence, s.presenceEnabled);
    check(kIdAir, s.airEnabled);
    check(kIdDeEss, s.deEssEnabled);
    check(kIdLowPass, s.lowPassEnabled);
    check(kIdCompressor, s.compressorEnabled);

    writeDouble(GetDlgItem(hwnd_, kIdHighPass + kValueOffset), s.highPassHz, 0);
    writeDouble(GetDlgItem(hwnd_, kIdDeMud + kValueOffset), s.deMudGainDb, 1);
    writeDouble(GetDlgItem(hwnd_, kIdPresence + kValueOffset), s.presenceGainDb, 1);
    writeDouble(GetDlgItem(hwnd_, kIdAir + kValueOffset), s.airGainDb, 1);
    writeDouble(GetDlgItem(hwnd_, kIdDeEss + kValueOffset), s.deEssGainDb, 1);
    writeDouble(GetDlgItem(hwnd_, kIdLowPass + kValueOffset), s.lowPassHz, 0);
    writeDouble(GetDlgItem(hwnd_, kIdCompressor + kValueOffset), s.thresholdDb, 1);

    SendDlgItemMessageW(hwnd_, kIdTarget, CB_SETCURSEL, s.target == Target::Microphone ? 0 : 1, 0);
    updateRowEnabling();
}

void EditorWindow::collectFromControls() {
    VoiceSettings& s = settings_;
    s.target = (SendDlgItemMessageW(hwnd_, kIdTarget, CB_GETCURSEL, 0, 0) == 1) ? Target::Playback
                                                                                : Target::Microphone;
    s.preampDb = readDouble(GetDlgItem(hwnd_, kIdPreamp + kValueOffset), s.preampDb);

    auto checked = [&](int id) {
        return SendDlgItemMessageW(hwnd_, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
    };
    s.highPassEnabled = checked(kIdHighPass);
    s.deMudEnabled = checked(kIdDeMud);
    s.presenceEnabled = checked(kIdPresence);
    s.airEnabled = checked(kIdAir);
    s.deEssEnabled = checked(kIdDeEss);
    s.lowPassEnabled = checked(kIdLowPass);
    s.compressorEnabled = checked(kIdCompressor);

    s.highPassHz = readDouble(GetDlgItem(hwnd_, kIdHighPass + kValueOffset), s.highPassHz);
    s.deMudGainDb = readDouble(GetDlgItem(hwnd_, kIdDeMud + kValueOffset), s.deMudGainDb);
    s.presenceGainDb = readDouble(GetDlgItem(hwnd_, kIdPresence + kValueOffset), s.presenceGainDb);
    s.airGainDb = readDouble(GetDlgItem(hwnd_, kIdAir + kValueOffset), s.airGainDb);
    s.deEssGainDb = readDouble(GetDlgItem(hwnd_, kIdDeEss + kValueOffset), s.deEssGainDb);
    s.lowPassHz = readDouble(GetDlgItem(hwnd_, kIdLowPass + kValueOffset), s.lowPassHz);
    s.thresholdDb = readDouble(GetDlgItem(hwnd_, kIdCompressor + kValueOffset), s.thresholdDb);
}

void EditorWindow::updateRowEnabling() {
    for (const Row& row : rows_) {
        if (row.check == nullptr) continue;  // the trim has no checkbox
        const bool on =
            SendDlgItemMessageW(hwnd_, row.id, BM_GETCHECK, 0, 0) == BST_CHECKED;
        EnableWindow(row.edit, on);
        EnableWindow(row.spin, on);
    }
}

void EditorWindow::loadFromDisk() {
    const std::string text = readFile(configPath_);
    if (text.empty()) {
        settings_ = apolite::defaultSettings();
        pushToControls();
        setStatus(L"No configuration yet, so these are the voice preset defaults. "
                  L"Press Apply to write one.");
        return;
    }

    const apolite::ParseResult parsed = apolite::parseConfigText(text);
    if (!parsed.ok) {
        setStatus(L"The existing file was not recognised, so the defaults are shown instead. "
                  L"Apply would replace it.",
                  true);
        return;
    }

    settings_ = parsed.settings;
    preservedLines_ = parsed.unsupported;
    pushToControls();

    if (preservedLines_.empty()) {
        setStatus(L"Loaded " + configPath_);
    } else {
        setStatus(L"Loaded, and kept " + std::to_wstring(preservedLines_.size()) +
                  L" line(s) this editor does not understand. Apply writes them back unchanged.");
    }
}

void EditorWindow::setStatus(const std::wstring& text, bool isError) {
    statusIsError_ = isError;
    SetWindowTextW(status_, text.c_str());
    InvalidateRect(status_, nullptr, TRUE);
}

void EditorWindow::apply() {
    collectFromControls();

    // Re-read the file rather than trusting what was loaded when the window
    // opened: it may have been edited by hand since, and those lines should not
    // be lost.
    std::vector<std::string> keep = preservedLines_;
    const std::string onDisk = readFile(configPath_);
    if (!onDisk.empty()) {
        const apolite::ParseResult current = apolite::parseConfigText(onDisk);
        if (current.ok) keep = current.unsupported;
    }

    std::string out = apolite::toConfigText(settings_);
    if (!keep.empty()) {
        out += "\n# Kept from the previous file. This editor does not edit these lines.\n";
        for (const std::string& line : keep) out += line + "\n";
    }
    preservedLines_ = keep;

    const size_t slash = configPath_.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
        SHCreateDirectoryExW(nullptr, configPath_.substr(0, slash).c_str(), nullptr);
    }

    // Write a temporary file and rename it over the target. The APO watches the
    // directory and reloads on change, and this way it never sees a partial file.
    const std::wstring temp = configPath_ + L".new";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        setStatus(L"Could not write to " + configPath_ +
                      L". If ApoLite is installed for all users, run the editor as administrator.",
                  true);
        return;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    CloseHandle(file);
    if (!ok || written != out.size()) {
        DeleteFileW(temp.c_str());
        setStatus(L"Could not write the configuration file.", true);
        return;
    }
    if (!MoveFileExW(temp.c_str(), configPath_.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        setStatus(L"Could not replace the configuration file. Is it open in another program?", true);
        return;
    }

    InvalidateRect(hwnd_, nullptr, TRUE);
    setStatus(L"Applied to " + configPath_ + L". Audio updates within about a second.");
}

void EditorWindow::resetToDefaults() {
    settings_ = apolite::defaultSettings();
    pushToControls();
    InvalidateRect(hwnd_, nullptr, TRUE);
    setStatus(L"Reset to the voice preset. Press Apply to write it.");
}

// ------------------------------------------------------------------- paint

void EditorWindow::paintCurve(HDC dc) {
    apolite::gui::paintResponseCurve(dc, curveRect_, settings_, 48000.0);
}

void EditorWindow::paintSummary(HDC dc) {
    const VoiceSettings& s = settings_;

    std::wstring lines;
    auto add = [&](const std::wstring& line) {
        if (!lines.empty()) lines += L"\r\n";
        lines += line;
    };

    if (s.compressorEnabled) {
        wchar_t buffer[192] = {};
        swprintf(buffer, 192,
                 L"Even out loud and quiet: anything above %.0f dB is held back, and quiet speech "
                 L"is lifted by about %.0f dB. Nothing above %.0f dB gets through.",
                 s.thresholdDb, s.makeupDb, s.limitDb);
        add(buffer);
    } else {
        add(L"Even out loud and quiet: off. Levels will vary more, and loud speech may clip.");
    }

    if (s.presenceEnabled && s.presenceGainDb != 0.0) {
        wchar_t buffer[192] = {};
        swprintf(buffer, 192,
                 L"Voice presence: %+.1f dB around 3 kHz, which is what makes a voice cut "
                 L"through a call.",
                 s.presenceGainDb);
        add(buffer);
    }

    if (s.highPassEnabled) {
        wchar_t buffer[192] = {};
        swprintf(buffer, 192, L"Remove rumble: everything below %.0f Hz is cut.", s.highPassHz);
        add(buffer);
    }

    RECT text = summaryRect_;
    text.left += scale(2);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(172, 178, 188));
    DrawTextW(dc, lines.c_str(), -1, &text, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

void EditorWindow::onPaint() {
    PAINTSTRUCT ps = {};
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT client = {};
    GetClientRect(hwnd_, &client);
    FillRect(dc, &client, background_);
    paintCurve(dc);
    paintSummary(dc);
    EndPaint(hwnd_, &ps);
}

// --------------------------------------------------------------- messages

LRESULT EditorWindow::handleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            dpi_ = GetDpiForWindow(hwnd_);
            if (dpi_ == 0) dpi_ = 96;
            configPath_ = apolite::defaultConfigPath();
            buildControls();
            RECT client = {};
            GetClientRect(hwnd_, &client);
            layout(client.right, client.bottom);
            loadFromDisk();
            return 0;
        }

        case WM_PAINT:
            onPaint();
            return 0;

        case WM_ERASEBKGND:
            return 1;  // painted in WM_PAINT

        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);
            if (code == BN_CLICKED) {
                if (id == kIdSave) {
                    apply();
                    return 0;
                }
                if (id == kIdReset) {
                    resetToDefaults();
                    return 0;
                }
                // A band checkbox toggles its own value box and the summary.
                for (const Row& row : rows_) {
                    if (row.check != nullptr && row.id == id) {
                        updateRowEnabling();
                        collectFromControls();
                        InvalidateRect(hwnd_, nullptr, TRUE);
                        return 0;
                    }
                }
                return 0;
            }
            if (code == EN_CHANGE || code == CBN_SELCHANGE) {
                collectFromControls();
                InvalidateRect(hwnd_, nullptr, TRUE);
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, RGB(24, 26, 30));
            SetTextColor(dc, reinterpret_cast<HWND>(lparam) == status_
                                 ? (statusIsError_ ? RGB(235, 122, 122) : RGB(146, 200, 158))
                                 : RGB(216, 220, 226));
            return reinterpret_cast<LRESULT>(background_);
        }

        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, RGB(38, 41, 47));
            SetTextColor(dc, RGB(228, 231, 236));
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }

        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, RGB(24, 26, 30));
            return reinterpret_cast<LRESULT>(background_);
        }

        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

bool EditorWindow::create(HINSTANCE instance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &EditorWindow::windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) return false;

    background_ = CreateSolidBrush(RGB(24, 26, 30));
    font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    hwnd_ = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, 700, 560, nullptr, nullptr, instance,
                            this);
    if (hwnd_ == nullptr) return false;

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

int EditorWindow::runMessageLoop() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK EditorWindow::windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    EditorWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<EditorWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
    return self->handleMessage(message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // One instance is enough; a second launch just raises the first window.
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\ApoLiteEditor");
    if (single != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kWindowClass, nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    EditorWindow window;
    if (!window.create(instance)) return 1;
    return window.runMessageLoop();
}
