# Feasibility: a high-level renderer for xbox-retail

Question (user, 2026-10-21): the picture is weak (moderate resolution, poor
post-processing, broken water, culling errors). Should reSOT be rebuilt the
way FuturamaPC was, or is the base good enough to build on?

Today reSOT renders at **NV2A level**: the game's own statically linked XDK
Direct3D runs and writes pushbuffer commands, and the runtime executes them
(`nv2a_hw_*`). The renderer only sees registers and has to infer, by
heuristics, what an effect buffer, a frame copy or a 2D draw is. This study
measures whether rendering can move up to the Direct3D API, where that is
known, and what it would cost. Everything below is from the retail XBE
(SHA-256 `753C58B1…`) and `source/xbox-retail/gen` at `662aea2`.

## How it was measured

- **Names.** The XBE was imported into Ghidra 12.1.2 with ghidra-xbe; its
  XbSymbolDatabase analyzer names XDK functions. The project lives in
  ignored `research/ghidra/`, and the function export is
  `research/ghidra/retail_functions.tsv`. This used FuturamaPC's
  `tools/ghidra_headless.py` and `ghidra_scripts/ExportFunctions.java`.
  Of 291 functions in the D3D section, 150 are named.
- **Calls.** `python tools/d3d-usage.py research/ghidra/retail_functions.tsv`
  takes every call in the generated code from outside the XDK sections into
  D3D, D3DX or XGRPH and names it.
- **Pushbuffer writes and inline state.** The generated code was scanned
  for references to the device global `0x0037B648`, the state variables the
  D3D section uses, and NV2A method headers stored by the callers of
  `D3DDevice_BeginPush`. These were one-off scripts; the counts are below.
- **Effort.** FuturamaPC's renderer (D:\FuturamaDecomp) was measured from
  its sources, reports and git history.

## Findings

### 1. The game uses the Direct3D API

84 library functions are called from game code, 80 of them named, at 663
call sites:

| Kind | Functions |
| --- | --- |
| Textures | 10 |
| Vertex shaders | 9 |
| Immediate mode and push | 11 |
| Render state | 8 |
| Surfaces and targets | 6 |
| Synchronisation | 5 |
| XGraphics helpers | 5 |
| Resources | 3 |
| Pixel shaders | 3 |
| Push buffers | 3 |
| Draws | 2 |
| Present | 2 |
| Bink overlay | 2 |
| Viewport | 2 |
| Gamma | 2 |
| Clear | 1 |
| Visibility | 1 |
| Other | 9 |

The most used:

| Function | Call sites |
| --- | --- |
| `SetRenderState_Simple` | 112 |
| `SetTexture` | 82 |
| `D3DResource_Release` | 36 |
| `SetVertexData4f` / `SetVertexData2f` | 29 / 29 |
| `SetRenderTarget` | 26 |
| `SetPixelShader` | 23 |
| `SetVertexShaderInputDirect` | 22 |
| `D3DTexture_GetSurfaceLevel2` | 18 |

For comparison, FuturamaPC's game called 88 D3D functions from 325 sites.

### 2. Direct pushbuffer writes are few and simple

- **The device global.** All 138 references to it lie in XDK code: 106 in
  the D3D section and 4 functions in the XDK area at the end of `.text`.
  None is in game code.
- **`BeginPush` / `EndPush`.** The game writes NV2A commands itself in 6
  functions: `0x000CFF00`, `0x000D05A0`, `0x000D1190`, `0x000D1360`,
  `0x000D1530` and `0x000D1700`; `EndPush` has 11 callers. The constant
  method headers they write are `BEGIN_END`, a 17-word transform-constant
  load (`0x0BA0`) and vertex data (`0x1850`): immediate-mode geometry.
  The four `0x000D1190`–`0x000D1700` routines are almost the same size.
- **Recorded push buffers.**
  - `BeginPushBuffer`: 2 callers.
  - `EndPushBuffer`: 3 callers.
  - `RunPushBuffer`: 5 callers (`0x000D0EB0`, `0x000D6FD0`–`0x000D7220`).

  The game records D3D calls and replays them. At API level that becomes a
  recorded command list.
- **Lazy state.** 16 of the D3D section's 124 state variables are touched
  directly by about 15 game functions. These are inlined XDK state macros:
  - `0x0037B1A8`: 12 functions, 35 writes.
  - `0x0037B1E0` and `0x0037B520`: 6 functions each.

  FuturamaPC found the same pattern (199 instructions in 22 functions) and
  planned to read it lazily at draw time.

None of this blocks an API-level renderer. In a recompilation the 6 push
writers and the recorded-buffer users can be replaced function by function
in `port/game`, as the KickOff is today.

### 3. Shaders: combiners and vertex programs are real

Unlike FuturamaPC, which used fixed function only, this game uses both:

- **Pixel shaders.** `CreatePixelShader` (11 sites) and `SetPixelShader`
  (23) set Xbox register combiners.
- **Vertex shaders.** `CreateVertexShader`, `SetVertexShader`, constant
  uploads, the 243 precompiled programs in `shaders.bin`, and
  `XGAssembleShader` for programs built at run time (the water, per
  popsot-fixes).

An API-level renderer still receives combiner register values and NV2A
vertex microcode. reSOT already translates both, to HLSL for D3D11 and
`vs_uber` / specialised combiners for SDL GPU. That work carries over. It is
the part FuturamaPC never had to write.

### 4. The renderer is one block with effect modules

- **One block.** All game callers of D3D lie in `0x000C9000`–`0x000D7400`:
  240 functions, mixed with some sound code. The exceptions are the
  present path (`0x0001E5F0`–`0x0001E8C8`, `0x00014910`), the water's
  immediate-mode upload (`0x00053190`) and `0x00068580`.
- **Interface.** The block has 57 entry points called directly from 224
  outside functions, and 41 of its functions sit in pointer tables.
- **Effect modules.** At `0x00428C1C`–`0x00428C68`, five four-entry tables
  each end in a function that sets render targets:

  | Table | Entries |
  | --- | --- |
  | 1 | `0D28D0`, `0D2940`, `0D2500` |
  | 2 | `0D31A0`, `0CB5C0`, `0D2A50` |
  | 3 | `0D32C0`, `0D32E0`, `0D3300` |
  | 4 | `0D3620`, `0D3730`, `0D37D0` |
  | 5 | `0D41C0`, `0D4240`, `0D4300`, `0D4390` |

  `0x000D3290` is a shared empty entry. These look like the post-effect
  modules; popsot-fixes found the PC soft focus as an effect vtable with a
  render method. Which one is soft focus, glow, motion blur or the
  time-power effects is not established; one `RECOMP_FRAME_TRACE` run with
  the effects enabled would map them.
- **Render targets and copies.** `SetRenderTarget` has 12 callers,
  `CreateTexture2` 4 and `GetBackBuffer2` 6. `CopyRects` is never called:
  frame copies are draws into render-target textures, which only an API
  view identifies as such.
- **Visibility.** `BeginVisibilityTest` has 1 caller, `0x000CE2C0`.

### 5. What FuturamaPC did, and what its numbers say

FuturamaPC replaced rendering one level *above* Direct3D, at NetImmerse's
`NiRenderer` (a 75-slot vtable). The XDK D3D never runs there:

- 8 functions got shims (with fake Xbox resource headers in guest memory);
- 79 trap.

Its graphics code is 26,505 lines:

| Part | Lines |
| --- | --- |
| RHI and D3D11 backend | 4,672 |
| Engine-level renderer core | 12,916 |
| Fixed-function shaders | 908 |
| Feature add-ons | 7,164 |

It has 7,624 lines of renderer tests. Calendar time, agent-assisted:
- first image matching xemu in about 7 hours (M3, 2026-10-05);
- resolution and AA the next day;
- render-to-texture, shadows, particles, widescreen and texture packs by
  2026-10-14;
- fixes until 2026-10-20.

The costly pitfalls were:
- pixel centres and scaling, four times: the half-pixel rule, the 1/16
  snap, seams on upscaled targets, viewport rounding;
- Xbox flip semantics: no clear in 2,980 of 3,000 frames, which needs
  persistent back buffers;
- bugs that statistics hid.

Jade has no `NiRenderer`-like interface known so far, and this game needs
combiners and vertex programs. A Direct3D-level renderer for it is
therefore larger than FuturamaPC's, and its 7 hours to first image plus
about 9 days to features is a floor, not an estimate.

### 6. The Jade engine source and the GDI boundary (added 2026-10-21)

The user supplied the Jade engine source and editor of the Rayman 4
prototype (Raving Rabbids phase, about 2006) and two Prince of Persia
prototypes (`G:\popopopopopopo`; usable freely in this private fork,
AGENTS.md). Selected parts are extracted to ignored `research/jade-src/`.

- **`GraphicDK/Sources/GDInterface/GDInterface.h`.** This is Jade's
  boundary between engine and renderer. Each back end fills a table
  `st_GDI` of about 20 function pointers:
  - display init and open/close;
  - `Clear`, `Flip`;
  - view and projection matrix;
  - `DrawIndexedTriangles`, `DrawIndexedSprites`;
  - texture and palette load/store/unload;
  - `SetTextureBlending`;
  - `SetTextureTarget` and `SetViewMatrix_SDW` (shadows);
  - `Request` (generic requests, `GDIrequest.h`).

  This is the counterpart of FuturamaPC's `NiRenderer` vtable.
- **Back ends in the source.** `Libraries/GX8` (Xbox), `Libraries/OpenGL`
  (PC) and `Libraries/XenonGraphics` (Xbox 360), plus `Main/MainPsx2`.
- **GX8, the Xbox back end,** has:
  - `AfterFX` (post effects);
  - `Gx8water`, `Gx8ShadowBuffer`, `Gx8flare`;
  - `GX8PixelShaders` (the combiner set-ups, in source) and
    `GX8VertexShaders` with 16 `.vsh` sources;
  - `GX8renderstate`, `GX8request`, `GX8tex`, `Gx8video`, `bink`;
  - `RASter/GX8_Menus.c` and `GX8_CheatFlags.h` (debug menus).
- **The same table exists in Sands of Time.** `sub_002F9EC0` stores 18
  render-block functions into one table (`+0x0C`…`+0x5C`). The slots match
  GDI roles:

  | Slot | Function | Role | Why |
  | --- | --- | --- | --- |
  | `+0x14` | `0x000CFB90` | Clear | calls `D3DDevice_Clear` |
  | `+0x1C` | `0x000CFBF0` | Flip | calls `D3DDevice_Swap` |
  | `+0x24` | `0x000CFC10` | SetProjectionMatrix | the projection reSOT hooks for Hor+ |
  | `+0x28` | `0x000D0090` | DrawIndexedTriangles | calls `DrawIndexedVertices` |
  | `+0x34` | `0x000D05A0` | probably DrawIndexedSprites | writes through `BeginPush` |
  | `+0x58` | `0x000D04B0` | probably SetTextureTarget (shadows) | sets a render target, clears, gets a surface level |

  The order differs slightly from 2006, so every slot has to be confirmed
  in this XBE.
- **No debug strings.** The retail XBE has none of the source's string
  literals (1 of 281 found), so matching is structural: tables, call
  patterns, constants.
- **The prototypes.**
  - The Warrior Within PS2 review build (`SLUS_210.22`, 2004-10-25) has no
    symbol table, only a few type names in text.
  - The Two Thrones Xbox prototype (`default.xbe`, 2005-10-02, devkit key,
    `EmeaMag_v001`) carries no Jade identifiers.

  Both are intermediate GX8 versions for cross-matching, not name sources.

This turns option D from "unknown" into a concrete one:
- **The interface.** Replace GX8 behind the GDI table (18 slots in this
  build) plus the GX8 entry points the engine calls directly. The render
  block has 57 such entry points, among them the effect modules, water and
  shadows.
- **What the replacement gets.** It receives Jade geometry (`GEO_`
  elements, vertices, UVs), materials and camera matrices: what each draw
  *is*.
- **What the source provides.** It describes what GX8 does with them,
  including the combiner set-ups and vertex shaders in readable form. The
  vertex shader `.vsh` sources and `GX8PixelShaders.c` can be rewritten as
  HLSL directly instead of translating NV2A state.

### 7. GDI slots, the quad helper and the effect modules (2026-10-22)

Decompiled with Ghidra 12.1.2 and ghidra-xbe, with the XDK and PS2-prototype
names applied (`research/ghidra/`).

**GDI table.** `sub_002F9EC0` fills 24 slots. Their order is the 2006
`st_GDI` order with one slot added after Clear and two before
SetTextureTarget:

| Slot | Function | Role |
| --- | --- | --- |
| 5 | `0CFB90` | Clear |
| 6 | `0D0CF0` | new in 2003 (an immediate-mode quad) |
| 7 | `0CFBF0` | Flip |
| 8 and 23 | `0D0530` | SetViewMatrix and SetViewMatrix_SDW: copies 16 floats to `0x00440BC0`, then the matrix upload `0CD970` |
| 9 | `0CFC10` | SetProjectionMatrix |
| 10 | `0D0090` | DrawIndexedTriangles |
| 11 | `0D0320` | Request |
| 12 | `0D0570` | SetTextureBlending |
| 13 | `0D05A0` | DrawIndexedSprites: builds four corners per sprite through `BeginPush` |
| 14–19 | `0D03A0` … | texture load/palette slots |
| 22 | `0D04B0` | SetTextureTarget: render target = a shadow texture from `0x00764CCC` or the main surface |

**Quad helper.** `0x000CBDE0` draws a pre-transformed quad: FVF `0x444`,
XYZRHW + diffuse + 4 texture coordinate sets, positions minus 0.5 px. It
uses immediate mode (`Begin`/`SetVertexData*`/`End`). Every effect draws
through it with positions in target pixels and texel coordinates.

**Effect modules** (`0x00428C1C`, four entries each, the last one renders):
- They share one context object: frame copy texture `+0x80`/`+0x94`/`+0xBC`,
  render targets `+0xCC`/`+0xC0`, full size `+0x6C/+0x70`, half size
  `+0x74/+0x78`.
- `0D2500`: two tinted passes and a reduction; not identified.
- `0D2A50`: up to six ping-pong passes whose offset follows the camera
  motion, so **motion blur**.
- `0D3300`: four passes with the texture scale halving each time, so
  **zoom blur**.
- `0D37D0` and `0D4390`: decompiled, not identified yet.
- The 2006 GX8 `AfterFX` is one monolithic module, so these 2003 classes have
  no one-to-one counterpart there.

## Options

| | What | Keeps | Cost (calendar, at FuturamaPC's pace) | Solves |
| --- | --- | --- | --- | --- |
| A | Keep polishing the NV2A renderer | everything | ongoing | slowly, by more heuristics |
| B | **Annotated NV2A**: hook the named D3D calls (`SetRenderTarget`, `CreateTexture2`, `GetSurfaceLevel2`, `Clear`, `SetViewport`, the effect modules, `DrawVerticesUP`) only to tell the existing renderer what each surface and draw is | the whole renderer, Android | about 1 week | effect buffers at full resolution (the pixel-unit vs. texture-space rule from popsot-fixes), the water frame copy, 2D/HUD classification; the basis for C |
| C | **HLE Direct3D**: replace the 84 called functions (plus the 6 push writers and 5 replay sites) with an API-level renderer on SDL GPU (D3D12/Vulkan, Android too), reusing reSOT's combiner/vertex translation, texture formats and swizzling | game logic, kernel, audio, input, saves, tests, German speech | first image: several days; parity with today: about 2–3 weeks; then features | all of the above at the root, plus MSAA, texture packs, vector fonts, interpolation later |
| D | **Engine level**: a new GX8 behind Jade's GDI table (18 slots here) and the directly called GX8 entry points, on SDL GPU, using the 2006 GX8 source as the description and starting point | as C | comparable to FuturamaPC's engine-level renderer, plus confirming each slot and structure against the 2003 build | as C, with draws that carry their meaning; no generic combiner translation needed |
| E | Restart like FuturamaPC (native XBE, own loader) | nothing of reSOT | months | nothing that C does not, and Android is lost |

Culling errors are separate. reSOT widens the frustum side planes for Hor+
(`target_hooks.c`). Other culling, such as sectors or the visibility test
at `0x000CE2C0`, has not been examined. Each option needs that look.

## Risks

- **Combiner and vertex-program coverage.** reSOT's translators cover what
  the current renderer has met. Missing modes (stencil, bump-env,
  dot-product) remain missing in C until written.
- **Fake resource headers.** Game code reads Xbox resource headers (format,
  size, data pointer) directly, as in FuturamaPC. The API layer has to keep
  them consistent in guest memory.
- **No `Unlock` on the Xbox.** Texture or vertex data written by the CPU
  needs change detection (FuturamaPC, LD 2.2).
- **Recorded push buffers.** Their contents must be API calls, not NV2A
  bytes, or C falls back to NV2A execution for them.
- **The fork stops following upstream.** C replaces the core of what
  FulGerNet's project works on daily; merging later changes becomes
  impractical.
- **Unnamed functions.** 4 of the 84 called functions and 141 of 291 in the
  D3D section have no names. The 4 called ones need identifying.

## Recommendation

**Go**, in two stages, and no restart (E). No blocker was found for an
API-level renderer:
- the game uses the API;
- direct pushbuffer writes are 6 small immediate-mode routines;
- inline state is 16 variables in about 15 functions;
- the shader work already exists in reSOT.

1. **Stage B first.** Use the names to annotate the existing renderer:
   render targets and their creators, the effect modules, frame copies and
   2D draws. Then fix the effect buffers at full resolution, the water and
   the HUD on that knowledge. It is about a week, keeps Android, and
   everything learned (which call means what) is needed for C anyway.
2. **Decide between C and D with B's results.** If B reaches the quality
   goal, stop there. If the heuristics still limit it, as they likely will
   for interpolation and texture packs, build a new renderer on SDL GPU.
   With the Jade source, D (engine level, like FuturamaPC) is now the
   likelier target:
   - a smaller interface with real meaning;
   - readable shaders;
   - the source as a starting point.

   C stays the fallback where the 2003 GX8 differs too much from the 2006
   source.

The first measurements of stage B:
- the effect-module map (which table is which effect, now with `AfterFX`
  as the reference);
- the GDI slot roles (each confirmed in `xbe_sites.txt`);
- the culling cause.
