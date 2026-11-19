#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <cstdint>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
typedef struct _PROCESS_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} PROCESS_POWER_THROTTLING_STATE, * PPROCESS_POWER_THROTTLING_STATE;
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#endif

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

namespace {

    constexpr UINT WM_TRAYICON = WM_USER + 1;
    constexpr UINT ID_HOTKEY_ACTIVE = 1001;
    constexpr UINT_PTR ID_TIMER_TRIM = 1;

    constexpr UINT ID_TRAY_TOGGLE_ACTIVE = 9000;
    constexpr UINT ID_TRAY_EXIT = 9001;
    constexpr UINT ID_TRAY_RESTORE_ALL = 9002;
    constexpr UINT ID_TRAY_AUTOSTART = 9003;
    constexpr UINT ID_TRAY_TASKBAR_TOGGLE = 9004;
    constexpr UINT ID_TRAY_MINIMIZE_TOGGLE = 9005;
    constexpr UINT ID_LANG_EN = 9010;
    constexpr UINT ID_LANG_RU = 9011;
    constexpr UINT ID_HOTKEY_LETTER_BASE = 9100; // 9100..9125 ('A'..'Z')
    constexpr UINT ID_WINDOW_MENU_BASE = 10000;

    const wchar_t* const REG_RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* const REG_SETTINGS_KEY = L"Software\\HideDem";
    const wchar_t* const APP_REG_NAME = L"HideDem";
    const wchar_t* const MUTEX_NAME = L"Local\\HideDem_SingleInstance_Mutex";

    enum class Language : DWORD {
        English = 0,
        Russian = 1
    };

    enum class StrId {
        MenuTogglePrefix,
        MenuSelectApp,
        MenuRestoreAll,
        MenuTaskbarToggle,
        MenuMinimizeToggle,
        MenuHotkeyPrefix,
        MenuAutostart,
        MenuLanguage,
        MenuExit,
        NoWindowsFound,
        ProtectedCountPrefix
    };

    struct WindowState {
        HWND hWnd = nullptr;
        bool taskbarHidden = false;
        bool minimizedByUs = false;
    };

    struct WindowEntry {
        HWND hWnd = nullptr;
        std::wstring title;
        std::wstring exeName;
    };

    struct UniqueHandle {
        HANDLE handle = nullptr;
        UniqueHandle() = default;
        explicit UniqueHandle(HANDLE h) : handle(h) {}
        ~UniqueHandle() { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;
        explicit operator bool() const { return handle && handle != INVALID_HANDLE_VALUE; }
    };

    struct UniqueRegKey {
        HKEY key = nullptr;
        ~UniqueRegKey() { if (key) RegCloseKey(key); }
        HKEY* put() { return &key; }
        operator HKEY() const { return key; }
    };

    struct AppState {
        HWND hMsgWnd = nullptr;
        NOTIFYICONDATAW nid{ sizeof(NOTIFYICONDATAW) };
        HICON hAppIcon = nullptr;
        ITaskbarList* pTaskbarList = nullptr;
        UINT wmTaskbarCreated = 0;

        Language lang = Language::English;
        bool hideFromTaskbar = true;
        bool minimizeOnHide = false;
        wchar_t hotkeyChar = L'Q'; // Ctrl+Shift+Q by default

        HWND lastToggledHwnd = nullptr;
        std::unordered_map<HWND, WindowState> protectedWindows;
    } g_app;

    // --- Localization ---
    const wchar_t* GetStr(StrId id) {
        if (g_app.lang == Language::Russian) {
            switch (id) {
            case StrId::MenuTogglePrefix:     return L"\u0421\u043A\u0440\u044B\u0442\u044C / \u0432\u0435\u0440\u043D\u0443\u0442\u044C \u043E\u043A\u043D\u043E\tCtrl+Shift+";
            case StrId::MenuSelectApp:        return L"\u0412\u044B\u0431\u0440\u0430\u0442\u044C \u043E\u043A\u043D\u043E \u0434\u043B\u044F \u0441\u043A\u0440\u044B\u0442\u0438\u044F";
            case StrId::MenuRestoreAll:       return L"\u0412\u043E\u0441\u0441\u0442\u0430\u043D\u043E\u0432\u0438\u0442\u044C \u0432\u0441\u0435 \u043E\u043A\u043D\u0430";
            case StrId::MenuTaskbarToggle:    return L"\u0421\u043A\u0440\u044B\u0432\u0430\u0442\u044C \u0438\u0437 \u043F\u0430\u043D\u0435\u043B\u0438 \u0437\u0430\u0434\u0430\u0447 \u0438 Alt+Tab";
            case StrId::MenuMinimizeToggle:   return L"\u0421\u0432\u043E\u0440\u0430\u0447\u0438\u0432\u0430\u0442\u044C \u043E\u043A\u043D\u043E \u043F\u0440\u0438 \u0441\u043A\u0440\u044B\u0442\u0438\u0438";
            case StrId::MenuHotkeyPrefix:     return L"\u0413\u043E\u0440\u044F\u0447\u0430\u044F \u043A\u043B\u0430\u0432\u0438\u0448\u0430: Ctrl+Shift+";
            case StrId::MenuAutostart:        return L"\u0417\u0430\u043F\u0443\u0441\u043A\u0430\u0442\u044C \u0432\u043C\u0435\u0441\u0442\u0435 \u0441 Windows";
            case StrId::MenuLanguage:         return L"\u042F\u0437\u044B\u043A / Language";
            case StrId::MenuExit:             return L"\u0412\u044B\u0445\u043E\u0434";
            case StrId::NoWindowsFound:       return L"(\u041D\u0435\u0442 \u0434\u043E\u0441\u0442\u0443\u043F\u043D\u044B\u0445 \u043E\u043A\u043E\u043D)";
            case StrId::ProtectedCountPrefix: return L"HideDem  \u2022  \u0421\u043A\u0440\u044B\u0442\u043E: ";
            }
        }
        else {
            switch (id) {
            case StrId::MenuTogglePrefix:     return L"Toggle Active Window\tCtrl+Shift+";
            case StrId::MenuSelectApp:        return L"Select Window to Hide";
            case StrId::MenuRestoreAll:       return L"Restore All Windows";
            case StrId::MenuTaskbarToggle:    return L"Hide from Taskbar and Alt+Tab";
            case StrId::MenuMinimizeToggle:   return L"Minimize Window on Hide";
            case StrId::MenuHotkeyPrefix:     return L"Hotkey: Ctrl+Shift+";
            case StrId::MenuAutostart:        return L"Run on Windows Startup";
            case StrId::MenuLanguage:         return L"Language";
            case StrId::MenuExit:             return L"Exit";
            case StrId::NoWindowsFound:       return L"(No eligible windows)";
            case StrId::ProtectedCountPrefix: return L"HideDem  \u2022  Protected: ";
            }
        }
        return L"";
    }

    // --- Memory & Power Optimization ---
    void TrimMemory() {
        CoFreeUnusedLibraries();
        HeapCompact(GetProcessHeap(), 0);
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    }

    void ScheduleMemoryTrim() {
        if (g_app.hMsgWnd) {
            SetTimer(g_app.hMsgWnd, ID_TIMER_TRIM, 1500, nullptr);
        }
    }

    void OptimizeProcessForIdle() {
        PROCESS_POWER_THROTTLING_STATE throttling{};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        throttling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));

#ifdef MEMORY_PRIORITY_VERY_LOW
        MEMORY_PRIORITY_INFORMATION memPrio{};
        memPrio.MemoryPriority = MEMORY_PRIORITY_VERY_LOW;
        SetProcessInformation(GetCurrentProcess(), ProcessMemoryPriority, &memPrio, sizeof(memPrio));
#endif

        SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
        TrimMemory();
    }

    // --- Theme & Settings ---
    bool IsDarkModeActive() {
        UniqueRegKey hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, hKey.put()) == ERROR_SUCCESS) {
            DWORD val = 1, size = sizeof(val);
            if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
                return (val == 0);
            }
        }
        return false;
    }

    void ApplySystemTheme() {
        HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
        if (!hUxTheme) hUxTheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!hUxTheme) return;

        auto pSetPreferredAppMode = reinterpret_cast<int(WINAPI*)(int)>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
        auto pFlushMenuThemes = reinterpret_cast<void(WINAPI*)()>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136)));
        if (pSetPreferredAppMode) pSetPreferredAppMode(IsDarkModeActive() ? 2 : 3);
        if (pFlushMenuThemes) pFlushMenuThemes();
    }

    void LoadSettings() {
        UniqueRegKey hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_SETTINGS_KEY, 0, KEY_READ, hKey.put()) == ERROR_SUCCESS) {
            auto readDword = [&](const wchar_t* name, DWORD def) -> DWORD {
                DWORD val = def, size = sizeof(val);
                if (RegQueryValueExW(hKey, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS)
                    return val;
                return def;
                };
            g_app.lang = (readDword(L"Language", 0) == 1) ? Language::Russian : Language::English;
            g_app.hideFromTaskbar = (readDword(L"HideTaskbar", 1) != 0);
            g_app.minimizeOnHide = (readDword(L"MinimizeOnHide", 0) != 0);

            DWORD keyVal = readDword(L"HotkeyChar", L'Q');
            if (keyVal >= L'A' && keyVal <= L'Z') {
                g_app.hotkeyChar = static_cast<wchar_t>(keyVal);
            }
        }
    }

    void SaveSettings() {
        UniqueRegKey hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_SETTINGS_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, hKey.put(), nullptr) == ERROR_SUCCESS) {
            DWORD langVal = static_cast<DWORD>(g_app.lang);
            DWORD taskbarVal = g_app.hideFromTaskbar ? 1 : 0;
            DWORD minVal = g_app.minimizeOnHide ? 1 : 0;
            DWORD keyVal = static_cast<DWORD>(g_app.hotkeyChar);
            RegSetValueExW(hKey, L"Language", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&langVal), sizeof(langVal));
            RegSetValueExW(hKey, L"HideTaskbar", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&taskbarVal), sizeof(taskbarVal));
            RegSetValueExW(hKey, L"MinimizeOnHide", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&minVal), sizeof(minVal));
            RegSetValueExW(hKey, L"HotkeyChar", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&keyVal), sizeof(keyVal));
        }
    }

    bool IsAutostartEnabled() {
        UniqueRegKey hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_READ, hKey.put()) == ERROR_SUCCESS) {
            return RegQueryValueExW(hKey, APP_REG_NAME, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        }
        return false;
    }

    void ToggleAutostart(bool enable) {
        UniqueRegKey hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_SET_VALUE, hKey.put()) == ERROR_SUCCESS) {
            if (enable) {
                wchar_t path[MAX_PATH];
                DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
                if (len > 0 && len < MAX_PATH) {
                    std::wstring cmd = L"\"" + std::wstring(path, len) + L"\"";
                    RegSetValueExW(hKey, APP_REG_NAME, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(cmd.c_str()),
                        static_cast<DWORD>((cmd.length() + 1) * sizeof(wchar_t)));
                }
            }
            else {
                RegDeleteValueW(hKey, APP_REG_NAME);
            }
        }
    }

    // --- Low-Level Affinity & Taskbar ---
    bool SetProcessDisplayAffinityRemote(HWND hWnd, DWORD affinity) {
#if defined(_WIN64)
        DWORD pid = 0;
        GetWindowThreadProcessId(hWnd, &pid);
        if (!pid) return false;

        UniqueHandle hProcess(OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, pid));
        if (!hProcess) return false;

        BOOL isWow64 = FALSE;
        if (IsWow64Process(hProcess.handle, &isWow64) && isWow64) {
            return false;
        }

        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        FARPROC pfnAffinity = hUser32 ? GetProcAddress(hUser32, "SetWindowDisplayAffinity") : nullptr;
        if (!pfnAffinity) return false;

#pragma pack(push, 1)
        struct RemoteStub {
            uint8_t sub_rsp[4] = { 0x48, 0x83, 0xEC, 0x28 };
            uint8_t mov_rcx[2] = { 0x48, 0xB9 };
            uint64_t hwndVal = 0;
            uint8_t mov_edx = 0xBA;
            uint32_t affinityVal = 0;
            uint8_t mov_rax[2] = { 0x48, 0xB8 };
            uint64_t funcVal = 0;
            uint8_t call_rax[2] = { 0xFF, 0xD0 };
            uint8_t add_rsp[4] = { 0x48, 0x83, 0xC4, 0x28 };
            uint8_t ret = 0xC3;
        } stub;
#pragma pack(pop)

        stub.hwndVal = reinterpret_cast<uint64_t>(hWnd);
        stub.affinityVal = affinity;
        stub.funcVal = reinterpret_cast<uint64_t>(pfnAffinity);

        LPVOID pRemoteMem = VirtualAllocEx(hProcess.handle, nullptr, sizeof(stub), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pRemoteMem) return false;

        SIZE_T written = 0;
        DWORD oldProt = 0;
        if (!WriteProcessMemory(hProcess.handle, pRemoteMem, &stub, sizeof(stub), &written) ||
            written != sizeof(stub) ||
            !VirtualProtectEx(hProcess.handle, pRemoteMem, sizeof(stub), PAGE_EXECUTE_READ, &oldProt)) {
            VirtualFreeEx(hProcess.handle, pRemoteMem, 0, MEM_RELEASE);
            return false;
        }

        UniqueHandle hThread(CreateRemoteThread(hProcess.handle, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(pRemoteMem), nullptr, 0, nullptr));
        if (!hThread) {
            VirtualFreeEx(hProcess.handle, pRemoteMem, 0, MEM_RELEASE);
            return false;
        }

        DWORD waitRes = WaitForSingleObject(hThread.handle, 2000);
        DWORD exitCode = 0;
        if (waitRes == WAIT_OBJECT_0) {
            GetExitCodeThread(hThread.handle, &exitCode);
            VirtualFreeEx(hProcess.handle, pRemoteMem, 0, MEM_RELEASE);
        }
        return (waitRes == WAIT_OBJECT_0 && exitCode != 0);
#else
        return false;
#endif
    }

    void ModifyTaskbarState(HWND hWnd, bool hide) {
        LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
        if (hide) {
            exStyle = (exStyle & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW;
            SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);
            if (g_app.pTaskbarList) g_app.pTaskbarList->DeleteTab(hWnd);
        }
        else {
            exStyle = (exStyle & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW;
            SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);
            if (g_app.pTaskbarList) g_app.pTaskbarList->AddTab(hWnd);
        }

        SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }

    HICON GenerateProceduralIcon() {
        const int size = std::max(32, GetSystemMetrics(SM_CXICON));
        BITMAPV5HEADER bi{};
        bi.bV5Size = sizeof(BITMAPV5HEADER);
        bi.bV5Width = size;
        bi.bV5Height = -size;
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;

        HDC hdcScreen = GetDC(nullptr);
        void* pBits = nullptr;
        HBITMAP hbmColor = CreateDIBSection(hdcScreen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &pBits, nullptr, 0);
        ReleaseDC(nullptr, hdcScreen);
        if (!hbmColor || !pBits) return nullptr;

        uint32_t* pixels = reinterpret_cast<uint32_t*>(pBits);
        constexpr int ss = 4;
        const float invSize = 32.0f / static_cast<float>(size);

        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float accR = 0, accG = 0, accB = 0, accA = 0;

                for (int sy = 0; sy < ss; ++sy) {
                    for (int sx = 0; sx < ss; ++sx) {
                        float px = (x + (sx + 0.5f) / ss) * invSize;
                        float py = (y + (sy + 0.5f) / ss) * invSize;
                        float dx = px - 16.0f;
                        float dy = py - 16.0f;

                        float distNorm = std::sqrt(std::pow(std::abs(dx) / 12.6f, 2.6f) + std::pow(std::abs(dy + 0.6f) / 13.4f, 2.6f));
                        if (distNorm <= 1.0f) {
                            float t = py / 32.0f;
                            float rF = 18.0f + t * 14.0f;
                            float gF = 24.0f + t * 28.0f;
                            float bF = 48.0f + t * 72.0f;

                            if (distNorm > 0.80f) {
                                float rimT = (distNorm - 0.80f) / 0.20f;
                                rF = (1.0f - rimT) * rF;
                                gF = (1.0f - rimT) * gF + rimT * 215.0f;
                                bF = (1.0f - rimT) * bF + rimT * 255.0f;
                            }

                            float eyeDist = (dx * dx) / 42.0f + (dy * dy) / 13.0f;
                            if (eyeDist <= 1.0f) {
                                rF = 235.0f; gF = 245.0f; bF = 255.0f;
                                if ((dx * dx + dy * dy) <= 4.8f) {
                                    rF = 16.0f; gF = 24.0f; bF = 46.0f;
                                }
                            }

                            if (std::abs(dx - dy) < 1.35f && distNorm < 0.88f) {
                                rF = 255.0f; gF = 60.0f; bF = 100.0f;
                            }

                            accR += rF; accG += gF; accB += bF; accA += 1.0f;
                        }
                    }
                }

                constexpr float invSamples = 1.0f / (ss * ss);
                float alpha = accA * invSamples;
                if (alpha > 0.0f) {
                    uint8_t aByte = static_cast<uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                    uint8_t rByte = static_cast<uint8_t>(std::clamp(accR * invSamples, 0.0f, 255.0f));
                    uint8_t gByte = static_cast<uint8_t>(std::clamp(accG * invSamples, 0.0f, 255.0f));
                    uint8_t bByte = static_cast<uint8_t>(std::clamp(accB * invSamples, 0.0f, 255.0f));
                    pixels[y * size + x] = (aByte << 24) | (rByte << 16) | (gByte << 8) | bByte;
                }
                else {
                    pixels[y * size + x] = 0;
                }
            }
        }

        HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, nullptr);
        ICONINFO ii{};
        ii.fIcon = TRUE;
        ii.hbmMask = hbmMask;
        ii.hbmColor = hbmColor;

        HICON hIcon = CreateIconIndirect(&ii);
        DeleteObject(hbmColor);
        DeleteObject(hbmMask);
        return hIcon;
    }

    // --- Window Protection Logic ---
    void PruneDeadWindows() {
        for (auto it = g_app.protectedWindows.begin(); it != g_app.protectedWindows.end();) {
            if (!IsWindow(it->first)) {
                if (g_app.lastToggledHwnd == it->first) g_app.lastToggledHwnd = nullptr;
                it = g_app.protectedWindows.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    bool IsValidTargetWindow(HWND hWnd) {
        if (!hWnd || !IsWindow(hWnd) || hWnd == GetShellWindow() || hWnd == g_app.hMsgWnd)
            return false;

        wchar_t cls[64]{};
        GetClassNameW(hWnd, cls, 64);
        if (wcscmp(cls, L"Progman") == 0 ||
            wcscmp(cls, L"Shell_TrayWnd") == 0 ||
            wcscmp(cls, L"Windows.UI.Core.CoreWindow") == 0)
            return false;

        DWORD pid = 0;
        GetWindowThreadProcessId(hWnd, &pid);
        if (pid == GetCurrentProcessId()) return false;

        RECT rc{};
        GetWindowRect(hWnd, &rc);
        return (rc.right > rc.left && rc.bottom > rc.top);
    }

    void ProtectWindow(HWND hWnd) {
        if (!IsValidTargetWindow(hWnd)) return;

        WindowState state{};
        state.hWnd = hWnd;

        SetProcessDisplayAffinityRemote(hWnd, WDA_EXCLUDEFROMCAPTURE);

        if (g_app.hideFromTaskbar) {
            ModifyTaskbarState(hWnd, true);
            state.taskbarHidden = true;
        }

        if (g_app.minimizeOnHide && !IsIconic(hWnd)) {
            ShowWindow(hWnd, SW_MINIMIZE);
            state.minimizedByUs = true;
        }

        g_app.protectedWindows[hWnd] = state;
        g_app.lastToggledHwnd = hWnd;
    }

    void UnprotectWindow(HWND hWnd) {
        auto it = g_app.protectedWindows.find(hWnd);
        if (it == g_app.protectedWindows.end()) return;

        if (IsWindow(hWnd)) {
            if (it->second.taskbarHidden) {
                ModifyTaskbarState(hWnd, false);
            }
            SetProcessDisplayAffinityRemote(hWnd, WDA_NONE);

            if (it->second.minimizedByUs || IsIconic(hWnd)) {
                ShowWindow(hWnd, SW_RESTORE);
            }
            SetForegroundWindow(hWnd);
        }

        g_app.protectedWindows.erase(it);
        if (g_app.lastToggledHwnd == hWnd) {
            g_app.lastToggledHwnd = nullptr;
        }
    }

    void ToggleWindowProtection(HWND hWnd) {
        if (g_app.protectedWindows.find(hWnd) != g_app.protectedWindows.end()) {
            UnprotectWindow(hWnd);
        }
        else {
            ProtectWindow(hWnd);
        }
    }

    void HandleHotkeyToggle() {
        PruneDeadWindows();
        HWND hActive = GetForegroundWindow();

        if (hActive && g_app.protectedWindows.find(hActive) != g_app.protectedWindows.end()) {
            UnprotectWindow(hActive);
        }
        else if (g_app.lastToggledHwnd && g_app.protectedWindows.find(g_app.lastToggledHwnd) != g_app.protectedWindows.end()) {
            UnprotectWindow(g_app.lastToggledHwnd);
        }
        else if (hActive && IsValidTargetWindow(hActive)) {
            ProtectWindow(hActive);
        }
        ScheduleMemoryTrim();
    }

    void RestoreAll() {
        for (const auto& [hWnd, state] : g_app.protectedWindows) {
            if (!IsWindow(hWnd)) continue;
            if (state.taskbarHidden) ModifyTaskbarState(hWnd, false);
            SetProcessDisplayAffinityRemote(hWnd, WDA_NONE);
            if (state.minimizedByUs) ShowWindow(hWnd, SW_RESTORE);
        }
        g_app.protectedWindows.clear();
        g_app.lastToggledHwnd = nullptr;
    }

    void ApplyTaskbarPreferenceChange() {
        PruneDeadWindows();
        for (auto& [hWnd, state] : g_app.protectedWindows) {
            if (g_app.hideFromTaskbar && !state.taskbarHidden) {
                ModifyTaskbarState(hWnd, true);
                state.taskbarHidden = true;
            }
            else if (!g_app.hideFromTaskbar && state.taskbarHidden) {
                ModifyTaskbarState(hWnd, false);
                state.taskbarHidden = false;
            }
        }
    }

    // --- Tray & Hotkey Management ---
    void UpdateTrayIcon(DWORD msg) {
        std::wstring tip = L"HideDem (Ctrl+Shift+";
        tip += g_app.hotkeyChar;
        tip += L')';
        wcscpy_s(g_app.nid.szTip, tip.c_str());
        Shell_NotifyIconW(msg, &g_app.nid);
    }

    bool ChangeHotkeyLetter(wchar_t newLetter) {
        if (newLetter < L'A' || newLetter > L'Z') return false;
        if (newLetter == g_app.hotkeyChar) return true;

        UnregisterHotKey(g_app.hMsgWnd, ID_HOTKEY_ACTIVE);
        if (RegisterHotKey(g_app.hMsgWnd, ID_HOTKEY_ACTIVE, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, static_cast<UINT>(newLetter))) {
            g_app.hotkeyChar = newLetter;
            SaveSettings();
            UpdateTrayIcon(NIM_MODIFY);
            return true;
        }

        RegisterHotKey(g_app.hMsgWnd, ID_HOTKEY_ACTIVE, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, static_cast<UINT>(g_app.hotkeyChar));
        MessageBeep(MB_ICONWARNING);
        return false;
    }

    // --- Tray Menu ---
    BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
        auto* pList = reinterpret_cast<std::vector<WindowEntry>*>(lParam);

        if (!IsWindowVisible(hWnd) && g_app.protectedWindows.find(hWnd) == g_app.protectedWindows.end())
            return TRUE;
        if (GetWindowTextLengthW(hWnd) <= 0 || !IsValidTargetWindow(hWnd))
            return TRUE;

        wchar_t title[256]{};
        if (GetWindowTextW(hWnd, title, 256) <= 0) return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId(hWnd, &pid);

        std::wstring exeName;
        if (UniqueHandle hProc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)); hProc) {
            wchar_t path[MAX_PATH];
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProc.handle, 0, path, &size)) {
                if (const wchar_t* name = wcsrchr(path, L'\\')) exeName = name + 1;
            }
        }

        pList->push_back({ hWnd, title, std::move(exeName) });
        return TRUE;
    }

    void ShowContextMenu(HWND hWnd) {
        PruneDeadWindows();
        ApplySystemTheme();

        POINT pt{};
        GetCursorPos(&pt);

        HMENU hMenu = CreatePopupMenu();
        HMENU hAppSubMenu = CreatePopupMenu();
        HMENU hHotkeySubMenu = CreatePopupMenu();
        HMENU hLangSubMenu = CreatePopupMenu();

        std::wstring header = std::wstring(GetStr(StrId::ProtectedCountPrefix)) + std::to_wstring(g_app.protectedWindows.size());
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_DISABLED | MF_STRING, 0, header.c_str());
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        std::wstring toggleStr = std::wstring(GetStr(StrId::MenuTogglePrefix)) + g_app.hotkeyChar;
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_TOGGLE_ACTIVE, toggleStr.c_str());

        std::vector<WindowEntry> windows;
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));
        std::sort(windows.begin(), windows.end(), [](const WindowEntry& a, const WindowEntry& b) {
            return _wcsicmp(a.exeName.c_str(), b.exeName.c_str()) < 0;
            });

        if (windows.empty()) {
            InsertMenuW(hAppSubMenu, -1, MF_BYPOSITION | MF_DISABLED | MF_STRING, 0, GetStr(StrId::NoWindowsFound));
        }
        else {
            for (size_t i = 0; i < windows.size(); ++i) {
                const auto& w = windows[i];
                std::wstring itemText = (w.exeName.empty() ? L"" : (L"[" + w.exeName + L"] ")) + w.title;
                if (itemText.length() > 55) itemText = itemText.substr(0, 52) + L"...";

                UINT flags = MF_BYPOSITION | MF_STRING;
                if (g_app.protectedWindows.find(w.hWnd) != g_app.protectedWindows.end()) {
                    flags |= MF_CHECKED;
                }
                InsertMenuW(hAppSubMenu, -1, flags, ID_WINDOW_MENU_BASE + static_cast<UINT>(i), itemText.c_str());
            }
        }

        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(hAppSubMenu), GetStr(StrId::MenuSelectApp));

        UINT restoreFlags = MF_BYPOSITION | MF_STRING | (g_app.protectedWindows.empty() ? MF_GRAYED : 0);
        InsertMenuW(hMenu, -1, restoreFlags, ID_TRAY_RESTORE_ALL, GetStr(StrId::MenuRestoreAll));
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING | (g_app.hideFromTaskbar ? MF_CHECKED : 0),
            ID_TRAY_TASKBAR_TOGGLE, GetStr(StrId::MenuTaskbarToggle));
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING | (g_app.minimizeOnHide ? MF_CHECKED : 0),
            ID_TRAY_MINIMIZE_TOGGLE, GetStr(StrId::MenuMinimizeToggle));

        // Hotkey letter submenu (A..Z)
        for (wchar_t ch = L'A'; ch <= L'Z'; ++ch) {
            wchar_t label[2] = { ch, L'\0' };
            UINT flags = MF_BYPOSITION | MF_STRING | (g_app.hotkeyChar == ch ? MF_CHECKED : 0);
            InsertMenuW(hHotkeySubMenu, -1, flags, ID_HOTKEY_LETTER_BASE + static_cast<UINT>(ch - L'A'), label);
        }
        std::wstring hotkeyMenuStr = std::wstring(GetStr(StrId::MenuHotkeyPrefix)) + g_app.hotkeyChar;
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(hHotkeySubMenu), hotkeyMenuStr.c_str());

        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        InsertMenuW(hLangSubMenu, -1, MF_BYPOSITION | MF_STRING | (g_app.lang == Language::English ? MF_CHECKED : 0), ID_LANG_EN, L"English");
        InsertMenuW(hLangSubMenu, -1, MF_BYPOSITION | MF_STRING | (g_app.lang == Language::Russian ? MF_CHECKED : 0), ID_LANG_RU, L"\u0420\u0443\u0441\u0441\u043A\u0438\u0439");
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(hLangSubMenu), GetStr(StrId::MenuLanguage));

        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0),
            ID_TRAY_AUTOSTART, GetStr(StrId::MenuAutostart));

        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, GetStr(StrId::MenuExit));

        SetForegroundWindow(hWnd);
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, nullptr);
        PostMessageW(hWnd, WM_NULL, 0, 0);

        switch (cmd) {
        case ID_TRAY_TOGGLE_ACTIVE:
            HandleHotkeyToggle();
            break;
        case ID_TRAY_EXIT:
            RestoreAll();
            PostQuitMessage(0);
            break;
        case ID_TRAY_RESTORE_ALL:
            RestoreAll();
            break;
        case ID_TRAY_TASKBAR_TOGGLE:
            g_app.hideFromTaskbar = !g_app.hideFromTaskbar;
            SaveSettings();
            ApplyTaskbarPreferenceChange();
            break;
        case ID_TRAY_MINIMIZE_TOGGLE:
            g_app.minimizeOnHide = !g_app.minimizeOnHide;
            SaveSettings();
            break;
        case ID_TRAY_AUTOSTART:
            ToggleAutostart(!IsAutostartEnabled());
            break;
        case ID_LANG_EN:
            g_app.lang = Language::English;
            SaveSettings();
            UpdateTrayIcon(NIM_MODIFY);
            break;
        case ID_LANG_RU:
            g_app.lang = Language::Russian;
            SaveSettings();
            UpdateTrayIcon(NIM_MODIFY);
            break;
        default:
            if (cmd >= static_cast<int>(ID_HOTKEY_LETTER_BASE) && cmd < static_cast<int>(ID_HOTKEY_LETTER_BASE + 26)) {
                ChangeHotkeyLetter(static_cast<wchar_t>(L'A' + (cmd - ID_HOTKEY_LETTER_BASE)));
            }
            else if (cmd >= static_cast<int>(ID_WINDOW_MENU_BASE) && cmd < static_cast<int>(ID_WINDOW_MENU_BASE + windows.size())) {
                ToggleWindowProtection(windows[cmd - ID_WINDOW_MENU_BASE].hWnd);
            }
            break;
        }

        DestroyMenu(hMenu);
        ScheduleMemoryTrim();
    }

    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == g_app.wmTaskbarCreated && g_app.wmTaskbarCreated != 0) {
            UpdateTrayIcon(NIM_ADD);
            return 0;
        }

        switch (msg) {
        case WM_TIMER:
            if (wParam == ID_TIMER_TRIM) {
                KillTimer(hWnd, ID_TIMER_TRIM);
                TrimMemory();
            }
            return 0;
        case WM_HOTKEY:
            if (wParam == ID_HOTKEY_ACTIVE) {
                HandleHotkeyToggle();
            }
            return 0;
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                ShowContextMenu(hWnd);
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
                ApplySystemTheme();
                ScheduleMemoryTrim();
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hWnd, ID_TIMER_TRIM);
            UnregisterHotKey(hWnd, ID_HOTKEY_ACTIVE);
            RestoreAll();
            Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    UniqueHandle hMutex(CreateMutexW(nullptr, TRUE, MUTEX_NAME));
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList, reinterpret_cast<void**>(&g_app.pTaskbarList)))) {
        g_app.pTaskbarList->HrInit();
    }

    LoadSettings();
    ApplySystemTheme();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"HideDemClass";
    RegisterClassExW(&wc);

    g_app.wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_app.hMsgWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    g_app.hAppIcon = GenerateProceduralIcon();

    RegisterHotKey(g_app.hMsgWnd, ID_HOTKEY_ACTIVE, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, static_cast<UINT>(g_app.hotkeyChar));

    g_app.nid.hWnd = g_app.hMsgWnd;
    g_app.nid.uID = 1;
    g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.hIcon = g_app.hAppIcon;
    UpdateTrayIcon(NIM_ADD);

    OptimizeProcessForIdle();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_app.hAppIcon) DestroyIcon(g_app.hAppIcon);
    if (g_app.pTaskbarList) g_app.pTaskbarList->Release();
    CoUninitialize();
    return 0;
}