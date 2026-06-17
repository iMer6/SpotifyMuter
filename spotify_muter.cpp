#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <iostream>
#include <string>
#include <vector>
#include <shellapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

using std::wstring;
using WindowProcedureType = LRESULT(__stdcall*)(HWND, UINT, WPARAM, LPARAM);

// constexpr – constant expression.
// A constexpr variable value must be known and computable at compile-time.
// const can be evaluate in runtime or compile-time
//
// WM_USER – constant that used to define private messages for custom window classes.
// Default WM_USER value is 0x0400 in hex or 1024 in decimal.
//
// WM_TRAYICON create unique ID for message that program will use
// for messaging with system tray (notification scope). 
//
/**
 * @brief Unique ID for system tray messages.
 */
constexpr unsigned int WM_TRAYICON = WM_USER + 1;
//
// ID_TRAY_EXIT – program exit ID.
// If user clicked on button "Exit" in tray menu,
// the program stop working.  
//
/**
 * @brief Program exit ID.
 * @note If user clicked on button "Exit of Muter" in tray menu,
 * the program stop.
 */
constexpr unsigned int ID_TRAY_EXIT = 1001;
/** 
 * @brief Struct that storing icon parameters.
 * Using to manage icons in notification scope.
 */
static NOTIFYICONDATAW nid = {};
/**
 * @brief Handle to a WiNDow – window object descriptor.
 * Address to which the system sends message.
 */
static HWND hWndInvisible = NULL;

bool IsAd(const wstring&);
wstring GetActiveTitle(DWORD);
void RefreshAndMute();

LRESULT __stdcall WndProc(
    HWND hWnd, // handle of the window that received the message
    UINT message, // event ID
    WPARAM wParam, // additional message information
    LPARAM lParam // additional message information
) {
    // Click on the tray icon
    if (message == WM_TRAYICON) {
        // WM_RBUTTONUP – RMB released (click)
        if (lParam == WM_RBUTTONUP) {
            POINT currentMouseCoords;
            GetCursorPos(&currentMouseCoords); // current cursor coordinates

            HMENU hMenu = CreatePopupMenu(); // empty pop-up menu handle
            InsertMenuW(
                hMenu, // a handle to the menu to be changed
                0, // before which menu item should a new one be inserted?
                MF_BYPOSITION | MF_STRING, // flags
                    // MF_BYPOSITION – 2nd parameter is the zero-based relative position
                    // MF_STRING – the menu item (last parameter) is a text string
                ID_TRAY_EXIT, // ID of the new menu item
                L"Exit of Muter" // content of the new menu item
            );
            
            SetForegroundWindow(hWnd); // focuses an invisible window.
            // For Windows: if focus is lost, the menu should be hidden
            // Without `SetforegroundWindow` the pop-up menu will behave buggy:
            // the menu won't close when clicking past
            
            // The menu will appear where the mouse is now
            // Lock the thread and displays the menu on the screen
            // Waits for user to either select an item or click past
            TrackPopupMenu(
                hMenu, // a handle to the menu to be displayed
                TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON, // flags
                    // TPM_LEFTALIGN – menu's left edge at X (3rd parameter)
                    // TPM_BOTTOMALIGN – menu's bottom edge at Y (4th parameter)
                    // TPM_LEFTBUTTON – user can select menu item with only LMB
                currentMouseCoords.x, // horizontal location of the menu, in screen coords
                currentMouseCoords.y, // vertical location of the menu, in screen coords
                0, // reserved. Must be 0
                hWnd, // a handle to the window that owns the menu
                nullptr
            );
            
            // Empty message for correct focus switching
            PostMessageW(
                hWnd, // handle of the window to which message is sent
                WM_NULL, // message to be posted (empty)
                0, // additional message-specific information (0 because WM_NULL)
                0 // additional message-specific information (0 because WM_NULL)
            );
            
            DestroyMenu(hMenu); // memory deallocation
        }
    // Click on the exit item
    // WM_COMMAND – interaction with the menu button
    } else if (message == WM_COMMAND) {
        // LOWORD (LOw WORD) – lower 16 bits – ID of the element that triggers the event
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            // The OS starts destroying the window:
                // a WM_DESTROY message is sent, followed by a WM_NCDESTROY
                // to the window procedure of that window. 
                // Gives the window time to free its resources
                // but doesn't terminate the app process.
            DestroyWindow(hWnd);
            return 0;
        }
    } else if (message == WM_DESTROY) {
        // The program icon is removed from the tray
        Shell_NotifyIconW(
            NIM_DELETE, // action to be taken by this function
            &nid // pointer to NOTIFYICONDATA structure
        );

        // Why not exit(0)?
            // exit(0) forcefully terminates the process.
            // While it closes handles, flushes buffers,
            // and calls destructors for global objects,
            // it ignores the Windows message queue and local function scopes.
            // ==> local destructors inside the window procedure won't be called,
            // stack unwinding is skipped,
            // and OS resources might not be released correctly.
            //
            // DestroyWindow + WM_DESTROY (Shell_NotifyIcon + PostQuitMessage) 
            // is the standard way to allow the app to exit naturally.

        // Program termination after processing window destroy messages:
            // puts a WM_QUIT message in the thread's message queue.
            // When the main message loop reaches WM_QUIT,
            // the GetMessage() returns 0 (or FALSE).
            // This leads to an exit from the while loop in main
            // and the program terminates naturally.
        PostQuitMessage(0);
    }
    
    return DefWindowProc(
        hWnd, // a handle to the window procedure that received the message
        message, // the message
        wParam, // additional message information
        lParam // additional message information
    );
}

DWORD WINAPI MuteThread(LPVOID lpParam) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    while (true) {
        RefreshAndMute();
        Sleep(700);
    }
    CoUninitialize();
    return 0;
}

int WINAPI WinMain(HINSTANCE HInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SpotifyMuterClass";
    RegisterClassExW(&wc);
    hWndInvisible = CreateWindowExW(0, L"SpotifyMuterClass", L"SpotifyMuter", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hWndInvisible;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Spotify Ad Muter активний");
    Shell_NotifyIconW(NIM_ADD, &nid);

    CreateThread(NULL, 0, MuteThread, NULL, 0, NULL);

    ShowWindow(GetConsoleWindow(), SW_HIDE);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

bool IsAd(const wstring& t) {
    if (t.empty()) return true;
    if (t == L"Spotify" || t == L"Advertisement" || t == L"Spotify Free" || t == L"Spotify Premium") return true;
    if (t.find(L" - ") == wstring::npos) return true;
    if (t.find(L"Ad") != wstring::npos || t.find(L"advert") != wstring::npos || t.find(L"Реклама") != wstring::npos) return true;
    return false;
}

wstring GetActiveTitle(DWORD pid) {
    struct Target { DWORD pid; wstring title; };
    Target target = { pid, L"" };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        Target* t = (Target*)lp;
        DWORD winPid;
        GetWindowThreadProcessId(hwnd, &winPid);
        if (winPid == t->pid && IsWindowVisible(hwnd)) {
            wchar_t buf[512];
            if (GetWindowTextW(hwnd, buf, 512) > 0) {
                wstring s(buf);
                if (s.find(L"GDI+") == wstring::npos) {
                    t->title = s;
                    return FALSE;
                }
            }
        }
        return TRUE;
    }, (LPARAM)&target);
    return target.title;
}

void RefreshAndMute() {
    IMMDeviceEnumerator* pEnum = NULL;
    CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&pEnum
    );
    
    if (!pEnum) return;

    IMMDevice* pDev = NULL;
    if (FAILED(pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDev))) {
        pEnum->Release();
        return;
    }

    IAudioSessionManager2* pMgr = NULL;
    pDev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pMgr);

    IAudioSessionEnumerator* pList = NULL;
    if (pMgr && SUCCEEDED(pMgr->GetSessionEnumerator(&pList))) {
        int count = 0;
        pList->GetCount(&count);
        std::vector<DWORD> spotifyPids;

        for (int i = 0; i < count; i++) {
            IAudioSessionControl* pControl = NULL;
            pList->GetSession(i, &pControl);
            IAudioSessionControl2* pControl2 = NULL;
            if (pControl && SUCCEEDED(pControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pControl2))) {
                DWORD pid;
                pControl2->GetProcessId(&pid);
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (h) {
                    wchar_t path[MAX_PATH];
                    DWORD sz = MAX_PATH;
                    if (QueryFullProcessImageNameW(h, 0, path, &sz)) {
                        if (wstring(path).find(L"Spotify.exe") != wstring::npos) spotifyPids.push_back(pid);
                    }
                    CloseHandle(h);
                }
                pControl2->Release();
            }
            if (pControl) pControl->Release();
        }

        bool muteEverything = false;
        wstring currentTitle = L"";

        for (DWORD pid : spotifyPids) {
            wstring t = GetActiveTitle(pid);
            if (!t.empty()) {
                currentTitle = t;
                if (IsAd(t)) muteEverything = true;
                break;
            }
        }

        if (currentTitle.empty() || currentTitle == L"Spotify") muteEverything = true;

        for (int i = 0; i < count; i++) {
            IAudioSessionControl* pControl = NULL;
            pList->GetSession(i, &pControl);
            IAudioSessionControl2* pControl2 = NULL;
            if (pControl && SUCCEEDED(pControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pControl2))) {
                DWORD pid;
                pControl2->GetProcessId(&pid);
                bool isSpotify = false;
                for (DWORD s : spotifyPids) if (s == pid) { isSpotify = true; break; }

                if (isSpotify) {
                    ISimpleAudioVolume* pVol = NULL;
                    pControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pVol);
                    if (pVol) {
                        pVol->SetMute(muteEverything, NULL);
                        pVol->Release();
                    }
                }
                pControl2->Release();
            }
            if (pControl) pControl->Release();
        }
        pList->Release();
    }
    if (pMgr) pMgr->Release();
    pDev->Release();
    pEnum->Release();
}