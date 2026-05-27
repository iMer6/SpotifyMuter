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

using namespace std;

// constexpr – constant expression.
// A constexpr variable value must be known and computable at compile-time.
// const can be evaluate in runtime or compile-time
//
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
 * @note If user clicked on bytton "Exit of Muter" in tray menu,
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
HWND hWndInvisible = NULL;

bool IsAd(const wstring& t);

wstring GetActiveTitle(DWORD pid);

void RefreshAndMute();

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP) {
            POINT curPoint;
            GetCursorPos(&curPoint);
            HMENU hMenu = CreatePopupMenu();
            InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Exit of Muter");
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, curPoint.x, curPoint.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
    } else if (message == WM_COMMAND) {
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            Shell_NotifyIconW(NIM_DELETE, &nid);
            exit(0);
        }
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
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
        vector<DWORD> spotifyPids;

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