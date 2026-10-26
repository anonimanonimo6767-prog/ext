# v67suite — Roblox External Cheat

External overlay. No injection. Reads memory via `ReadProcessMemory`.

---

## Features

| Feature | Default Key | Notes |
|---------|-------------|-------|
| Toggle Menu | INSERT | rebindable in-menu |
| Toggle ESP | F1 | rebindable |
| Toggle Aimbot | F2 | rebindable |
| Hold to Aim | RMB | rebindable |

### ESP
- Corner box (not full rect — cleaner look)
- Health bar with numeric readout
- Player names
- Skeleton (2-bone head→root)
- Tracers from screen bottom
- Custom RGBA color picker

### Aimbot
- FOV circle preview on overlay
- Smooth factor slider (0.01 = instant, 1.0 = sluggish)
- Bone target: Head or Hip
- Activates on hold key (RMB default)
- Uses `SendInput` absolute move — works on all monitors

### Offset Dumper
Runs in background thread on launch:
1. Snapshots committed memory pages
2. Scans for `"DataModel\0"` string
3. Back-traces pointer to Instance header
4. Walks `Children` arrays to find: Workspace, Camera, Players, LocalPlayer
5. All found addresses shown live in **Info** tab
6. Retries every 3 seconds until found

---

## Build (GitHub Actions)

1. Push this repo to GitHub
2. Actions → **Build v67suite** → Run workflow
3. Download `v67suite-x64` artifact → `v67suite.exe`

### Manual build (local MSVC)
```
ilammy/msvc-dev-cmd or Developer Command Prompt (x64)

cl.exe /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
  /I. /Iimgui /Iimgui/backends ^
  cheat.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp ^
  imgui/imgui_widgets.cpp imgui/backends/imgui_impl_dx11.cpp ^
  imgui/backends/imgui_impl_win32.cpp ^
  /Fe:v67suite.exe ^
  /link d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib user32.lib gdi32.lib winhttp.lib ^
  /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup
```

---

## Usage
1. Launch Roblox, join any game
2. Run `v67suite.exe` as Administrator
3. Waits automatically for Roblox process + window
4. Offset dump runs in background (check Info tab)
5. INSERT = toggle menu
6. All keybinds rebindable via Keybinds tab (click button → press key)

---

## Structure

```
v67suite/
├── cheat.cpp                     ← everything
├── CMakeLists.txt
├── README.md
└── .github/
    └── workflows/
        └── build.yml
```
ImGui downloaded automatically by CI. For local build, clone
https://github.com/ocornut/imgui tag v1.90.4 into `./imgui/`

---

## Offsets tuning

All offsets are in the `Offsets` struct at the top of `cheat.cpp`.
The dumper auto-finds: DataModel, Workspace, Camera, Players, LocalPlayer.
Static field offsets (CFrame, Health, Children, Name, etc.) are constants
— update them if Roblox patches their layout.

```cpp
uintptr_t oName        = 0x68;
uintptr_t oChildren    = 0x70;
uintptr_t oCFrame      = 0x110;
uintptr_t oHealth      = 0x148;
uintptr_t oMaxHealth   = 0x150;
uintptr_t oViewMatrix  = 0x1F8;
```
