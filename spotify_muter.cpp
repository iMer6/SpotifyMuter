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
    NOTIFYICONDATAW g_nid = {};

    /**
     * @brief Handle to a WiNDow – window object descriptor.
     * Address to which the system sends message.
     */
    HWND g_hWndInvisible = nullptr;

    /**
     * @brief Event handle used to signal the background thread to stop.
     */
    HANDLE g_StopEvent = nullptr;

    /**
     * @brief Handle to the created thread.
     * Thread is created in WinMain by CreateThread().
     */
    HANDLE g_hMuteThread = nullptr;
}

bool IsAd(const wstring&);
std::vector<wstring> GetSpotifyTitls(const std::vector<DWORD>&);
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
LRESULT CALLBACK WindowProcedure(
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
            // Thread needs to finish working
            if (g_StopEvent) {
                SetEvent(g_StopEvent);
            }

            // The program icon is removed from the tray
            Shell_NotifyIconW(
                NIM_DELETE, // action to be taken by this function
                &g_nid // pointer to NOTIFYICONDATA structure
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
DWORD WINAPI MuteThread([[maybe_unused]] LPVOID lpParam) {
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

int WINAPI WinMain(
    HINSTANCE hInstance,
    [[maybe_unused]] HINSTANCE hPrevInstance,
    [[maybe_unused]] LPSTR lpCmdLine,
    [[maybe_unused]] int nShowCmd
) {
    g_StopEvent = CreateEventW(
        nullptr, // the handle cannot be inherited by child processes
        1, // TRUE. Event object is a manual-reset
              // ResetEvent() to set the event state to nonsignaled
        0, // FALSE. The initial state of the event object is nonsignaled
        L"SpotifyMuterStopEvent" // event object name
    );
    if (!g_StopEvent) return 1;

    /*
    The window is invisible.
    Style, icon, curson, background, menu is no needed.
    Win32 architecture requires an invisible window to receive messages (e.g. clicks)
    */
    WNDCLASSEXW windowClassInfo = {};
    windowClassInfo.cbSize = sizeof(WNDCLASSEXW);
    windowClassInfo.lpfnWndProc = WindowProcedure; // window procedure
    windowClassInfo.hInstance = hInstance; // app instance handle
    windowClassInfo.lpszClassName = L"SpotifyMuterClass"; // window class name    
    RegisterClassExW(&windowClassInfo);

    g_hWndInvisible = CreateWindowExW(
        0, // extended window style
        L"SpotifyMuterClass", // window class name
        L"Spotify Muter", // window name
        0, // window style
        0, // initial window horizontal position
        0, // initial window vertical position
        0, // window width (device units)
        0, // window height (device units)
        nullptr, // handle to the parent or owner window
        nullptr, // handle to a menu. nullptr = The class menu to be used
        hInstance, // handle to the module instance to be associated with the window
        nullptr // additional data. nullptr = No needed
    );
    if (!g_hWndInvisible) return 1;

    g_nid.cbSize = sizeof(NOTIFYICONDATAW); // structure size (bytes)
    g_nid.hWnd = g_hWndInvisible; // handle to the window that receives notifications
    g_nid.uID = 1; // app-defined taskbar icon ID
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; /* flags
        NIF_MESSAGE = the  5th parameter member is valid
        NIF_ICON = the 6th parameter is valid
        NIF_TIP = the 7th parameter is valid
    */
    g_nid.uCallbackMessage = WM_TRAYICON; // app-defined message ID
    g_nid.hIcon = reinterpret_cast<HICON>(LoadImageW(
        nullptr, // system resource that is built into Windows itself
        reinterpret_cast<wchar_t*>(32512), /* image
            32512 == IDIAPPLICATION == Default application icon */
        1, // type. IMAGE_ICON
        0, // icon width (pixels)
        0, // icon height (pixels)
        LR_DEFAULTSIZE | LR_SHARED /* flags
            LR_DEFAULTSIZE = uses system metrics for width and height
                if 4th and 5th parameters is 0.
                Default 16×16 or 32×32 pixels
            LR_SHARED = if the icon has already been loaded
                (by another process or previously), there is no need to load it again.
                Using a reference (copy).

                No need to call DestroyIcon().
                Windows will clean it up when the app closes.

                Required for system icons.
        */
    )), // handle to the icon. Default system app icon,
    wcscpy_s(g_nid.szTip, L"Spotify Muter (Running)"); // tooltip

    // Add the app icon in the system tray ("^" on the task bar)
    Shell_NotifyIconW(
        NIM_ADD, // action to be taken
        &g_nid // pointer to the NOTIFYICONDATA structure
    );

    // Start a background thread to monitor Spotify state and mute it
    g_hMuteThread = CreateThread(
        nullptr, // security attributes.
            // nullptr = default security desctiptor, handle cannot be inherited
        0, // initial stack size. Default 1 MB
        MuteThread, // pointer to app-defined func to be executex by the thread.
        nullptr, // pointer to a variable to be passed to the thread. No additional data
        0, // flags
           // 0 = the thread runs immediately after creation
        nullptr // the thread ID is not returned
    );

    // Buffer to the message
    MSG message;
    /* Message loop
    
    Windows is an event-driven system.
    When the system wants to close an app, Windows receive the event,
    wraps it in an MSG and put into app's message queue.
    App must constantly check this queue, take messages from it and process them.
    Message loop keeps the program alive and allows it to interact with OC
    (respond to clicks on the tray icon)
    */
    while (GetMessageW(
        &message, // pointer to MSG structure
        nullptr, // collect messages for all windows, created by the current thread
                 // + thread messages
        0, // ignore messages with IDs < the specified one
        0 // ignore messages with IDs > the specified one
        /* 0 + 0 = app will collect all event (mouse movements, clicks,
            system commands, tray notifications) in chronological order.
        */
    )) {
        TranslateMessage(&message); // virtual-key message ==> charecters messages
        DispatchMessageW(&message); // dispatches message ==> WindowProcedure
    }

    // Memory deallocation at an exit

    // Waiting for the thread to finish
    if (g_hMuteThread) {
        WaitForSingleObject(g_hMuteThread, 2000);
        CloseHandle(g_hMuteThread);
    }
    if (g_StopEvent) {
        CloseHandle(g_StopEvent);
    }

    return static_cast<int>(message.wParam);
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
    // Interface MultiMedia Device (part of a Windows Core Audio APIs)
    IMMDeviceEnumerator* pDeviceEnumerator = nullptr;
    // Interface of MultiMedia Device. Audio endpoint device
    IMMDevice* pDevice = nullptr;
    // Interface. Enables a client to access the session and volume controls
    // for both cross-process and process-specific audio sessions
    IAudioSessionManager2* pAudioSessionManager = nullptr;
    // Interface. Enumerates audio sessions on an audio device
    IAudioSessionEnumerator* pAudioSessionEnumerator = nullptr;

    std::vector<ISimpleAudioVolume*> spotifyVolumes;
    std::vector<DWORD> spotifyPIds;

    HRESULT hResult = E_FAIL;

    int sessionCount = 0;
    
    // Creates a single COM class object (multimedia device enumerator)
    hResult = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), // class GUID to be created
        nullptr, // the object is not being created as part of an aggregate
        CLSCTX_INPROC_SERVER, /* class context: In-Process Server
            Code that controls the object is loaded into the process address space.
            Server is implemented as a DLL.
            CoCreateInstance() ==> LoadLibrary().

            This is the fastest way to work with COM objects:
                • no delays in data transfer (no IPC)
            */
        __uuidof(IMMDeviceEnumerator), // reference to the ID of the interface
            // Used to communicate with the object
        (void**)&pDeviceEnumerator // pointer where the reference to the create object will be written
    );
    if (FAILED(hResult)) goto Release;

    hResult = pDeviceEnumerator->GetDefaultAudioEndpoint(
        eRender, // the data-flow direction for the endpoint device
            // eRender – searches for a sound playback device (audio outputs)
        eMultimedia, // the role of the endpoint device
            // eMultimedia – suitable for multimedia and music
        &pDevice // pointer to a pointer variable to store the audio device address
    );
    if (FAILED(hResult)) goto Release;

    hResult = pDevice->Activate(
        __uuidof(IAudioSessionManager2), // interface GUID to be obtained from the device
        CLSCTX_INPROC_SERVER,  /* class context: In-Process Server
            Code that controls the object is loaded into the process address space.
            Server is implemented as a DLL.
            CoCreateInstance() ==> LoadLibrary().

            This is the fastest way to work with COM objects:
                • no delays in data transfer (no IPC)
            */
        nullptr, // activation parameters: not used for IAudioSessionManager2
        (void**)&pAudioSessionManager // pointer to a pointer variable to store the interface address
    );
    if (FAILED(hResult)) goto Release;

    hResult = pAudioSessionManager->GetSessionEnumerator(
        &pAudioSessionEnumerator
    );
    if (FAILED(hResult)) goto Release;

    hResult = pAudioSessionEnumerator->GetCount(&sessionCount);
    if (FAILED(hResult)) goto Release;

    for (int i = 0; i < sessionCount; i++) {
        // Interface. Enables to configure the control parameters
        // for an audio session and to monitor events in the session
        IAudioSessionControl* pAudioSessionControl = nullptr;
        // Interface. Used to get information about the audio session
        // IAudioSessionControl extended version
        IAudioSessionControl2* pAudioSessionControl2 = nullptr;

        HRESULT getSessionResult = pAudioSessionEnumerator->GetSession(
            i, // the session number
            &pAudioSessionControl // pointer to the IAudioSessionControl interface of the session object
        );
        if (FAILED(getSessionResult)) continue; // go to next session

        HRESULT queryInterfaceResult = pAudioSessionControl->QueryInterface(
            __uuidof(IAudioSessionControl2),
            (void**)&pAudioSessionControl2
        );
        if (SUCCEEDED(queryInterfaceResult)) {
            DWORD pid = 0;
            HRESULT getProcessIdResult = pAudioSessionControl2->GetProcessId(&pid);
            if (SUCCEEDED(getProcessIdResult) && pid != 0) {
                HANDLE hOpenProcess = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION, // the access to the process object
                        // PROCESS_QUERY_LIMITED_INFORMATION – to retrieve the executable image full name
                    FALSE, // the processes do not inherit this handle
                    pid // the local process ID to be opened
                );
                if (hOpenProcess) {
                    wchar_t pathBuffer[MAX_PATH];
                    DWORD bufferSize = MAX_PATH;
                    BOOL queryFullProcessImageNameResult = QueryFullProcessImageNameW(
                            hOpenProcess, // handle to the process
                            0, // flags. 0 – the name should use the Win32 path format
                            pathBuffer, // the path to the executable image
                            &bufferSize // the size of the path buffer 
                    );
                    if (queryFullProcessImageNameResult) {
                        if (wstring(pathBuffer).find(L"Spotify.exe") != wstring::npos) {
                            ISimpleAudioVolume* pSimpleAudioVolume = nullptr;
                            HRESULT queryInterfaceResult = pAudioSessionControl->QueryInterface(
                                __uuidof(ISimpleAudioVolume),
                                (void**)&pSimpleAudioVolume
                            );
                            if (SUCCEEDED(queryInterfaceResult)) {
                                spotifyVolumes.push_back(pSimpleAudioVolume); // for mute
                                spotifyPIds.push_back(pid); // for GetSpotifyTitles
                            }
                        }
                    }
                    CloseHandle(hOpenProcess);
                }
            }
            pAudioSessionControl2->Release();
        }
        pAudioSessionControl->Release();
    }

    // Is ad playing now? ==> Mute
    if (!spotifyPIds.empty()) {
        bool mute = false;
        std::vector <wstring> activeSpotifyTitles = GetSpotifyTitles(spotifyPIds);
        for (const wstring& title : activeSpotifyTitles) {
            if (!title.empty() && IsAd(title)) {
                mute = true;
                break;
            }
        }
        
        for (ISimpleAudioVolume* item : spotifyVolumes) {
            item->SetMute(
                mute,
                nullptr
            );
        }
    }
    
    // Memory deallocation
    for (ISimpleAudioVolume* item : spotifyVolumes) {
        if (item) { item->Release(); }
    }

    Release:
        if (pAudioSessionEnumerator) {
            pAudioSessionEnumerator->Release();
            pAudioSessionEnumerator = nullptr;
        }
        if (pAudioSessionManager) {
            pAudioSessionManager->Release();
            pAudioSessionManager = nullptr;
        }
        if (pDevice) {
            pDevice->Release();
            pDevice = nullptr;
        }
        if (pDeviceEnumerator) {
            pDeviceEnumerator->Release();
            pDeviceEnumerator = nullptr;
        }
        return;
}
