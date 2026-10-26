# POPSoT Recomp on Windows

Version 0.2.0 bundles [the supplied Windows defaults](default-options.ini):
D3D11, unlimited FPS, VSync/glow/logs off, 720p (150%), post-processing Low
and look sensitivity 200%. A missing INI is created before the launcher;
existing preferences are retained. The release ZIP contains no separate INI;
the executable creates it on first launch from its embedded defaults.
No ISO or game-installation path is bundled.
It also includes the effect-quality and
logging repairs recorded in [validation](../../docs/VALIDATION.md).

The preceding 0.1.1 release includes the shared timer lock reduction and APU/
IRQL and Vulkan changes. Android display/lifecycle controls are host-specific;
Windows retains its full-size compositor. See [validation](../../docs/VALIDATION.md).

Vulkan specialization update (2026-10-20): the optional native Vulkan
renderer shares Android's cached constant combiner programs. Both target
builds and all 8 GPU/support tests pass; a 20-second retail Vulkan startup
check reaches the title. Initial shader compilation can still stall. See
[the study](../../docs/VULKAN-PERFORMANCE.md).

The Windows build: a 64-bit executable per target, Direct3D 11 (default),
experimental native D3D12/Vulkan for both targets, XInput and XAudio2.

The Android correction/optimization audit is in
[PLATFORM-PARITY.md](../../docs/PLATFORM-PARITY.md). Shared APU/IRQL fixes
and native renderer optimizations also compile into these executables;
Android-specific host changes and intentional backend differences are
listed there.

## Play: POPSoT Launcher

`popsot-launcher.exe` (beside `sot-xbox-retail.exe` in `bin\`, source in
[launcher/](../../launcher), Qt 6 Widgets) is the way in, after the
FuturamaPC launcher: a plain window in the system's light or dark Windows
style, no animation, no links. It shows the PC version's icon (see below),
the installation, the settings of `popsot-options.ini` on tabs (Game,
Graphics, Audio, Controls, Advanced), Save and Play, in German or English.

First start, or "Install from ISO...": a setup wizard asks for

1. the image of the **USA disc** (NTSC retail, required): its `default.xbe`
   must have the SHA-256 the port is built for (753C58B1...); another
   revision of the game is named as such;
2. the image of the **European disc** (PAL, optional): its German speech
   (`SOUND/SOUNDX4`) replaces the English set in `SOUND/SOUNDX` and
   `soundx0` (the English archives are kept in `SOUND/original-english`),
   and its five German bonus films (`CINE_90n4`) go to `VIDEO`. Without it
   the speech stays English; the Game tab can add it later or restore the
   English speech. Texts and film subtitles in German need only the USA
   disc;
3. the game folder (default: `Game` beside the launcher; in a source
   checkout `Assets\xbox-retail` when the game is already there).

The launcher reads the images itself (XDVDFS: bare XISO and redump layouts,
`launcher/src/xiso.cpp`), copies every file except the dashboard update,
and remembers the folder as `[Launcher] GameDir`. Play saves the settings and
starts `sot-xbox-retail.exe` with `POPSOT_ASSET_DIR` set to that folder.

Without the launcher: `sot-xbox-retail.exe` finds the game through
`POPSOT_ASSET_DIR`, `[Launcher] GameDir`, the files beside it, or a source
checkout's `..\Assets\xbox-retail`; without game files it starts the
launcher beside it, or says that it needs it. `tools/run.ps1` is unchanged.

Command line (tests, scripts): `popsot-launcher --check-iso <image>` and
`popsot-launcher --install <USA image> <European image or -> <folder>`.

Build: `.\tools\build-launcher.ps1` (Qt 6.8 for MSVC 64 bit, by default
`D:\Qt\6.8.3\msvc2022_64`, or `-QtDir`); it puts the Qt runtime beside the
launcher with windeployqt. The icon of both executables is the PC
version's: `python tools\extract-icon.py "<PC game folder>\POP.EXE"
bin\pop.ico` writes it from the player's own copy (not in the repository);
without it the builds use the neutral `port/windows/res/app.ico`.

The settings file was `popsot-options.ini`; the first start moves it to
`popsot-options.ini`.

The app starts full screen with graphics, audio, keyboard and controller
enabled. Space or controller A skips a Bink video, including videos whose
original skip permission is disabled. F11 changes window mode; Alt+F4 exits.
(The F5 info panel is gone from Windows, 2026-10-26: F6 shows the same and more.)
F9 (this fork) traces the next frame into the error log and, with the
Direct3D 11 renderer, writes it and every smaller render target as BMPs at
host resolution to `logs/capture/` beside the EXE (`c_flip<n>_after.bmp`
and one file per target); press it where something looks wrong.
Internal resolution now goes up to 2160p (450%).
Android's FPS touch button hides while its panel is open and keeps the same
touch area to close it.
F6 opens the translucent diagnostics panel. It leaves the game
playable. It shows, twice a second: frame rate and frame time with the
current graphics settings; executor time (interpreter, draw calls), present
and title time; draws, triangles, vertices and texture uploads per frame;
batch reuse hits and misses; guest heap and contiguous memory; the host
process's memory and CPU; active APU voices (2D, 3D, multipass), streaming
voices and voices held by a DirectSound lock; DPCs queued and run per second;
threads at raised IRQL and IRQL transitions per second; the current world;
and the last lines of this run's log. Ctrl+Shift+C or the Copy button puts
all of it, with the last 80 log lines, on the clipboard.

F7 opens/closes the restored `Port Options` overlay (demo and retail);
Esc or its X button also closes it. Expand Graphics or Controls to edit
settings. Double-click a binding to change it, right-click to clear it;
scroll to reach the remaining actions. The overlay captures game input
while open. F6 remains assigned to diagnostics; while options are open,
the options surface takes display priority. The same settings remain in
the game's main menu under `Port Options`:
 Graphics has the FPS limiter (30/60/120/None; 60 by default), VSync, post-processing
and internal rendering resolution (240p, 360p, 480p, 720p, 1080p, 1440p);
each choice cycles its value. Resolution changes from either menu or F7 preserve the current
fullscreen/window state; only F11 toggles it, including while F7 is open.
The INI key is `[Graphics] InternalResolutionPercent` (50/75/100/150/225/300,
default 150). The old `Resolution` window-size index is no longer used.
The labels assume a 480-line guest target; aspect ratio remains unchanged.
Movies and options retain display resolution; auxiliary effects follow effect quality.
 Controls has **Look sensitivity** (25–200% in steps of 25, default 200%),
also in F7, and the keyboard and joystick
bindings in three pages each; choosing a binding waits for the next key,
mouse button or joystick input (Esc cancels), and `Reset all bindings`
restores the defaults. A joystick binding is taken only after every pad
input has been released, so the press that opened the row is never bound;
from the game's menu, Back cancels it and an untouched one lapses after
8 seconds (pressing B to leave the page used to bind B). The game's own
timing can still limit FPS. Settings are saved in `popsot-options.ini`
beside the executable, except in runs with `RECOMP_PAD_SCRIPT` or
`RECOMP_PAD_LIVE`: their scripted presses can reach these pages, so their
changes last only for that run.
Look sensitivity uses `[Controls] LookSensitivityPercent` and scales only
the right-stick camera values after the original deadzone. It applies to
keyboard/mouse and controller look, including full deflection above 100%;
the Android counterpart applies the same setting to touch look too.
F7 Graphics selects `Direct3D 11`, `D3D12 (native)` or `Vulkan (native)`
after restarting the game. The two native modes use SDL GPU's D3D12/Vulkan
command encoders, with DXIL/SPIR-V shaders, native GPU resources and GPU draws.
They do not create a D3D11 device or use D3D11On12/DXVK. Supported NV2A vertex
programs run on the GPU; unsupported programs retain the CPU interpreter.

Run `./tools/setup-native-gpu.ps1` once before building, and again when
upgrading from a native build without retail queries. It downloads pinned,
SHA-256-checked SDL3/DXC SDKs and SDL3 sources, applies the published
`port/third_party/sdl-gpu-query/` additions, then builds SDL3.dll with native
D3D12/Vulkan occlusion queries. Visual Studio and CMake are build requirements.
The player executable needs that SDL3.dll, its licence and `SDL3-POPSOT.txt`,
which identifies this altered SDL build. It needs no compiler or SDK.
Host shaders are embedded at build time; DXC is not a runtime requirement.
`tools/setup-vulkan.ps1` forwards to this installer. Existing DXVK files under
`renderers/vulkan/` are ignored and no longer packaged.

**Native backends are experimental on both demo and retail.** Retail now
uses actual hardware visibility queries, read asynchronously after submission
fences, with immutable reports that survive counter clears. An older SDL DLL
without the query extension or a device without the required query capability
produces an explicit D3D11 fallback. Demo does not enable retail's report rules.
Retail white light-map borders and forming sand-tank alignment share the
Android corrections and pass host GPU readbacks; broader gameplay
validation remains pending. Stencil support is still incomplete, as on the
D3D11 renderer. NV2A vertex programs run on the GPU in `vs_uber`, a shader
compiled offline that interprets the program (no shader compiler at run
time); programs that write constants or read more than eight inputs still use
the CPU interpreter. `POPSOT_NATIVE_CPU_VSH=1` puts every program back on the
CPU, for comparison. `POPSOT_ASYNC_PIPELINES=1` compiles new D3D12/Vulkan
pipelines on background threads and skips their draws until ready, as
Android does by default (see port/android/README.md); off on Windows, where
no compile stall has been measured.

`POPSOT_RENDERER=d3d11`, `d3d12` or `vulkan` overrides the saved choice for one
run. D3D11 stays the default. The panel reports the actual active backend,
separately from the saved choice. Missing SDL3.dll, unsupported drivers or
initialization failure log a D3D11 fallback. `POPSOT_GPU_DEBUG=1` enables SDL's
native GPU validation where available. Logs remain beside the executable.
Save data stays in
that game folder, so keep it writable.

After building, `./tools/package.ps1 -Target xbox-demo` (or `xbox-retail`)
copies the executable and your game files into `bin/portable/<target>/`.
This folder needs no development tools. It contains your disc files: do not
redistribute it. Packaging again preserves existing save folders.

## Build requirements

- Windows 10/11 x64, a Direct3D 11 GPU
- Visual Studio with the C++ desktop workload (the scripts default to the
  `Visual Studio 18 2026` generator; pass `-Generator` for another)
- CMake 3.20+
- Python 3.10+ with `capstone` (`pip install capstone`)

The game files go in `Assets/<target>/` first; see "Supply your game files"
in the [README](../../README.md#supply-your-game-files).

## Build

```powershell
.\tools\recomp.ps1 -Target xbox-demo      # analyse, lift and build (about 12 minutes)
.\tools\recomp.ps1 -Target xbox-retail
.\tools\recomp.ps1 -Target xbox-retail -BuildOnly   # after editing port/ (host, overrides, runtime)
```

Both targets share the runtime in `port/third_party/xboxrecomp`; each has
its own build tree, `build/<target>`, which compiles it for that target
(`POPSOT_RUNTIME_DEMO` or `POPSOT_RUNTIME_RETAIL`). `-BuildOnly` rebuilds only
the target named. The lifted C goes to `source/<target>/gen` (ignored by
Git). The executables
land in `bin/` as `sot-xbox-demo.exe` and `sot-xbox-retail.exe`, with the
POPSoT icon (`port/windows/res`).

The build is 16:9 (Hor+) by default. To build the original 4:3 instead,
configure with an empty `POPSOT_ASPECT`; `run.ps1 -Aspect` (or
`RECOMP_HW_ASPECT`) overrides either at run time:

```powershell
cmake -S . -B build/xbox-demo -DPOPSOT_TARGET=xbox-demo -DPOPSOT_ASPECT=""   # then recomp.ps1 -Target xbox-demo -BuildOnly
.\tools\run.ps1 -Pad -Aspect 4:3          # a 16:9 build shown 4:3
```

## Run

```powershell
.\tools\run.ps1                           # the demo, full screen
.\tools\run.ps1 -Windowed                 # in a window
.\tools\run.ps1 -Target xbox-retail
```

Alt+F4 or the window's close button exits the game process, including its
rendering and audio threads.

Double-clicking also works directly from `bin/`: select the game folder in
the picker when the files are not beside the executable.
`POPSOT_ASSET_DIR` explicitly overrides the game folder. The script remains
useful for diagnostic settings and captures and selects `Assets/<target>/`.
`.\tools\shortcut.ps1` puts a desktop shortcut, "POPSoT - Xbox demo", that
runs `run.ps1` with a double click, full screen (`-Target xbox-retail` for
the retail build, `-Arguments '-Windowed'` for extra switches).

The game always has a controller on port 0, without the emulated USB bus:
an XInput pad (an Xbox controller, on any of Windows' four slots), else any
other joystick or pad Windows sees, with the keyboard and mouse merged on
top while the game window has the focus (`RECOMP_KEYBOARD=0` turns them
off):

The keyboard follows the PC version's default layout (its Controls -
Keyboard screen), each action on the Xbox button that does it:

| Action | Keys | Xbox pad |
| --- | --- | --- |
| Move | W A S D, arrow keys | Left stick (arrows also the d-pad) |
| Walk toggle | Left Shift, Keypad . | Left stick at walking deflection |
| Jump | Space, Right Ctrl | A |
| Special action | Right mouse button | Right trigger |
| Sword attack | Left mouse button | X |
| Use dagger | E, middle mouse button | Y |
| Cancel | C, Keypad 1 | B |
| Rewind | R, Right Shift | Left trigger |
| Camera look | F, Delete | White |
| Alternate view | Q, End | Black |
| Reset camera | X | Right stick click |
| Start | Esc, Enter | Start |
| (back) | Backspace | Back |
| Camera | Mouse movement | Right stick |

The camera-look, alternate-view and reset-camera buttons are the ones left
on the pad once the others are placed; that the game uses them for those is
still to be checked in play. `-Set @{RECOMP_MOUSE_SPEED='60'}` (1-100,
default 43 as on the PC) scales the mouse, `RECOMP_WALK_DEFLECTION`
(default 12000 of 32767) is how far walking pushes the stick. The pointer
is hidden over the picture; the menus do not follow it. A joystick that is not an XInput device
uses buttons 1-4 as A B X Y, 5/6 white/black, 7/8 the triggers, 9 Back,
10 Start, its hat as the d-pad. `-UsbPad` brings back the old emulated USB
pad, for comparison.

`-Aspect 16:9` (or any `w:h`; 16:9 is the default build's) widens the
picture Hor+: more scene at the sides, the same vertical view, the 2D at its
original 4:3 size; the in-game HUD moves out to the screen's sides
(`-Set @{RECOMP_HW_HUD_EDGE='0'}` keeps it in the 4:3 frame). The game opens as a borderless window over the whole
monitor; `-Windowed` opens a window instead, and F11 switches
between the two while it runs. `-NoVsync` stops waiting for the
display, `-NoAudio` leaves the sound hardware out.

Diagnostics go to `logs/<target>-err.txt`; previous runs are kept in
`logs/history/` so relaunching does not erase a crash. Retail uses separate
virtual reservations and committed pages by default; developers can compare
the previous allocator with `-Set @{RECOMP_EXT_VMA='0'}`. That comparison
path can exhaust the guest heap during gameplay.

Useful switches: `-Software` (CPU
rasteriser, for comparison), `-Seconds N`, `-SnapshotEvery S` (frame dumps in
`logs/fb`), `-Set @{RECOMP_HW_SCALE='3'}` (internal resolution multiple),
`-Set @{RECOMP_APU_WAV='out.wav'}` (record the sound). More in
[docs/RECOMP.md](../../docs/RECOMP.md).

### Language, HD menu font and buttons (retail)

`[Language] Text=german` and `Movies=german` are the Windows defaults:
- The menus and texts come from the German text streams that the USA disc
  already holds.
- The cinematics play their German sound track.
- The loading and logo films are the German ones.
- The bonus films use their German-subtitled versions once
  `tools/german-speech.py --films` has copied them from a European disc.

`english` restores the original for either.

Texture packs replace a guest texture by content, at any size: `<key>.tga` in
`textures/` beside the executable (`POPSOT_TEXTURE_PACK` overrides the folder).
`tools/hd-ui.py` builds the menu font and button atlases at 8x:
- The font is rendered from the disc's own vector font
  (`MEDIA/AtlantisMF.ttf`) into the measured glyph rectangles.
- The buttons are drawn as SVG (`tools/hd-ui/buttons.svg`).
- The controller drawing is traced from your own atlas.

To build them:
1. Run the game once with `POPSOT_TEXTURE_DUMP=1` up to the profile screen.
   This writes `textures/dump/`.
2. Run:

       python tools/hd-ui.py --game Assets/xbox-retail --pack bin/textures

For the HUD, `--hud-pack` takes it from the PC version's PoP Texture
Studio pack (`Evgesha.JK`, e.g. in the GOG folder). Run a level once with
the dump on (`POPSOT_TEXTURE_DUMP=1`, `POPSOT_START_LEVEL=6101_couloir`, with
`POPSOT_CHEATS=sand` for the full tanks), then:

       python tools/hd-ui.py --game Assets/xbox-retail --pack bin/textures --hud-pack "F:/GOG Games/Prince of Persia - The Sands of Time/Evgesha.JK"

### Level select (retail)

The main menu has the developers' "Special Load Map" again ("Spezielle
Karte laden"). It lists one entry per area of the game and the hidden
worlds (the original Prince of Persia bonus and the credits map) and starts
the chosen one directly, like popsot-fixes' Special Load on PC. The
developers' test maps (WhiteRoom, magic corridor, magic fountain) are left
out: they have no start and do not play. It is meant for testing: the Prince keeps whatever the
menu had (sword, sand tanks), not what the story would have given him by
then. `POPSOT_HIDDEN_MENUS=0` hides it together with "Port Options".

### Free camera (retail)

Back or F8 detaches the camera, as in popsot-fixes: left stick / the
movement keys fly, right stick / the look keys turn, LT / RT or Q / E move
down / up, Black or Shift is faster, White or Ctrl slower, Y or P freezes
the world while the camera keeps moving. The game gets no input but Start
while it is on (a menu page keeps its input). The Debug page switches it
too. `POPSOT_FREECAM=0` disables it.

### Cheats (retail)

Like popsot-fixes on PC, rebuilt from the PS2 prototype's cheat mode:
invulnerable, infinite sand and one-hit kills (Farah is never killed by a
hit), and the prototype's two spawner switches, which retail still tests
but never sets: "No enemies" (spawners drop their spawn requests) and
"Endless enemies" (a spawner that has sent all its enemies starts again).
"Port Options" is also in the pause menu now, so the "Cheats" page
is at hand in play; its last entry gives the next of the four swords once
the menu is closed. In play, Ctrl+F1, Ctrl+F2 and Ctrl+F3 switch the first
three and Ctrl+F4 gives the next sword. They are not saved and start off.
For tests, `POPSOT_CHEATS=invulnerable,sand,onehit,nospawn,endless` switches
them on at start and `POPSOT_START_LEVEL=6101_couloir` (a room of the level
select) starts that level by itself once the main menu is up.

### German hint text (retail)

With German text, the hints say "Drückt" where the German data has
"Ziehen" ("Ziehen X, um das Schwert zu ziehen", "Ihr ziehenet A zu früh"),
and the button symbols lose the brackets fifteen hints put around them.
`POPSOT_TEXT_FIXES=0` shows the original text.

### Sound: 5.1 and room reverb

"Port Options" -> "Sound" (Windows): "Speakers" Stereo (default) or 5.1,
used from the next start, and "Room reverb" On (default) / Off, at once.
Saved in `popsot-options.ini` (`[Audio] Speakers=stereo|5.1`, `Reverb=1|0`);
`POPSOT_SPEAKERS` and `POPSOT_REVERB` override them for one run. The reverb
is the Xbox's I3DL2 room reverb: the game sends a copy of its sounds to it
and sets the room (the DSP that ran it on the Xbox is not emulated; the
XAudio2 reverb takes its place with the game's own settings). 5.1 sends the
game's six speaker channels to a 5.1 device; on a stereo device Windows
gets them mixed down. `RECOMP_APU_WAV=<file>` records what is played, with
every channel and the reverb send last.

### Main-menu camera (retail)

The main menu's 3D set was built for 4:3 and runs out at the edges at 16:9.
As in popsot-fixes on PC, the camera is moved: by default to the menu
picture and flight end the PC port has in its `popfix.ini`, so the scene
starts like there (same world, same coordinates; the picture can still
differ slightly). In the main menu the free camera (Back or F8) places your
own: X stores the menu picture, B the end of the New Game flight, both in
`popsot-options.ini` (`[Menu] CameraStart` / `CameraEnd`, `x y z yaw
pitch`; `CameraMode=own`). New Game flies from the picture to the end at
the game's pace. "Port Options" -> "Debug" -> "Menu camera" switches PC
port, original and own. `[Menu] CameraForward` / `CameraUp` / `CameraSide`
move the camera when a mode has no picture (defaults 3.5 / 6.25 / 0);
`POPSOT_MENU_CAMERA=0` (or `pc`, `own`, `original`) overrides the mode for
one run.

### Prototype page (retail)

"Port Options" -> "Prototype": switches the PS2 prototype used that
retail still reads but never sets. "Life regeneration" gives one life point
per second; "Hide interface" hides the life bar and the hints. Not saved;
`POPSOT_PROTO=regen,nohud` switches them on at start.

### Debug page (retail)
### Debug page (retail)

"Port Options" -> "Debug", in the main and the pause menu: the free camera, the world freeze, the game speed and the main-menu
camera (100 %, 50 %,
25 %, through a clock factor the game reads but never writes; the scripts'
own slow motion still works on top). Not saved; `POPSOT_GAME_SPEED=50`
starts at that speed and `POPSOT_TIMER_DIAG=1` logs the game clock against
the wall clock. The level select stays in the main menu only: started from
the pause menu, a map change ended on the "Spielende" page.

### Soft focus strength

The menu entry formerly called texture glow switches the title's soft-focus
overlay (a blurred 160x120 copy of the frame laid over it). Port Options >
Graphics > Soft focus cycles Off, 25 %, 50 %, 75 % and 100 % (the Xbox's own
strength); the INI keeps `[Graphics] TextureGlowEffect` as the switch and
`SoftFocusStrength=1..100` as the strength (`POPSOT_SOFT_FOCUS_STRENGTH`
overrides it). Direct3D 11 only for now.

### Post processing quality and texture glow

`Texture glow effect` uses `[Graphics] TextureGlowEffect=1` by default on
Windows (this fork, 2026-10-22). `Post processing` is separate:
`PostProcessingQuality=0/1/2/3/4` selects Off/Low/Mid/High/Ultra (Windows
default Ultra). Ultra renders every auxiliary buffer (effect chains, frame
copies, shadow maps) at the internal resolution; filter taps keep their
original spacing, so the look is the original one, only finer. Low and Mid now budget auxiliary buffers,
including projected-shadow maps, at 50% and 75% width/height. High retains
original effect resolution. Off omits recognized blur/bloom chains, their
fullscreen consumers, the final glow composite and recognized projected
shadows; other auxiliary buffers use 25% while retaining their materials.
Display/history buffers and tiny bloom levels are protected. The user confirms
the option works (2026-10-20). See the shared [effect study](../../docs/POST-PROCESSING.md)
for exact coverage and the remaining water/motion-blur identification work.

F11 synchronizes the adopted SDL window's border and resize flags with Win32
when switching native Vulkan/D3D12 between fullscreen and a movable, resizable
window. Console logs read the existing session files live, including startup,
without redirecting game output. The host supplies the exact file paths it
opened; external disk redirects are resolved from the actual stdout/stderr
FILE descriptors. A visible console header reports each source or an open
error. Existing INI settings are retained.

The F7-only `Console logs` switch is saved as `[Diagnostics] ConsoleLogs=1`.
Error messages are red; warnings are yellow. The original file logs remain
in `logs/<target>-err.txt` and `logs/<target>-out.txt`.

The in-game Port Options post-processing selector offers Low/Mid/High/Ultra.
Off remains available through `[Graphics] PostProcessingQuality=0` in the INI
and, on Windows, through F7. An existing Off setting is shown as Custom in
the in-game menu and is preserved until the quality selector is changed.

Vulkan uses dedicated translated vertex programs and optimized complex fragment
programs for demo and retail. The host compiler (glslang/SPIRV-Tools) is built
from pinned sources with `tools/setup-shader-compiler.ps1`, also called by the
platform dependency setup script. Players need no separate shader compiler.
Optimized modules are stored in `shadercache/`; Vulkan's driver pipeline cache
is separate. Compilation uses background workers by default on both hosts,
with reference pipelines available during compilation. First-use draw skips
can still occur until a pipeline is ready. `POPSOT_NATIVE_TRANSLATE=0` and
`POPSOT_NATIVE_SPECIALIZE=0` are developer comparison switches;
`POPSOT_ASYNC_PIPELINES=0` forces synchronous compilation. See
[the shader study](../../docs/VULKAN-PERFORMANCE.md) and
[validation](../../docs/VALIDATION.md) for scope and observed results.
