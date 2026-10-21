# Knowledge from popsot-fixes and FuturamaPC

This document collects what two related projects already established and
what it means for reSOT, so that it does not have to be found again. It
records knowledge, values, causes and methods. It is not a claim that any of
it has been applied or checked in reSOT, except where a check is named.

| Source | What it is | Commit read | Local copy |
| --- | --- | --- | --- |
| [popsot-fixes](https://github.com/frankyfife/popsot-fixes) | Drop-in `dx.dll` (Direct3D 9 proxy) for the PC (GOG) release of the same game; brings it closer to the Xbox version. Its Xbox side was analysed in Ghidra + ghidra-xbe on the USA `default.xbe` and in an xemu RenderDoc capture | `bf0e030` (2026-09-30) | `D:\Projekte\popsot-fixes`, notes in `docs/TECHNICAL.md` |
| [FuturamaPC](https://github.com/frankyfife/futurama_recomp) (private) | PC port of the original Xbox Futurama (2003, NetImmerse 4.2, XDK 5233): the original XBE runs natively, kernel and XDK libraries are replaced by HLE, new renderer on D3D11 | `948bd8f` (2026-10-20) | `D:\FuturamaDecomp`, design in `docs/loader_design.md`, interfaces in `src/INTERFACES.md`, one report per milestone in `docs/` (mostly German) |

References below: `T:<line>` is popsot-fixes `docs/TECHNICAL.md`; `pf:<file>:<line>`
is popsot-fixes `src/`; `F:<path>` is a FuturamaPC path; `LD` is FuturamaPC
`docs/loader_design.md`; `IF` is FuturamaPC `src/INTERFACES.md`.

## Rules for using it

- **popsot-fixes is one of the "other PoP tools"** in [AGENTS.md](../AGENTS.md):
  a comparison reference. Its code is not copied. Facts, values, causes and
  addresses may be used once they are checked in the target XBE.
- **PC addresses** (`gpp.exe`, image base `0x400000`) are never valid here.
  They are given only to find the code in popsot-fixes' notes. Each item says
  whether an Xbox address exists.
- **Xbox addresses from popsot-fixes belong to the USA retail XBE.** Checked
  2026-10-21 in `source/xbox-retail/gen` (HEAD `4ee3626`): all 23 code
  addresses listed in [Xbox retail anchors](#xbox-retail-anchors) are function
  definitions there, and `0x007584F0` is read as a global
  (`engine/engine_0000.c`). This strongly suggests the same build as
  `xbox-retail` (SHA-256 `753C58B1…`), but the hash of popsot-fixes' XBE was
  not recorded. None of the four sampled addresses occurs in
  `source/xbox-demo/gen`: **nothing from popsot-fixes applies to the demo
  until it is found again in the demo XBE.**
- **FuturamaPC code** is GPL-3.0 with an attribution term. It is our own work,
  so its author can contribute parts under MIT. Each such contribution must
  say so in its commit message. Third-party code in FuturamaPC (doctest) keeps
  its own licence. Most of what is useful here is method and tooling, not code.
- An item moves from here into the README status table and
  [VALIDATION.md](VALIDATION.md) only when it has been done and observed in
  reSOT, as AGENTS.md requires.

## Rendering and effects

### Effect buffers at internal resolution

**reSOT now.** `hw_surface_scale`
(`port/third_party/xboxrecomp/src/kernel/nv2a_hw_d3d11.inc:909-933`) scales
only surfaces at least 640 wide with `InternalResolutionPercent`. Narrower
targets get the effect-quality budget (25–100 %, never more,
`resot_effect_quality.h:10-16`). The comment gives the reason: enlarging
targets without converting the guest's texel offsets gave "layers out of
register" (RECOMP.md:349-356, VALIDATION.md:3423). At 1440p every blur, bloom
and shadow buffer therefore stays at 480p size or smaller.

**popsot-fixes solved the same problem on PC** (T:113-126, pf:proxy.cpp:426-633, 747-766):

- Enlarge the effect targets by k = back-buffer height / 512, up to 4.
- Scale every size given in **target pixels**: copy rectangles, quad corners,
  blur destination width and height.
- Leave sizes given as **texture-space fractions** (the blur pass's
  u/v extents) alone. Scaling them too broke the image (popsot-fixes commit
  `afb12e9`).
- Enlarging only the destination made each pass cover the top-left corner and
  left "a small ghost in the screen corner" (commit `de8d146`). That is the
  same family of symptom reSOT recorded.

**For reSOT.** At NV2A level the equivalent question is per draw: is a vertex
position in target pixels (pre-transformed, `w==1`) or is a texture coordinate
unnormalised (in texels, as with Xbox rectangle textures)? Both need the scale
factor. Normalised coordinates do not. This is the first candidate for an
"Ultra" step above High. Check with `RECOMP_HW_GLOW_DUMP` and frame traces
that every pass of a chain covers its full target.

### Soft focus (halo and grey ghost)

Known on PC (T:89-111; PC addresses only: render method `0x670710`, effect
vtable `0x7b18e4`, blur pass `0x66b540`, quad helper `0x66b300`, kernel
`0x7b18fc`):

- The frame copy is drawn into target A, downsampled 2:1 into B, blurred into
  a quarter of A and back into half of B. The result is laid over the finished
  frame at alpha `0x68` (41 %).
- The blur kernel has three taps (−1,−1), (+1,0), (0,+1) with weight 1/3. It is
  asymmetric, so the blur drifts up-left. Tap step = extent / source size.
- The copy is taken **before the glow is added**. Over glowing areas the dark,
  unlit picture is laid on top: a ghost around characters and a grey ghost
  inside lights.
- popsot-fixes states this is also present on PS2 and Xbox at native
  resolution, so it is the game's design, not a PC defect (README:22-28).
- An emulator that steps the kernel in texels of enlarged targets makes the
  halo shrink with resolution.

**For reSOT.** The Xbox soft-focus pass is not identified yet. The demo's
320×240 ping-pong passes (POST-PROCESSING.md:49-51) are a candidate, not a
finding. Once found, two choices need options, both off by default:

- tap step in original texels (authentic halo) or in enlarged texels (tight);
- copy before glow (original) or after glow (no grey ghost).

### Glow downsample chain

On PC the glow chain is a frame copy → 256, 128, 64, 32, 16, 8, each blurred
twice, added over the frame in pairs (T:101-103). A **point-sampled** 2:1 step
reads exactly on a texel border, so each level shifts half a texel and the
glow ends up-left after five levels. Forcing linear filtering when a render
target is sampled into a smaller one fixes it (pf:proxy.cpp:362-372).

**For reSOT.** Check whether the Xbox chains (demo 640 → 320 → 160 → 80;
retail down to 10×7, RECOMP.md:536, POST-PROCESSING.md:49-51) use point
sampling for their downsamples. reSOT collapses guest min/mag/mip filtering to
one linear-or-point bit (`nv2a_hw_d3d11.inc:1550-1566`), so a point downsample
would drift the same way. Note also that `TextureGlowEffect=0` is the default
on both hosts (`port/*/default-options.ini`), so the default picture has no
final glow composite: not the original look.

### Water refraction

Established for the Xbox (T:22-81, pf:proxy.cpp:923-967):

- **Simulation.** CPU heightfield, identical on PC and Xbox. Xbox: step
  `0x00051C00`, impulses `0x00051800`, normals `0x00051A70`, heights to vertex
  buffer `0x00053190`. None of the 243 precompiled Xbox vertex shaders in
  `shaders.bin` displaces vertices. The water vertex shader is **compiled at
  run time from source text inside the XBE**.
- **Refraction source.** Texture stages 1–3 sample a **full 640×480 frame copy
  with linear filtering**.
- **Constants.** c12 = (ratio 0.6, ratio² 0.36, x scale **28 px**, y scale
  **4.5 px**), c13 = (0.5, 0, ZOFFSET **3**, ZMAX **10**). Offset =
  scale × (min(eye z, ZMAX) + ZOFFSET).
- **Pixel shader.** An `lrp` chain. Combiner constants: water colour 0.086,
  specular 0.102.

**For reSOT.** The scales are in **pixels**: the frame copy is sampled with
unnormalised texture coordinates. When the frame copy follows internal
resolution, those coordinates and the refraction offsets must be multiplied by
the same factor. Otherwise refraction is too weak by that factor, or only the
top-left of the copy is sampled. POST-PROCESSING.md:73 lists water as
unidentified. The values above are what to look for in a frame trace. Check
this before any change to how the frame copy is scaled. The xemu capture under
[Xbox reference material](#xbox-reference-material) has these draws.

### 2D layer and HUD

- On PC every menu, text and HUD element is a pre-transformed quad in a
  virtual 640×480 space stretched to the back buffer (T:162-176). popsot-fixes
  moves x toward the centre by (4/3) / aspect and leaves **full-width quads**
  (fades, letterbox bars, full-screen pictures) alone. It excludes the
  post-effect quads, the video quad and the screen distortion.
- FuturamaPC classifies each screen-space draw by the third of the 640×480
  grid its HUD group centre falls into (left, centre, right), with an
  exception table, and keeps the frontend at 4:3 because Hor+ revealed parked
  menu panels (F:docs/g3_report.md:16-62).

**reSOT now.** reSOT has its own heuristic (RECOMP.md:379-405,
`nv2a_hud_layout.h`): `w==1` geometry centred at 4:3, flat side pieces in the
top and bottom bands shifted toward the edge. The right-side and bottom HUD
elements, other aspect ratios and all HUD states are unchecked
(VALIDATION.md:1971-1973). The "Saving profile" text wider than its box at
16:9 is open (VALIDATION.md:3156-3158). The thirds rule is a candidate for the
unchecked cases.

### 3D main menu at 16:9

The world `menu3D` was built for 4:3, so its edges show in widescreen
(T:323-343, pf:menucam.cpp:311-433). popsot-fixes moves the camera instead of
narrowing the view:

- **Detection.** By the world name (`char[60]` at world `+0x1D8`) when the
  `.wow` parser runs. Worlds merge into a "SuperWorld", so the current world
  says nothing.
- **Values (world units, from game data, probably the same on Xbox).**
  Camera02 at (−83.59, 1.39, −2.66). The new-game flight ends at
  (−92.802, −0.965, 3.932) after about 8 s. The Prince stands on the balcony at
  about (−103.2, −5.05, 1.0).
- **Fix.** +3.5 along the view direction, +6.25 on world Z.
- **During the flight.** Follow the game's progress s (projection onto the
  menu → end line). Aim at the Prince over the first 15 % with a smoothstep.
  Hold the end pose while the balcony camera rests. Blend over 0.75 s if the
  next camera is within 3 units, otherwise cut.

**reSOT now.** In the demo the garden is not modelled past its 4:3 edge, so
the black clear colour shows at the lower left (VALIDATION.md:3454-3456).
Retail's menu scene is not documented. The PC offsets apply to the retail
`menu3D` world if the data matches. The demo menu is a different scene.

### Camera structure

On PC (T:310-321, pf:menucam.cpp:9-17; offsets to check in the Xbox XBE):

- World rows I `+0x88`, J `+0x98`, K (view direction) `+0xA8`; position
  `+0xB8`; view matrix `+0xCC`. The main view camera is display `+0xCC`.
- The view is built several times per frame (main view, visibility, passes
  from the origin). Filter by pointer and position.
- GOG's widescreen patch replaces the projection's tangent factor 0.5 and
  divisor 2.0.

reSOT already does Hor+ with its own projection and culling hooks
(`source/<target>/target_hooks.c`). These offsets matter for the free camera
and the menu camera.

### Anti-aliasing and filtering

reSOT has no MSAA, no supersampling beyond the internal scale, and no
anisotropic filtering (`nv2a_hw_d3d11.inc:1067,1277,1322`,
`nv2a_hw_native.inc:292,927`). FuturamaPC's recipes are backend-level and
apply to D3D11, D3D12 and Vulkan alike (LD 8.6 G3–G6, 8.8 R1/R3):

- **MSAA.** Persistent multisampled back buffers keep the Xbox flip
  semantics.
- **Alpha to coverage.** Only for alpha-tested, unblended draws, with coverage
  (a − ref) / fwidth(a) + 0.5.
- **Supersampling.** Exact box filter at 2×, otherwise Catmull-Rom with
  anti-ringing.
- **Resolve.** Gamma-correct resolve via TYPELESS targets with sRGB views,
  **off by default** because the Xbox resolved in gamma space.
- **Fractional scales.** "Anti-aliased nearest" for point-sampled 2D.
- **NV2A pixel centres.** Integer pixel centres and a 1/16-pixel vertex snap.
  The 0.5 offset applies only at 1× and one sample (LD:813,832;
  F:docs/m8_report.md:61-79).

### Texture packs

- **FuturamaPC** (IF 4.38): hash "FTX1" = XXH64 over a header (format, bpp,
  width, height, masks, byte count), the level-0 bytes and the palette for
  palettised formats. Mips are not hashed. The hash is taken at upload, for
  static textures only, and the swap is asynchronous. `[render]
  texture_pack_dump` writes all textures. `F:tools/texture_upscale.py` builds
  a pack from a dump:
  - alpha is handled apart from colour and never goes through the AI;
  - the colour under transparent texels is kept;
  - text has its own path;
  - output is BC1/BC3 DDS with mips.
- **popsot-fixes** (T:351-426, pf:texpack.cpp) reads the PC "Evgesha" HD pack
  (SHA-256 of the original PC texture). PC uploads do not hash like Xbox
  swizzled data, so that pack cannot be matched directly. Its 32-bit keys look
  like Jade BF keys. That is an inference, but if true it would allow keying on
  the Jade texture key instead of content. Known pack defects: 32 empty
  textures, about 0.8 brightness, and a font atlas reduced to 256×64.

**For reSOT.** Hash at first use of an NV2A texture: unswizzled level 0 plus
format and size, cached by address and content. The Xbox D3D has no unlock to
signal writes (LD 2.2), so dynamic textures need dirty tracking or re-hashing.

### Movies

- **reSOT now.** The game's own Bink decoder writes YUY2 to the overlay, and
  the host converts it to RGB on the CPU and shows it at 4:3
  (`video/fb_present.c:723-845`). There is no replacement path.
- **popsot-fixes method** (T:428-457, pf:video.cpp), PC Bink 1:
  - keep the original decoder for sound, timing and skipping, and substitute
    the picture at the matching frame time;
  - decode on a thread with a four-picture queue (2560×1792 on the game thread
    took about 37 ms and broke the audio);
  - convert NV12 with BT.601 unless the file is tagged BT.709;
  - H.264 pads the height to a multiple of 16 (1384 → 1392), and this is
    announced only with the first sample, which caused a green bar;
  - some videos are opened ahead and made current later (loading screens).
- **Upscaling** (popsot-fixes README:100-106). 4x NMKD Siax 200k keeps the
  depth-of-field blur, realesrgan-x4plus does not. Burned-in text is re-set,
  not upscaled.
- **FuturamaPC pipeline** (`F:tools/movie_upscale.py`): FFmpeg → PNG →
  Real-ESRGAN → 75/25 AI/Lanczos mix → H.264 with the exact source
  timestamps → check frame count and every timestamp. Only the XMV demuxer and
  the pack key are XMV-specific.

## Input and vibration

- **Pad.** reSOT exposes one pad on port 0 only. `XGetDevices` returns 1 and
  `XGetDeviceChanges` always returns 0 (`source/xbox-retail/target_hooks.c:328-340`).
  Hot-plug is host-side only.
- **Xbox pad map** (T:228-258, retail `0x000DC160`): action bits
  - 0 A, 1 B, 2 X, 3 Y
  - 4 Black, 5 White, 6 left trigger, 7 right trigger
  - 8 Back, 9 Start, 10 right thumb, 11 left thumb
  - 12–15 D-pad up, right, down, left

  Movement uses actions `0x25`–`0x28`, the camera `0x29`–`0x2C`.
- **Modern layout.** The Xbox pad had no shoulder buttons. popsot-fixes follows
  the PS2 version: LB = left trigger (rewind), RB = right trigger (special
  action), LT = Black (alternate view), RT = White (look). If reSOT offers this
  as a preset, the `l`/`r`/`L`/`R` button glyphs in font 5 (`\p5\<c>`,
  T:294-308) no longer match and need other art.
- **Vibration has never been validated in reSOT** (VALIDATION.md:3413). The
  Xbox side is known (T:260-292, all retail):
  - large motor `0x000DC700` (pad in EAX, strength 0–255 in EDX);
  - small motor `0x000DC6A0` (pad in ESI, on/off in ECX);
  - option `0x000DBAC0`; countdown per 30 Hz game frame in `0x000DC160`;
  - AI functions small `0x002DFF20`, large `0x002DFF70`, enable `0x002E0070`;
  - callers: Prince per frame `0x0018E510`, rumble generators `0x0020C730`,
    start/stop `0x0012E000` / `0x0012E5D0`.

  Breakpoints or traces on these show whether the game asks for rumble and
  whether `xbox_HlePadRumble` (`usb/usb_gamepad.c:611-621`) passes it on.
  Mapping used on PC: large to the left motor at s / 255 · 65535, small to the
  right motor at `0xFFFF`.
- **FuturamaPC input** (F:docs/m4_report.md:9-26). It merges inputs from all
  pads: buttons are ORed, the larger analog value wins. It reloads XInput at
  run time with throttled hot-plug polling, sends rumble to the last pad
  actually used, and maps the mouse through the inverse of the game's stick
  curve. All of it is host-side and fits behind reSOT's pad report.

## Timing

- **reSOT now.**
  - Retail runs variable-step after the timestep fix
    (`source/xbox-retail/target_hooks.c:29-74`).
  - "Whether the demo's simulation speed is correct above 60 fps was not
    measured" (VALIDATION.md:2670-2673).
  - There is no interpolation.
  - The bath-fight drop (120 → 85–90 FPS, lockstep with the executor) is open
    (VALIDATION.md:2404-2431).
- **FuturamaPC** (F:docs/m3b_60hz_report.md:9-16, F:docs/m3d_report.md:31-53):
  - a lockstep virtual clock instead of raw QPC samples, because QPC jitter in
    a fixed-step loop gives frames with 0 or 2 steps;
  - a pacer coupled to the display rate;
  - an inventory of per-tick versus per-second code before any rate change.
- **Interpolation needs object-level data.** At NV2A level only matrices in
  vertex-shader constants are visible. In reSOT it would mean overriding the
  Jade camera and matrix submission in `port/game`, keyed by call site and
  vertex buffer. That is a project of its own.
- **Pitfalls worth reusing:**
  - patching the tick rate speeds up tick-bound code (fades, ×0.98 damping,
    a hard-coded 0.02 s step, script waits);
  - load screens are frame-bound when host I/O is instant;
  - a frame without draws must still present, and it shows the picture from
    two presents ago (LD:830,863,869,897,971).

## Audio

- **reSOT now.** reSOT emulates the APU voice processor at register level. The
  GP/EP DSP is a stub: effects are bypassed, sends dropped, mixbins folded to
  stereo (`apu/apu_dsp.c:1-14`, RECOMP.md:430-440). There is no reverb.
  Retail missing sounds and music or speech dropouts are open
  (VALIDATION.md:2869-2873).
- **FuturamaPC** replaced DSOUND by HLE, so its engine does not fit. Its
  verification tools do (F:docs/m5_report.md:20-51):
  - `F:tools/audio_compare.py`: correlates an output capture with the disc
    originals, decoded independently, and reports correlation, pitch ratio,
    gain per channel, SNR, balance and dropouts;
  - a reference Xbox ADPCM decoder, bit-exact with ffmpeg `adpcm_ima_xbox`;
  - the exact DSOUND distance and level model;
  - the I3DL2 preset table.

  These can turn "some sounds missing" into a measurable test with
  `RECOMP_APU_WAV`. Note one lesson: verification without ears missed a
  device-specific bug that only a real 5.1 endpoint showed (LD:1255).

## Saves

- **reSOT now.** The guest signs saves with `XboxSignatureKey` and all other
  kernel keys set to zero (`kernel/kernel_bridge.c:333-343`). Saves will
  probably not verify on a console or in a keyed xemu (not tested). There is
  no verify or resign tool, and the full save/load cycle is unvalidated.
- **FuturamaPC** (`F:tools/verify_save.py`, LD R11):
  - XCalculateSignature = HMAC-SHA1(key, data);
  - the save folder name is 12 hex digits of the name hash mod
    `0xFFFFFFFFFFC5`;
  - `SaveMeta.xbx` holds a UTF-16 `Name=`;
  - console key mode = HMAC-SHA1(XboxCERTKey, certificate signature key)[0:16],
    which is unverified with a real console save;
  - `--resign` for imports.

  Only the 4096-byte container with the signature at `0xFEC` is
  Futurama-specific. Also generic for a kernel HLE (F:docs/m7_report.md):
  - FATX enumerates in creation order;
  - the "no space" dialog preselects the Dashboard, so a dashboard launch must
    return to the game.

## Extras

- **Free camera** (T:345-349, pf:menucam.cpp:210-257):
  - write yaw and pitch rows into the main camera struct, keeping the I/J signs
    of the start camera;
  - speed 6 units/s, ×4 fast, ×0.25 slow; turn 2 rad/s; pitch limited to
    ±1.5;
  - freeze through the main-loop pause flag (PC `0xAF4498`; while it is set
    the loop only renders).

  The Xbox addresses need finding. In reSOT this is an override in
  `port/game`, simpler than a detour.
- **Cheats** (T:474-536, pf:cheats.cpp).
  - **Symbols.** The PS2 prototype SLUS_207.43 (2003-08-19) ships
    `MAPFILE.MAP` with symbols such as `Cheat_Set`/`Cheat_Test`,
    `Global_Bhv_MainLoop`, `fn_RegenerateLife`,
    `fn_Combat_ProcessCurrentHurt`, `fn_SetWeapon` and `Spawn_WP__MSG`.
  - **Cross-build anchors.** Script references and AI function ids are the same
    across builds. Global object `0x4E000254`; the object chain is
    `+0x18 → +4 → +0x44` (variables).
  - **Game rule.** Damage 1000.0 is the game's own instant kill.
  - **Variable offsets differ between builds** (life `+0x19C` on PC, `+0x17C` on
    PS2). Find them again in the Xbox XBE, for example from its
    `fn_RegenerateLife`.
- **Level select.** Retail has none (VALIDATION.md:2570-2573). popsot-fixes'
  `P_SpecialLoad` is a PC menu page (T:188-205). For retail it is an idea, not
  a hidden page.
- **Console menus** (T:207-226, pf:consolemenu.cpp; retail):
  - menu manager `*(0x007584F0)`, same layout as PC: open page count `+0xF0`,
    page stack `+0x6C + i·4`;
  - page state `+0x84`: 0 inactive, 1 fading in, 2 active, 4 → 5 faded out,
    6 another page on top;
  - dialog answers at `+0x1A0`/`+0x1A4` and `+0x1B0`/`+0x1B4`;
  - functions: tick `0x00017D10`, element show `0x00038E20`, open menu
    `0x000E53F0`, dialog answer `0x00018BD0`, save script `0x002CAE60`,
    save-list fill `0x002C95B0`;
  - stable Jade keys: load page `0x0B004F85`, level-name text bank
    `0x0B004F7D`.

  These help with the in-game options pages and the "Saving profile" box.

## Launcher and options

FuturamaPC's launcher is Qt (Windows only). Its reusable core is a contract
with the game executable (IF 4.21:773-847):

- **Tool modes.** Each runs before the game starts and writes JSON:
  - `--print-config-schema`: section, type, default, choices with labels,
    range, help, tab, order and widget for every key;
  - `--check-iso= --result=`: disc check plus XBE hash, with a reason code;
  - `--query-gpu`;
  - `--print-paths`.
- **Schema test.** `F:tests/schema_tests.cpp` scans the host source so that
  every key read has a schema row and every row is read.
- **Exit codes.** 0 ok, 1 config, 2 stage, 3 fatal, 5 watchdog, 6 already
  running, ≥ `0xC0000000` crash. The host writes a one-line cause to stderr and
  never shows a message box.
- **Result screen.** Shows the cause, the last 30 log lines, and buttons to
  open the log or folder and to retry.

reSOT has a GDI launcher with SHA-256 detection and ISO installation, the F7
panel and in-game option pages. There is no setup wizard and no crash result
screen: the exception handler only logs (`port/windows/src/main.c:438-625`).
AGENTS.md requires launcher parity with Android, so the contract (schema,
exit codes, cause line) is what transfers. A Qt launcher would cover Windows
only.

## Verification methods

This is where FuturamaPC is furthest ahead and where reSOT's README is
weakest ("Checked against the console ⚪").

### xemu reference comparison

Not planned for reSOT as a general method (user, 2026-10-21): only for
special cases. FuturamaPC tools: `F:tools/xemu_capture.py`, `F:tools/m3_check.py:95-112,134-203`.

- **Capture.** Start xemu without touching its config. Grab the client area
  every N s with `PrintWindow(PW_RENDERFULLCONTENT)`, falling back to a screen
  grab, and write PNGs plus an index.
- **Interval.** Choose one that is not a multiple of any animation period:
  a 1 s cycle aliased against 3 s captures, and 0.62 s walks through all
  phases.
- **Scale.** xemu shows 1× nearest-upscaled. Compare on a 640×480 box
  downscale, with xemu's menu bar masked.
- **Metric.** Mean absolute difference per channel, limited per scene. Animated
  scenes use a static-pixel mask built over all reference captures, plus
  explicit masks for periodic regions.
- **Honest scale of the result.** FuturamaPC's README says "bit-identical to
  xemu". Measured, it is exact (0.00) on two static screens, 0.29–0.30 on the
  loading screen and 0.01 on the frontend over its static pixels
  (F:docs/m3_report.md:149,182-188, F:docs/m3c_report.md:11,148). The fixes that
  raised the frontend from 61 % to 98.8 % exact pixels:
  - integer pixel centres;
  - the 1/16-pixel snap;
  - bit-replicating 16-bit texture conversion.
- **Caveat.** xemu is itself an emulation (LD:836-847).

### Retail-XBE byte tests

`F:tests/g8_cheats_tests.cpp:33-77,484-514` loads the XBE from an
environment variable and asserts the exact bytes at every hook and probe site,
vtable slots, string operands and `ret N`. It is skipped when the XBE is
absent. 18 test files use the pattern.

reSOT now has this as `tools/test-xbe-sites.py` (2026-10-21). It checks
claims (call targets, operands, function starts) rather than raw bytes, so
no game code is written into the repository.

### Input scripts that wait on conditions

FuturamaPC runs a text DSL on a virtual pad (F:tools/input_scripts/README.md:14-68):

- **Commands.** press, hold, wait, loop, expect, screenshot, unplug, replug,
  quit.
- **Time unit.** One tick is one game poll of the pad. Absolute ticks drift by
  up to 18 between runs, so scripts wait on conditions read from guest state:
  main menu, level loaded, controllable, paused, rumble, a log line.
- **Failure.** A screenshot and exit code 7.
- **Checkers.** `F:tools/m*_check.py` turns the log into numbered pass/fail
  criteria. Every check also fails on any error, warning, "unimplemented" or
  trap line.

reSOT's `RECOMP_PAD_SCRIPT` uses fixed milliseconds (`usb_gamepad.c:366-410`).
Condition waits would make the scripted runs in VALIDATION.md repeatable.

### Reports and dead ends

Two habits worth copying:

- **Milestone reports.** Each has exit criteria with evidence, a check matrix
  (builds × checks), deviations with ids, and review findings re-checked
  against the binary (e.g. F:docs/m3d_report.md:176-196).
- **The "dead ends" scan** (F:docs/tote_enden.md:1-16,217-225). It looks for
  flags only ever written with 0, conditions that are constant 0, names
  referenced by scripts but absent from the data, stub natives, and the diff
  between prototype and retail. Each finding becomes an option that is off by
  default, so "every option off" stays the original game. This works on Jade
  AI code too: reSOT's `gen/ai` and `gen/scripts` are already C.

### Generic tools

`F:tools/xdvdfs.py` and `F:tools/xbeinfo.py` (header, keys, certificate,
libraries, sections, VA ↔ offset) are game-independent.

## Xbox reference material

An xemu RenderDoc capture of the Xbox game (water scene, 2026-09-23, from the
popsot-fixes analysis) is kept locally at
`research/xemu-capture-2026-09-23/`:

- `cap.rdc`;
- 44 xemu-generated GLSL shaders in `shaders/`;
- the uniform log `water_uniforms.txt`;
- the RenderDoc probe script `probe.py`.

`research/` is ignored by Git, and the capture contains game data, so it is
not published. Which disc it was taken from was not recorded; the popsot-fixes
notes refer to the USA XBE.

## Xbox retail anchors

All from popsot-fixes. Each is a function definition in
`source/xbox-retail/gen` at `4ee3626`. That proves the address exists, not
that it is the function described.

| Address | Meaning |
| --- | --- |
| `0x00051C00` / `0x00051800` / `0x00051A70` / `0x00053190` | Water: step, impulses, normals, heights to vertex buffer |
| `0x000DC160` | Pad map and rumble countdown |
| `0x000DC700` / `0x000DC6A0` | Large / small motor |
| `0x000DBAC0` | Vibration option |
| `0x002DFF20` / `0x002DFF70` / `0x002E0070` | AI: rumble small / large / enable |
| `0x0018E510` | Prince per-frame (rumble script variables) |
| `0x0020C730` / `0x0012E000` / `0x0012E5D0` | Rumble generators, start, stop |
| `0x00017D10` / `0x00038E20` / `0x000E53F0` / `0x00018BD0` | Menu tick, element show, open menu, dialog answer |
| `0x002CAE60` / `0x002C95B0` | Save script, save-list fill |
| `0x007584F0` (data) | Menu manager pointer |

## Goals of this fork (user, 2026-10-21)

- **German speech.** The finished port should speak German: the user rates
  the German dubbing clearly above the English one in tone and timbre. Only
  the speech comes from the European release; everything else stays NTSC
  (the `xbox-retail` USA executable, its text, timing and data).
- **Game logic at 60 Hz.**
- **Cut content, lost levels, features missing from retail, differences and
  easter eggs** become launcher options, off by default, as in FuturamaPC:
  with every option off the game is the original.
- **No xemu comparison** as a general method; only for special cases.
  FuturamaPC did not need it either.

## German speech on the NTSC executable

What the user's discs show (2026-10-21; copies of the index files and the
European XBE in ignored `research/sound-usa/`, `research/sound-pal/`,
`research/pal-retail/`).

**Both discs use the same archive scheme.** `SOUND/<name>.fat` is a
directory of 20-byte headers, each followed by a path:

| Offset | Field |
| --- | --- |
| `+0x04` | Offset into `<name>.big` |
| `+0x08` | Size |
| `+0x10` | Path length |

**The speech data is the same set, packed differently:**

| | USA retail (`xbox-retail`) | European retail |
| --- | --- | --- |
| Executable | 4,603,904 bytes, 2003-10-20 | 4,141,056 bytes, 2004-01-12, entry `0x00311045`, 31 sections: a different build |
| Language banks | `SOUNDX.big`: 274 `sound/<lang>/*.sb2` each for english, french, spanish, plus 98 shared | `soundx0`–`SOUNDX4`: 274 banks plus `stream.ls2` each for english, french, spanish, italian, **german**; `SOUNDX` holds only the 98 shared |
| Streamed speech | `soundx0`–`soundx2.big`: `sound/<lang>/stream.ls2` only | inside each language archive |

**What was compared:**

- **English bank sizes.** All 274 English banks have the same size on both
  discs.
- **German bank names.** The German names are exactly the English ones.
- **Bank contents.** They are not byte-identical: g11_01 differs in 28 bytes,
  g12_01 in 45, all within the first `0x200` bytes. The bank header version
  (`04000a00…`) is the same on both discs and in German.
- **Reading.** The differing fields are probably references into the
  matching `stream.ls2`. This is not yet decoded.

**Approach.** Replace the whole English set with the German one: the 274
`sound/english/*.sb2` entries of `SOUNDX` and `sound/english/stream.ls2` of
`soundx0`, all taken from `SOUNDX4` of the user's European disc. They go
together, so that bank headers and stream match.

Two ways to do it:

- **An installer step** that writes a German `SOUND/` beside the NTSC data,
  derived from the user's disc and never committed (like FuturamaPC's packs).
- **A redirection** in the kernel's file layer.

The language stays English for the game, so menus and subtitles stay NTSC
English.

**To verify:**

- The USA executable accepts the European banks and stream; the same format
  version suggests so.
- The cinematics have five variants on the European disc
  (`VIDEO/30fps/CINE_90x0`–`90x4`) and three on the USA disc. That `90x4` is
  German is assumed from the archive order (0 en, 1 fr, 2 es, 3 it, 4 de) and
  unchecked.
- Lip-sync and timing in cinematics.
- The 275th European entry.

## Game logic at 60 Hz

reSOT's own analysis (RECOMP.md:559-568, VALIDATION.md:3076-3100):

- **Original.** The retail frame clock `sub_000DB740` computes a fixed
  timestep 1,000,000 / (int(refresh) >> 1) µs. At NTSC 60 Hz that is
  33,333 µs: the console's game logic runs at 30 Hz.
- **reSOT.** The refresh field is zero in reSOT, so the target hook replaces
  that step with the measured frame interval. Logic therefore runs at the
  picture rate, for example 120 Hz at 120 fps, with the original 1 ms minimum
  and 160 ms cap.
- **Demo.** The demo has no such hook. Its speed above 60 fps was not
  measured (VALIDATION.md:2744).

**For a fixed 60 Hz,** the step becomes 16,667 µs per tick. Then decide
between two options:

- **Picture limited to 60.** Simple.
- **Picture above 60 with interpolation.** This is FuturamaPC's approach
  (F:docs/m3d_report.md). At NV2A level it needs object-level hooks in the
  Jade engine, so it is a project of its own.

FuturamaPC's lesson applies first: list the code that counts ticks rather
than time (fades, damping, script waits), because a different tick rate
changes its speed (LD:863,897). Whether the European build runs PAL60 is
irrelevant here: logic and data stay NTSC.

## Cut content and differences as launcher options

FuturamaPC's method (F:docs/tote_enden.md) applies to the lifted Jade code in
`gen/ai` and `gen/scripts`. It looks for:

- flags only ever written with 0;
- conditions that are always 0;
- names referenced but absent from the data;
- stub natives.

Sources for reSOT:

- the PS2 prototype SLUS_207.43 with its `MAPFILE.MAP` symbols (popsot-fixes,
  T:474-536);
- the demo against retail: the demo's hidden Load Map page and its levels;
- the European build against the USA build: same game, three months later;
- popsot-fixes' findings (T:188-205; the PC level select is a PC page).

Each finding is marked proven or assumed. Each becomes an option, off by
default, that the launcher offers on Windows and Android alike (AGENTS.md).
FuturamaPC's schema contract (see [Launcher and options](#launcher-and-options))
is the model.

## Suggested order

Each step ends with a VALIDATION.md entry.

1. **Address checks** for `target.h` and `target_hooks.c`. Done 2026-10-21:
   `tools/test-xbe-sites.py`, `source/<target>/xbe_sites.txt`. Retail is
   fully checked against its XBE. The demo is checked against the generated
   code, and against the user's PAL demo disc (same code, different hash;
   VALIDATION.md).
2. **German speech** on `xbox-retail`, as above: decode the differing bank
   header fields, swap the English set for the German one, check in game,
   then make it an installer/launcher option.
3. **60 Hz game logic**: list the tick-bound code, fix the step, decide the
   picture limit; then the same for the demo.
4. **Dead-ends scan and first launcher options.**
5. **Rumble check** with the retail anchors, then the PS2 shoulder layout as
   an optional preset.
6. **Effect buffers at internal resolution**, following the pixel-unit versus
   texture-space rule (water, glow, then soft focus).
7. **Condition waits for `RECOMP_PAD_SCRIPT`** and a log checker, so that
   crash reports such as the garden lever and the second ladder can be
   replayed.
8. **Audio measurements** with `RECOMP_APU_WAV` against the disc originals
   for the retail dropouts; also useful for the German speech.
9. **Save verify/resign tool and real signature keys**, then a full
   save/load cycle.
10. **Retail main-menu camera at 16:9**, then the free camera.
11. **MSAA/alpha-to-coverage and anisotropic filtering**, then texture and
    movie packs.
