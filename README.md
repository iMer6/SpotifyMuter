# Spotify Advertisement Muter (WinAPI)

A Windows utility that automatically mutes Spotify ads.

## How it works?

Програма працює у фоновому режимі та використовує **Windows Audio Session API (WASAPI)** для керування звуком окремих процесів.

1. Програма кожні 700мс перевіряє заголовки вікон усіх процесів `Spotify.exe`;
2. Якщо грає реклама (змінена назва вікна) програма це фіксує;
3. Звук для Spotify вимикається на рівні системного мікшера до закінчення реклами;
4. Програма працює на фоні та додає іконку в системний трей для керування.

## Features
- **minimalism**: немає вікон, працює непомітно в області сповіщень;
- **low resource consumption**: використовує окремий потік (`MuteThread`) для перевірки, що не блокує систему;
- **native WinAPI**: wrote on native C++ without heavy libraries.

## Manage
- **tray icon**: програма відображає іконку в треї
- **context menu**: RMB on the icon -> `Exit of Muter` to stop the program.

## Build

Для compile you знадобиться **Visual Studio Code**

''' Terminal
g++ spotify_muter.cpp -o Spotify_Muter -mwindows -lole32 -luser32 -lshell32
'''

## Dependencies
The program uses the following system libraries:
- `ole32.lib` (for COM interfaces)
- `user32.lib` (for work with windows and tray)
- `shell32.lib` (for icon in tray)

