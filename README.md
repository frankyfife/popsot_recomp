# POPSoT Recomp

**Prince of Persia: The Sands of Time, the Xbox version, as a native Windows
game.** A continuation of FulGerNet's static recompilation of the game, with
its own launcher, fixes and options.

## What it does better, especially on Windows

| | |
| --- | --- |
| **The Xbox game, natively** | The console version recompiled to x64 code, not the 2003 PC port: its water that moves on its own, its soft focus and glow, its menus. |
| **Up to 2160p, 16:9** | Internal resolution up to 4.5x (2160p), widescreen Hor+ with the interface kept at 4:3, effects at full resolution on Ultra without shimmer, the soft focus adjustable from off to the Xbox's 100 %. |
| **Sharp interface** | Menu font and button prompts rendered again at 8x from the game's own font, fitted to the original glyphs. |
| **5.1 surround and room reverb** | Discrete 5.1 through XAudio2 and the Xbox's own I3DL2 room reverb per area, without dropouts. |
| **German, complete** | German texts and film subtitles from the USA disc; German speech and the German bonus films from your European disc, added by the launcher. Hint texts with correct German verbs and button prompts. |
| **Launcher with installation** | `popsot-launcher.exe` installs the game once from your own disc images (USA required, European optional), checks the executable by its hash and needs no other tools; settings on tabs, then Play. |
| **Port Options in the game** | Graphics, sound, full keyboard and pad rebinding, a level select for every area plus the POP1 bonus and the credits, cheats (invulnerability, infinite sand, one-hit kills, no portal enemies, next sword), the PS2 prototype's life regeneration and HUD switch, and a debug page (free camera, world freeze, game speed, main-menu camera). |
| **Main-menu camera** | Place the title-screen view yourself with the free camera (or use the PC version's), so the wide picture shows no empty set edges. |
| **Renderers and pacing** | Direct3D 11 by default, Direct3D 12 and Vulkan experimental; frame limit 30/60/120/unlimited, VSync. |
| **Diagnostics** | F6 panel (frame time, renderer, audio); F9 captures a frame with all its render targets for bug reports. |

Your own discs are needed; no game files are included.

<img src="docs/media/gameplay-xbox-retail.jpg" alt="The Xbox retail game recompiled, 16:9: the ramparts cutscene, the Vizier's guard, the Prince in the courtyard, a wall run, a ladder, the inner courtyard, two sword fights, the pause menu">

*The Xbox retail game at 16:9 (Hor+): a cutscene on the ramparts, the
Vizier's guard, the courtyard, the wall-run and ladder tutorials, two fights
and the pause menu.*

<img src="docs/media/gameplay-xbox-demo.jpg" alt="The Xbox demo recompiled, 16:9: main menu, intro, the Prince waking, the Vizier's soldiers, explosions on the ramparts, a ledge, the Sands of Time transition">

*The Xbox E3 2003 demo at 16:9: main menu, intro, the first level and the
ramparts under fire.*

## Play

1. Build `bin\` (see Build): `sot-xbox-retail.exe` and `popsot-launcher.exe`.
2. Start `popsot-launcher.exe`. On the first start its setup wizard asks for
   the image (ISO) of your USA disc and, optionally, of your European disc
   for the German speech, and copies the game files into a folder of your
   choice.
3. Choose the settings on the launcher's tabs and press **Play**.

In the game, the main and the pause menu have **Port Options**: graphics,
sound, controls, level select, cheats, prototype features and the debug
page. Space or controller A skips videos; F11 switches between window and
full screen; F6 shows the diagnostics panel; F9 captures a frame into
`logs/capture/`. More in the [Windows instructions](port/windows/README.md).

Supported executables (SHA-256 of `default.xbe`); other revisions are refused
because every address in the project belongs to these files:

```text
xbox-retail  753C58B191279BF4D1A8B28A936DF36AA222ED566487AB1FBDFCD090E91EE379   USA (NTSC) retail
xbox-demo    79C1A47828630AB0D4E5DD19B1FD036FFA58EA2C0FE729B0D3456DAC3E79D4F5   E3 2003 demo
```

## Build

Windows 10/11 x64, Visual Studio 2022 (MSVC), CMake and Python; Qt 6 for the
launcher. For a build the game's `default.xbe` goes into `Assets/<target>/`
(ignored by Git), as do the game files for `tools/run.ps1`.

```powershell
.\tools\recomp.ps1 -Target xbox-retail     # analyse, lift and build the game
.\tools\build-launcher.ps1                 # the launcher (Qt 6), into bin\
.\tools\run.ps1 -Target xbox-retail        # play from the source checkout
```

The launcher and the game take the PC version's icon when
`python tools\extract-icon.py "<PC game folder>\POP.EXE" bin\pop.ico` has
written it from your own copy. Details: [port/windows/README.md](port/windows/README.md).
An Android build (one app for both games, Vulkan) exists as well:
[port/android/README.md](port/android/README.md).

## How it works

An Xbox executable is 32-bit x86 code. POPSoT lifts every routine of
`default.xbe` to C with [xboxrecomp](https://github.com/sp00nznet/xboxrecomp)
(vendored and patched under `port/third_party/xboxrecomp`, one runtime for
both targets, compiled for one of them per build tree) and links the result
against that Windows replacement for the console:

```text
default.xbe ──► disassemble ──► find functions ──► lift to C ──► MSVC x64
                                                       │
            ┌──────────────────────────────────────────┘
            ▼
   lifted game + XDK code          (source/<target>/gen/<folder>, generated)
            │ kernel calls, GPU pushbuffer, USB, overlay registers
            ▼
   runtime (port/third_party/xboxrecomp/src, built for this target)
     kernel    files, threads, timers, interrupts
     nv2a      pushbuffer executor ─► Direct3D 11 (register combiners in HLSL)
     apu       voice processor and mixbins ─► XAudio2 (stereo or 5.1, reverb)
     video     window, PVIDEO overlay (Bink movies)
     usb/input emulated OHCI controller, keyboard or XInput pad
```

The game's own code runs: its BigFile loader, the Jade engine, the compiled
AI functions, the Bink decoder and even the statically linked Xbox Direct3D,
which keeps writing NV2A commands that the runtime executes on the host GPU.
Details, addresses and the fixes that were needed are in
[docs/RECOMP.md](docs/RECOMP.md); every change and what was measured is in
[docs/VALIDATION.md](docs/VALIDATION.md).

## Status

| | Xbox retail (USA) | Xbox E3 demo |
| --- | --- | --- |
| Playable | yes | yes |
| Menus, movies, saving | yes | yes |
| Sound | 5.1 or stereo, room reverb | stereo |
| German | texts, subtitles, speech (European disc) | English only |
| Direct3D 11 | default | default |
| Direct3D 12, Vulkan | experimental | experimental |
| Android | experimental | experimental |
| Compared frame by frame with a console | not yet | not yet |

## Repository layout

```text
  source/                one folder per Xbox executable (the game side)
    xbox-demo/           target.h (addresses of that XBE), seed_functions.json,
                         target_hooks.c, gen/ (lifted C by folder)
    xbox-retail/         the same for the retail game
  port/                  what replaces the console
    windows/             Windows host: entry point, options, crash report, profiler
    android/             Android app: native host, Gradle project, touch controls
    game/                hand-written overrides the lifted code calls
    third_party/xboxrecomp/      recompiler (tools/) and Xbox runtime (src/), patched
  launcher/              POPSoT Launcher (Qt 6): ISO installation, settings, Play
  tools/                 recomp.ps1, run.ps1, build-launcher.ps1, extract-icon.py, ...
  docs/                  RECOMP.md, VALIDATION.md, LICENSING.md, media/
  Assets/                your game files (ignored)
  bin/ build/ logs/      outputs (ignored)
```

## Contributing

Read [AGENTS.md](AGENTS.md) first: it is short and it is the rule set. In
brief: never commit game files, executables or extracted assets, and never
edit `source/<target>/gen` by hand (`tools/recomp.ps1` writes it); fix
behaviour in `port/`, the target's `target.h`, hooks and seed list, or the
runtime; record the XBE address that justifies every target-specific change;
and write down what you actually observed in `docs/VALIDATION.md`.

## Licence

POPSoT Recomp's own files are MIT. `port/third_party/xboxrecomp` is MIT with
LGPL-2.1-or-later parts taken from xemu; `port/third_party/extract-xiso` is
under its own BSD-style licence. The game, its assets and the C generated
from its executables belong to their owners and are not covered. See
[LICENSE](LICENSE) and [docs/LICENSING.md](docs/LICENSING.md).

*Prince of Persia* is a trademark of Ubisoft. This project is not affiliated
with or endorsed by Ubisoft or Microsoft.
