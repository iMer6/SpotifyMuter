#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <shellapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

using std::wstring;
using WindowProcedureType = LRESULT(__stdcall*)(HWND, UINT, WPARAM, LPARAM);

// constexpr – constant expression.
// A constexpr variable value must be known and computable at compile-time.
// const can be evaluate in runtime or compile-time
namespace {
    // WM_USER – constant that used to define private messages for custom window classes.
    // Default WM_USER value is 0x0400 (hex) or 1024 (decimal).
    // WM_TRAYICON create unique ID for message that program will use
    // for messaging with system tray (notification scope).
    /**
     * @brief Unique ID for system tray messages.
     */
    constexpr unsigned int WM_TRAYICON = WM_USER + 1;

    // ID_TRAY_EXIT – program exit ID.
    // If user clicked on button "Exit" in tray menu,
    // the program stop working.
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
    NOTIFYICONDATAW nid = {};

    /**
     * @brief Handle to a WiNDow – window object descriptor.
     * Address to which the system sends message.
     */
    HWND hWndInvisible = nullptr;

    /**
     * @brief Event handle used to signal the background thread to stop.
     */
    HANDLE g_StopEvent = nullptr;

    HANDLE hMuteThread = nullptr;
}

bool IsAd(const wstring&);
wstring GetActiveTitle(DWORD);
void RefreshAndMute();

/**
 * @brief Window procedure that handles system messages for the app's window.
 * 
 * Manages window messages for system tray interaction,
 * context menu execution, and graceful app shutdown.
 * 
 * @param hWnd handle to the window that received the message.
 * @param message the event/message ID.
 * @param wParam additional message information (message-dependent).
 * @param lParam additional message information (message-dependent).
 * 
 * @return LRESULT – the result of the message processing (depends on the message sent).
 * 
 * @note Unhandled messages are passed to `DefWindowProc`.
 * @note Implements standard WinAPI workflow, ensuring reliable focus shifting
 * via `SetForegroundWindow` and `WM_NULL`, as well as clean resources deallocation
 * instead of a hard process termination.
 * 
 * @note ASCII schema
 * ```text
 * Incoming Message
 *      |
 *      |==> Click on tray icon (message == WM_TRAYICON)
 *      |       |
 *      |       | ==> Create pop-up menu with button "Exit of Muter"
 *      |
 *      |==> Click on the exit button (message == WM_COMMAND and ID_TRAY_EXIT)
 *      |       |
 *      |       | ==> DestroyWindow() ==> next message is WM_DESTROY ---+
 *      |       | ==> Returns 0                                         |
 *      |                                                               |
 *      |==> message == WM_DESTROY <== ---------------------------------+
 *      |       |
 *      |       | ==> Trigger event to finish, close handles, remove tray icon
 *      |       | ==> Natural exit (DestroyWindow + Shell_NotifyIcon + PostQuitMessage)
 *      |       | ==> Returns 0
 *      |
 *      | ==> Other messages (not processed)
 *              │
 *              | ==> DefWindowProc()
 * ```
 */
LRESULT __stdcall WndProc(
    HWND hWnd, // handle of the window that received the message
    UINT message, // event ID
    WPARAM wParam, // additional message information
    LPARAM lParam // additional message information
) {
    switch(message) {
        // Click on the tray icon
        case WM_TRAYICON: {
            // WM_RBUTTONUP – RMB released (click)
            // WM_LBUTTONUP – LMB released (click)
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
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
            return 0; // message processed
        }

        // Click on the exit item
        // WM_COMMAND – interaction with the menu button
        case WM_COMMAND: {
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
            break;
        }

        case WM_DESTROY: {
            if (g_StopEvent) {
                SetEvent(g_StopEvent);
                WaitForSingleObject(hMuteThread, INFINITE);
                CloseHandle(hMuteThread);
                CloseHandle(g_StopEvent);
            }

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
            return 0;
            // break is not needed (unreachable code)
        }
    }
    return DefWindowProc(
        hWnd, // a handle to the window procedure that received the message
        message, // the message
        wParam, // additional message information
        lParam // additional message information
    );
}

/**
 * @brief Worker thread procedure that periodically refreshes and mutes audio sessions.
 * 
 * This func initializes the COM library in Multithreaded Apartment (MTA) mode
 * to interact with the Windows Core Audio APIs.
 * It runs an efficient polling loop until a stop signal is received.
 * 
 * @param lpParam unused thread parameter (marked as `[[maybe_unused]]`).
 * 
 * @note The loop executes `RefreshAndMute()` every 500 ms.
 * @note It relies on the global synchronization object `g_StopEvent`.
 * Triggering this event breaks the loop immediately,
 * ensuring a responsive thread shutdown.
 * 
 * @note ASCII schema
 * ```text
 * Start thread
 *      |
 *      |
 * Turn on COM subsystem (CoInitializeEx)
 *      |
 *      |==> Wait 500 ms or stop signal <== --------------------+
 *      |       |                                               |
 *      |       |==> 500 ms passed ==> Call RefreshAndMute() ---+
 *      |       |
 *      |       |==> Signal received ------+
 *      |                                  |
 * Turn off COM (CoUninitialize) <== ------+
 *      |
 *      |
 * End (return 0)
 * ```
 * 
 * @return `DWORD` – returns 0 upon successful completion and clean COM uninitialization. 
 */
DWORD __stdcall MuteThread([[maybe_unused]] LPVOID lpParam) {
    /*
    Function RefreshAndMute interact with Windows volume mixer.
    It needs access to the Core Audio API.
    These APIs are built on Microsoft's COM technology.

    COM <==> Component Ojbect Model
    */

    // Initializing the COM library for the current thread
    CoInitializeEx(
        nullptr, // reserved. Must be nullptr
        COINIT_MULTITHREADED /* MTA (Multithreaded Apartment) model
            COM objects creates in this thread can be called from other threads
            without additional marshaling from the system */
    );

    /*
    Workhorse
    
    If g_StopEvent was raised (SetEvent) {
        WaitForSingleObject returns WAIT_OBJECT_0.
        WAIT_OBJECT_0 != WAIT_TIMEOUT ==> false.
        Cycle stop.
    } Else (500 ms have passed) {
        WaitForSingleObject returns WAIT_TIMEOUT.
        WAIT_TIMEOUT == WAIT_TIMEOUT ==> true.
        Cycle continues.
    }
    */
    while (WaitForSingleObject(g_StopEvent, 500) == WAIT_TIMEOUT) {
        RefreshAndMute();
    }

    /*
    Closes the COM library on the current thread,
    unloads all DLLs loaded by the thread,
    frees any other resources that the thread maintains,
    and forces all RPC connections on the thread to close
    */
    CoUninitialize();
    return 0;
}

int WINAPI WinMain(HINSTANCE HInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    g_StopEvent = CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        L"SpotifyMuterStopEvent"
    );
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

    hMuteThread = CreateThread(NULL, 0, MuteThread, NULL, 0, NULL);

    ShowWindow(GetConsoleWindow(), SW_HIDE);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

/**
 * @brief Checks whether the given window title corresponds to an advertisement
 * or a commercial Spotify state.
 * 
 * Analises the window title strin to determine if it represents an ad
 * or a generic Spotify state (like "Spotify Free") rather than an actual song.
 * 
 * @param title a constant reference to the window title to check.
 * 
 * @return bool – is an ad playing right now?
 */
bool IsAd(const wstring& title) {
    if (title.empty()) {
        return false;
    }
    
    if (title == L"Spotify" ||
        title == L"Слухайте музику без реклами" ||
        title == L"Advertisement" ||
        title == L"Spotify Free" ||
        title == L"Spotify Premium" ||
        title == L"Реклама"
    ) { return true; }
    
    // npos (no position) – substring is not found
    
    if (title.find(L" - ") == wstring::npos) {
        return true;
    }

    if (title.find(L"Advertisement") != wstring::npos ||
        title.find(L"advert") != wstring::npos ||
        title.find(L"Реклама") != wstring::npos
    ) {
        return true;
    }

    return false;
}

std::vector<wstring> GetSpotifyTitles(const std::vector<DWORD>& spotifyPIDs) {
    /*
    The EnumWindows callback function cannot direclty return a value.
    Local Target structure is created,
    where the PID that needs to be found will be written,
    and an empty title string, where the result will be written.
    */
    struct Target {
        const std::vector<DWORD>& pids;
        std::vector<wstring> titles;
    };
    Target target = {
        spotifyPIDs,
        {}
    };
    
    EnumWindows([](
        HWND hwnd, // handle to the window
        LPARAM lp // (LPARAM)&target
    ) -> BOOL {
        // Pointer to the original target variable
        Target* pTarget = reinterpret_cast<Target*>(lp);

        DWORD winPid;
        // What PID does the current window belong to?
        GetWindowThreadProcessId(
            hwnd, // handle to the window
            &winPid // pointer to a variable that receives the PID
        );

        auto it = std::find(
            pTarget->pids.begin(),
            pTarget->pids.end(),
            winPid
        );

        // Is this the desired PID and is this window visible?
        if (it != pTarget->pids.end() && IsWindowVisible(hwnd)) {
        // if (winPid == pTarget->pid && IsWindowVisible(hwnd)) {
            wchar_t windowTitleBuffer[512];
            // Does the window have a title?
            if (GetWindowTextW(
                    hwnd, // handle to the window
                    windowTitleBuffer, // buffer that will receive the text
                    std::size(windowTitleBuffer) // max number of characters to copy
                ) > 0
            ) {
                wstring windowTitle(windowTitleBuffer); // wchar_t ==> wstring
                /*
                GDI+ – service or background windows
                created by the Windows graphics subsystem (not required)
                */
                if (windowTitle.find(L"GDI+") == wstring::npos) {
                    pTarget->titles.push_back(windowTitle);
                }
            }
        }
        return TRUE; // continue searching
    }, reinterpret_cast<LPARAM>(&target));

    return target.titles;
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

        //
        // new
        //
        std::vector <wstring> activeSpotifyTitles = GetSpotifyTitles(spotifyPids);
        for (const wstring& title : activeSpotifyTitles) {
            if (!title.empty()) {
                currentTitle = title;

                if (IsAd(title)) {
                    muteEverything = true;
                    break;
                }
            }
        }
        //
        //
        //

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