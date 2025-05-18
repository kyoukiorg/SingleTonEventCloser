#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdarg.h>

typedef NTSTATUS (NTAPI *NtQuerySystemInformation_t)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

#define SystemHandleInformation 16

typedef struct _SYSTEM_HANDLE {
    ULONG       ProcessId;
    BYTE        ObjectTypeNumber;
    BYTE        Flags;
    USHORT      Handle;
    PVOID       Object;
    ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE, *PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION {
    ULONG HandleCount;
    SYSTEM_HANDLE Handles[1];
} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION;

BOOL GetHandleName(HANDLE h, WCHAR* name, DWORD size);

void log_message(const char* fmt, ...) {
    FILE* f = fopen("roblox_kill_log.txt", "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

BOOL CloseRobloxSingletonEventHandle() {
    BOOL found = FALSE;
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        log_message("Failed to create process snapshot\n");
        return FALSE;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hProcessSnap, &pe32)) {
        log_message("Failed to get first process\n");
        CloseHandle(hProcessSnap);
        return FALSE;
    }

    do {
        if (strcmp(pe32.szExeFile, "RobloxPlayerBeta.exe") == 0) {
            HANDLE hProcess = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
            if (hProcess == NULL) {
                log_message("Failed to open ROBLOX process (PID: %d)\n", pe32.th32ProcessID);
                continue;
            }
            log_message("Found ROBLOX process (PID: %d)\n", pe32.th32ProcessID);

            HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
            if (!hNtdll) {
                log_message("Failed to get ntdll.dll handle\n");
                CloseHandle(hProcess);
                continue;
            }
            NtQuerySystemInformation_t NtQuerySystemInformation = (NtQuerySystemInformation_t)GetProcAddress(hNtdll, "NtQuerySystemInformation");
            if (!NtQuerySystemInformation) {
                log_message("Failed to get NtQuerySystemInformation address\n");
                CloseHandle(hProcess);
                continue;
            }

            DWORD handleInfoSize = 0x10000;
            PSYSTEM_HANDLE_INFORMATION handleInfo = (PSYSTEM_HANDLE_INFORMATION)malloc(handleInfoSize);
            if (!handleInfo) {
                log_message("Failed to allocate memory for handleInfo\n");
                CloseHandle(hProcess);
                continue;
            }

            NTSTATUS status;
            ULONG returnLength = 0;
            while ((status = NtQuerySystemInformation(
                SystemHandleInformation,
                handleInfo,
                handleInfoSize,
                &returnLength
            )) == 0xC0000004) {
                handleInfoSize *= 2;
                PSYSTEM_HANDLE_INFORMATION newHandleInfo = (PSYSTEM_HANDLE_INFORMATION)realloc(handleInfo, handleInfoSize);
                if (!newHandleInfo) {
                    log_message("Failed to reallocate memory for handleInfo\n");
                    free(handleInfo);
                    CloseHandle(hProcess);
                    handleInfo = NULL;
                    break;
                }
                handleInfo = newHandleInfo;
            }

            if (handleInfo && status == 0) {
                for (ULONG i = 0; i < handleInfo->HandleCount; i++) {
                    SYSTEM_HANDLE handle = handleInfo->Handles[i];

                    if (handle.ProcessId == pe32.th32ProcessID) {
                        HANDLE hDup = NULL;
                        if (DuplicateHandle(
                            hProcess,
                            (HANDLE)(ULONG_PTR)handle.Handle,
                            GetCurrentProcess(),
                            &hDup,
                            0,
                            FALSE,
                            DUPLICATE_SAME_ACCESS
                        )) {
                            WCHAR name[MAX_PATH] = {0};
                            if (GetHandleName(hDup, name, MAX_PATH)) {
                                if (wcsstr(name, L"ROBLOX_singletonEvent") != NULL) {
                                    log_message("Found target handle: %ls\n", name);

                                    BOOL closeResult = FALSE;
                                    HANDLE hDummy = NULL;
                                    closeResult = DuplicateHandle(
                                        hProcess,
                                        (HANDLE)(ULONG_PTR)handle.Handle,
                                        GetCurrentProcess(),
                                        &hDummy,
                                        0,
                                        FALSE,
                                        DUPLICATE_CLOSE_SOURCE
                                    );
                                    if (closeResult) {
                                        log_message("Successfully closed target handle in ROBLOX process (PID: %d)\n", pe32.th32ProcessID);
                                        if (hDummy) CloseHandle(hDummy);
                                        found = TRUE;
                                    } else {
                                        DWORD error = GetLastError();
                                        log_message("Failed to close target handle in ROBLOX process (PID: %d), error: %lu\n", pe32.th32ProcessID, error);
                                    }
                                    CloseHandle(hDup);
                                    break;
                                } else {
                                    log_message("Handle name (not match): %ls\n", name);
                                }
                            } else {
                                log_message("Failed to get handle name for handle 0x%04X\n", handle.Handle);
                            }
                            CloseHandle(hDup);
                        } else {
                            log_message("Failed to duplicate handle 0x%04X\n", handle.Handle);
                        }
                    }
                }
            } else {
                log_message("Failed to query system handles (status: 0x%08X)\n", (unsigned int)status);
            }

            if (handleInfo)
                free(handleInfo);
            CloseHandle(hProcess);
            if (found) break;
        }
    } while (Process32Next(hProcessSnap, &pe32));

    CloseHandle(hProcessSnap);

    if (!found) {
        log_message("Did not find target handle or failed to close handle in ROBLOX process\n");
    }

    return found;
}

BOOL GetHandleName(HANDLE h, WCHAR* name, DWORD size) {
    typedef NTSTATUS (NTAPI *NtQueryObject_t)(
        HANDLE Handle,
        ULONG ObjectInformationClass,
        PVOID ObjectInformation,
        ULONG ObjectInformationLength,
        PULONG ReturnLength
    );

    typedef struct _UNICODE_STRING {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR  Buffer;
    } UNICODE_STRING, *PUNICODE_STRING;

    typedef struct _OBJECT_NAME_INFORMATION {
        UNICODE_STRING Name;
    } OBJECT_NAME_INFORMATION, *POBJECT_NAME_INFORMATION;

    static NtQueryObject_t pNtQueryObject = NULL;
    if (pNtQueryObject == NULL) {
        HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
        if (!hNtdll) return FALSE;
        pNtQueryObject = (NtQueryObject_t)GetProcAddress(hNtdll, "NtQueryObject");
        if (pNtQueryObject == NULL) return FALSE;
    }

    ULONG len = 0;
    pNtQueryObject(h, 1, NULL, 0, &len);
    if (len == 0) return FALSE;

    POBJECT_NAME_INFORMATION nameInfo = (POBJECT_NAME_INFORMATION)malloc(len);
    if (!nameInfo) return FALSE;

    if (pNtQueryObject(h, 1, nameInfo, len, &len) == 0) {
        if (nameInfo->Name.Buffer) {
            size_t copyLen = nameInfo->Name.Length / sizeof(WCHAR);
            if (copyLen >= size) copyLen = size - 1;
            wcsncpy(name, nameInfo->Name.Buffer, copyLen);
            name[copyLen] = 0;
            free(nameInfo);
            return TRUE;
        }
    }

    free(nameInfo);
    return FALSE;
}

#define IDC_DESC_LABEL 1000
#define IDC_KILL_BUTTON 1001
#define IDC_STATUS_LABEL 1002

#define COLOR_BG RGB(255, 255, 255)
#define COLOR_BTN_BG RGB(0, 120, 215)
#define COLOR_BTN_BG_HOVER RGB(0, 150, 255)
#define COLOR_BTN_TEXT RGB(255,255,255)
#define COLOR_STATUS_SUCCESS RGB(0, 180, 0)
#define COLOR_STATUS_FAIL RGB(200, 0, 0)

HBRUSH hBgBrush;
BOOL buttonHover = FALSE;

void SetRoundedCorners(HWND hwnd, int radius) {
    HRGN hRgn = CreateRoundRectRgn(0, 0, 420, 190, radius, radius);
    SetWindowRgn(hwnd, hRgn, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hButton, hStatus, hDesc;
    static HFONT hFontLarge = NULL, hFontNormal = NULL, hFontButton = NULL;
    static COLORREF statusColor = COLOR_STATUS_FAIL;
    switch (msg) {
    case WM_CREATE: {
        hBgBrush = CreateSolidBrush(COLOR_BG);

        LOGFONT lf = {0};
        lf.lfHeight = 28;
        lf.lfWeight = FW_BOLD;
        strcpy(lf.lfFaceName, "Segoe UI");
        hFontLarge = CreateFontIndirect(&lf);
        lf.lfHeight = 18;
        lf.lfWeight = FW_NORMAL;
        hFontNormal = CreateFontIndirect(&lf);
        lf.lfHeight = 20;
        lf.lfWeight = FW_BOLD;
        hFontButton = CreateFontIndirect(&lf);

        hDesc = CreateWindowExA(
            0, "STATIC", "Allow Multiple Instances of Roblox",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            10, 18, 390, 40,
            hwnd, (HMENU)IDC_DESC_LABEL, GetModuleHandleA(NULL), NULL);
        SendMessage(hDesc, WM_SETFONT, (WPARAM)hFontLarge, TRUE);

        hButton = CreateWindowExA(
            0, "BUTTON", "Allow Multiple Instances",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            85, 70, 250, 45,
            hwnd, (HMENU)IDC_KILL_BUTTON, GetModuleHandleA(NULL), NULL);
        SendMessage(hButton, WM_SETFONT, (WPARAM)hFontButton, TRUE);

        hStatus = CreateWindowExA(
            0, "STATIC", "",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            10, 130, 390, 30,
            hwnd, (HMENU)IDC_STATUS_LABEL, GetModuleHandleA(NULL), NULL);
        SendMessage(hStatus, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

        SetRoundedCorners(hwnd, 18);
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_KILL_BUTTON) {
            HBRUSH hBrush = CreateSolidBrush(buttonHover ? COLOR_BTN_BG_HOVER : COLOR_BTN_BG);
            FillRect(dis->hDC, &dis->rcItem, hBrush);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, COLOR_BTN_TEXT);

            RECT rc = dis->rcItem;
            DrawTextA(dis->hDC, "Allow Multiple Instances", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            DeleteObject(hBrush);
            return TRUE;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetWindowRect(hButton, &rc);
        MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rc, 2);
        BOOL hover = PtInRect(&rc, pt);
        if (hover != buttonHover) {
            buttonHover = hover;
            InvalidateRect(hButton, NULL, TRUE);
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_KILL_BUTTON) {
            SetWindowTextA(hStatus, "Working...");
            statusColor = COLOR_STATUS_FAIL;
            BOOL result = CloseRobloxSingletonEventHandle();
            if (result) {
                SetWindowTextA(hStatus, "You can now open multiple Roblox applications!");
                statusColor = COLOR_STATUS_SUCCESS;
            } else {
                SetWindowTextA(hStatus, "Failed to allow multiple instances. Please try again.");
                statusColor = COLOR_STATUS_FAIL;
            }
            InvalidateRect(hStatus, NULL, TRUE);
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == hStatus) {
            SetBkColor(hdcStatic, COLOR_BG);
            SetTextColor(hdcStatic, statusColor);
            return (INT_PTR)hBgBrush;
        } else {
            SetBkColor(hdcStatic, COLOR_BG);
            SetTextColor(hdcStatic, RGB(30,30,30));
            return (INT_PTR)hBgBrush;
        }
    }
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, hBgBrush);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        if (hFontLarge) DeleteObject(hFontLarge);
        if (hFontNormal) DeleteObject(hFontNormal);
        if (hFontButton) DeleteObject(hFontButton);
        if (hBgBrush) DeleteObject(hBgBrush);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "RobloxKillerClass";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;

    RegisterClassExA(&wc);

    int width = 420, height = 190;
    int screenX = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int screenY = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = CreateWindowExA(
        0, "RobloxKillerClass", "Roblox Multi-Instance Enabler",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        screenX, screenY, width, height,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxA(NULL, "Failed to create window.", "Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return 0;
}
