# Spotify Advertisement Muter for Windows

A Windows utility for automatically mute Spotify when ads are playing.

## How it works?

The program runs in the background and uses the **Windows Audio Session API** to control the sound of individual processes.

1. The program checks the window titles of all `Spotify.exe` processes every 500ms;
2. If an ad is playing (Spotify changes the window title), the program records this;
3. The sound for Spotify is muted at the system mixer level until the end of the commercial;
4. The program adds an icon the system tray (the `^` icon in the taskbar) for management and runs in the background.

## Features
- **minimalism**: no windows (visible), works invisibly in the notification area;
- **low resource consumption**: uses a separate thread for verification that doesn't block the system;
- **native WinAPI**: wrote on native C++ without heavy libraries.

## Manage

- **tray icon**: the program displays an icon in the tray;
- **context menu**: LMB or RMB click on the icon ==> `Exit of Muter` to stop the program.

## Build

The project can be build in VS Code using two different methods. Choose the one.

### Method 1: using CMake and Clangd

Standard and most robust method. It uses the native `CMakeLists.txt` to handle dependencies, warnings, and optimisation.

#### Preparation
1. Install **GCC (MinGW-w64)** complier and **CMake**;
2. Install the following extensions in VS Code:
    - **Clangd** for fast autocompletion and error highlighting;
    - **CMake Tools** for build management;
    - **C/C++** (from Microsoft) for debugging (optional).

#### Compiling and running
1. Clone and open the project in VS Code
```bash
   git clone https://github.com/iMer6/SpotifyMuter.git;
```
2. Press `Ctrl + Shift + P` (command palette);
3. Run `CMake: Configure`;
4. When prompted, select the GCC (MinGW) complier kit;
5. Press `F7` or the Build button in the bottom panel of VS Code. The executable `SpotifyMuter.exe` file will appear in the `build` folder;
6. Run the command `.\build\SpotifyMuter.exe` in a terminal in the root of the project. The utility will run in the background, its icon can be seen in the system tray (the `^` icon in the taskbar, next to the selected language).

### Method 2: using Microsoft C/C++ extension

You can compile the single source file directly using Microsoft's official C/C++ extension and a local build task.

#### Preparation
1. Install **GCC (MinGW-w64)** compiler;
2. Install the **C/C++** extension by Microsoft in VS Code.

#### Compiling and running
1. Clone and open the project foled in VS Code
```bash
   git clone https://github.com/iMer6/SpotifyMuter.git;
```
2. Create a folder named `.vscode` in the root directory;
3. In the created folder create a file named `tasks.json`;
4. Paste the following configuration into `tasks.json` file

```
{
    "version": "2.0.0",
    "tasks": [
        {
            "type": "cppbuild",
            "label": "C/C++: g++.exe build active file (SpotifyMuter)",
            "command": "g++.exe",
            "args": [
                "-std=c++17",
                "${file}",
                "-o",
                "${fileDirname}\\SpotifyMuter.exe",
                
                // Warnings
                "-Wall",
                "-Wextra",
                "-Wconversion",
                "-Wdouble-promotion",
                "-Wpedantic",
                "-Wshadow",
                "-Wformat=2",
                "-fdiagnostics-color=always",

                // Optimisation and compression
                "-Os",
                "-s",

                // Hide the Windows console window
                "-mwindows",

                // Native Win32 libraries
                "-luser32",
                "-lshell32",
                "-lole32",
                "-luuid"
            ],
            "options": {
                "cwd": "${fileDirname}"
            },
            "problemMatcher": [
                "$gcc"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "detail": "Compile project."
        }
    ]
}
```

5. Open your main source code file (SpotifyMuter.cpp);
6. Press `Ctrl + Shift + B` to trigger the compilation;
7. Run the command `.\SpotifyMuter.exe` in a terminal in the root of the project. The utility will run in the background, its icon can be seen in the system tray (the `^` icon in the taskbar, next to the selected language).

## Dependencies

The project is developed using native Windows tools without heavy third-party libraries.
To compile it you need:

- **Operating System**: Windows 10 or Windows 11;
- **Compiler**: GCC (MinGW-w64) with C++17 support (or newer);
- **Tools**: CMake 3.10 or newer.