/* Prince of Persia: The Sands of Time, Xbox retail NTSC entry hooks.
 * Addresses verified in SHA-256 753C58B1...1EE379 (2003-10-20).
 * XAPI entry/ret bytes, 25-byte caps and 22-byte state verified in retail XPP:
 *   0x0041172B XGetDevices; 0x0041174D XGetDeviceChanges;
 *   0x00417D4B XInputOpen; 0x00417DA1 XInputClose;
 *   0x00417DAD XInputGetCapabilities; 0x00417F85 XInputGetState;
 *   0x00417FF8 XInputSetState.
 * Retail game call sites: sub_000DBB50 (0x000DBB5C, 0x000DBBD5,
 * 0x000DBBE2, 0x000DBBEF) and sub_000DBC60 (0x000DBC76, 0x000DBCAA).
 * RECOMP_USB=1 runs the original XAPI routines instead.
 */
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../port/game/guest_probe.h"
#include "target.h"
#if defined(_MSC_VER)
#define RESOT_TLS __declspec(thread)
#else
#define RESOT_TLS __thread
#endif
extern RESOT_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_esi;
extern ptrdiff_t g_xbox_mem_offset;
#define GMEM32(va) (*(volatile uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define GMEMF(va)  (*(volatile float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

/* Retail's frame clock sub_000DB740 captures elapsed microseconds through
 * its calibrated RDTSC clock (sub_00011A10), then computes a fixed timestep
 * as 1000000 / (int(refresh) >> 1). The refresh field at 0x007654A4 remains
 * zero here: 0x000DB780 / 0x000DB7CD read it, 0x000DB7A7 divides by zero,
 * and _ftol at 0x00348128 produces an invalid integer. The title's 1000-us
 * minimum at 0x000DB7EA then makes a 60-fps picture advance only 0.06 game
 * seconds per wall second. A live capture had new/old timestamps 33860721 /
 * 33843988 us, refresh 0, and timestep 1000 us.
 *
 * At only this _ftol call, replace the invalid reciprocal with the measured
 * interval the game has already captured. Keep _ftol, the original minimum,
 * 160-ms float cap, speed factors and pause handling. Valid refresh values
 * retain the original path; no host-clock scale or frame limit is changed.
 * RESOT_RAW_RETAIL_TIMESTEP=1 restores the unmodified path for comparison. */
extern RESOT_TLS double g_fp_stack[8];
extern RESOT_TLS int g_fp_top;

/* RESOT_DUMP_RAM=<file>[,seconds]: writes guest RAM 0..64 MB once, after
 * the given seconds (default 30) from the first frame. For finding tables
 * such as a font's glyph rectangles offline. */
static void resot_dump_ram_check(void)
{
    static int state = -1;
    static clock_t start;
    static double after = 30.0;
    static char path[260];
    if (state < 0) {
        const char *e = getenv("RESOT_DUMP_RAM"), *comma;
        state = e && *e ? 0 : 2;
        if (!state) {
            snprintf(path, sizeof path, "%s", e);
            comma = strrchr(path, ',');
            if (comma) { after = atof(comma + 1); path[comma - path] = 0; }
            start = clock();
        }
    }
    if (state == 0 && (double)(clock() - start) / CLOCKS_PER_SEC >= after) {
        FILE *f = fopen(path, "wb");
        state = 1;
        if (f) {
            fwrite((const void *)((uintptr_t)g_xbox_mem_offset), 1, 64u << 20, f);
            fclose(f);
            fprintf(stderr, "[DUMP] guest RAM written to %s\n", path);
        }
    }
}

void sub_00348128_enter(void)
{
    resot_dump_ram_check();
    static int configured, raw;
    static unsigned reports;
    float refresh;
    uint64_t now, previous, elapsed;
    double input;
    if (GMEM32(g_esp) != 0x000DB7B8u)
        return;
    refresh = GMEMF(0x007654A4u);
    if (isfinite(refresh) && refresh >= 2.0f)
        return;
    if (!configured) {
        const char *e = getenv("RESOT_RAW_RETAIL_TIMESTEP");
        raw = e && *e && *e != '0';
        configured = 1;
    }
    now = ((uint64_t)GMEM32(0x007654CCu) << 32) | GMEM32(0x007654C8u);
    previous = ((uint64_t)GMEM32(0x007654D4u) << 32) | GMEM32(0x007654D0u);
    elapsed = now >= previous ? now - previous : 0;
    input = g_fp_stack[(unsigned)g_fp_top & 7u];
    if (!reports++ || (getenv("RESOT_TIMER_DIAG") && reports % 120 == 0))
        fprintf(stderr, "[TIME] retail invalid refresh %.3f, fixed-step input %.3f;"
                        " measured interval %llu us (%s)\n", refresh, input,
                (unsigned long long)elapsed, raw ? "original conversion" : "calibrated clock");
    if (!raw)
        g_fp_stack[(unsigned)g_fp_top & 7u] = (double)elapsed;
}

/* Retail water simulation at 30 Hz. sub_000525A0 (single caller 0x00053A7D,
 * cdecl, water object at stack +4) converts the 64-bit frame time
 * 0x007654E0 (us) to seconds, caps it at 1/30 s (0x00429740) and steps every
 * height field with sub_00051C00. That step damps by a constant per call, so
 * at 60 frames per second the waves die out twice as fast and the pool looks
 * flat. Step once per 1/30 s of game time instead: the frame is identified by
 * the new timestamp 0x007654C8, since a scene may hold several water objects.
 * On a skipped frame only the guard and the done flag (+0xA0) are kept, as
 * the original sets it. RESOT_RAW_WATER=1 restores the per-frame step.
 * RESOT_WATER_DIAG=1 reports, every 2 s per object, the largest field
 * amplitude (+0x128) and how many fields are frozen (+0x13C bit 1); a field
 * whose amplitude falls under 1e-5 (0x00429428) is cleared and frozen. */
extern void sub_000525A0_gen(void);

static void resot_water_diag(uint32_t object, int stepped)
{
    static struct { uint32_t object, calls, steps; uint64_t since; } seen[8];
    uint32_t grid, cols, rows, fields = 0, frozen = 0, i, slot;
    float peak = 0.0f;
    if (!object)
        return;
    for (slot = 0; slot < 8 && seen[slot].object && seen[slot].object != object; slot++)
        ;
    if (slot == 8)
        return;
    seen[slot].object = object;
    seen[slot].calls++;
    seen[slot].steps += stepped;
    seen[slot].since += ((uint64_t)GMEM32(0x007654E4u) << 32) | GMEM32(0x007654E0u);
    if (seen[slot].since < 2000000u)
        return;
    grid = GMEM32(object + 0x34u);
    cols = GMEM32(object + 0x38u);
    rows = GMEM32(object + 0x3Cu);
    for (i = 0; grid && cols * rows < 4096u && i < cols * rows; i++) {
        uint32_t field = GMEM32(grid + 4u * i);
        if (!field)
            continue;
        fields++;
        frozen += (GMEM32(field + 0x13Cu) & 2u) != 0;
        if (GMEMF(field + 0x128u) > peak)
            peak = GMEMF(field + 0x128u);
    }
    fprintf(stderr, "[WATER] object %08X: %u fields, %u frozen, peak %.6g;"
                    " %u steps in %u calls\n", object, fields, frozen, peak,
            seen[slot].steps, seen[slot].calls);
    seen[slot].calls = seen[slot].steps = 0;
    seen[slot].since = 0;
}

void sub_000525A0(void)
{
    static int configured, raw, diag, step;
    static uint32_t frame_lo, frame_hi;
    static uint64_t pending;
    uint32_t object = GMEM32(g_esp + 4), saved_lo, saved_hi;
    if (!configured) {
        const char *e = getenv("RESOT_RAW_WATER");
        raw = e && *e && *e != '0';
        e = getenv("RESOT_WATER_DIAG");
        diag = e && *e && *e != '0';
        configured = 1;
    }
    if (raw) {
        if (diag)
            resot_water_diag(object, 1);
        sub_000525A0_gen();
        return;
    }
    if (GMEM32(0x007654C8u) != frame_lo || GMEM32(0x007654CCu) != frame_hi) {
        frame_lo = GMEM32(0x007654C8u);
        frame_hi = GMEM32(0x007654CCu);
        pending += ((uint64_t)GMEM32(0x007654E4u) << 32) | GMEM32(0x007654E0u);
        step = pending >= 33333u;
        if (step)
            pending = pending - 33333u > 33333u ? 0 : pending - 33333u;
    }
    if (diag)
        resot_water_diag(object, step);
    if (!step) {
        if (object && !GMEM32(object + 0xA0u) && !GMEM32(object + 0xA4u))
            GMEM32(object + 0xA0u) = 1;
        g_esp += 4;                          /* ret (cdecl) */
        return;
    }
    saved_lo = GMEM32(0x007654E0u);
    saved_hi = GMEM32(0x007654E4u);
    GMEM32(0x007654E0u) = 33333u;
    GMEM32(0x007654E4u) = 0;
    sub_000525A0_gen();
    GMEM32(0x007654E0u) = saved_lo;
    GMEM32(0x007654E4u) = saved_hi;
}

/* German text and movies on the USA executable.
 *
 * The retail XBE has two language indices, both in the order of the
 * European disc: 0 English, 1 French, 2 Spanish, 3 Italian, 4 German.
 * (Proven: loading4.int shows "LADE...", the European SOUNDX4 is the German
 * speech, and the bonus films CINE_90n4 carry German subtitles.)
 * - 0x00758F24, set once by sub_00369310 from XGetLanguage. The USA build
 *   maps only English, French and Spanish (0..2). It picks the speech
 *   archive (sub_000D7C20), the Bink audio track group (sub_0001E5F0), the
 *   loading and logo films (sub_000CA460, sub_000CA680) and the uix font.
 * - 0x0044A9D8, the text language: copied from 0x00758F24 at start-up
 *   (sub_000152F0), changed by TEXT_ChangeLang sub_00069D50, and read by
 *   the text bank loader sub_00069660 for every text group. The AI reads it
 *   too (sub_000F8790), e.g. for the bonus film number (Cine_%04d).
 * 0x00758F24 stays as it is, so the speech set (tools/german-speech.py)
 * and the font are unchanged. Instead:
 * - RESOT_TEXT_LANGUAGE=german sets 0x0044A9D8 to 4 before each text bank.
 * - RESOT_MOVIE_LANGUAGE=german moves the movie calls to index 4:
 *   - the four-track group passed to BinkSetSoundTrack (sub_003A6570,
 *     called at 0x0001E736) and to BinkSetMixBins (sub_003A8340, called at
 *     0x0001E812, 0x0001E830, 0x0001E84E, 0x0001E87A). Only films with
 *     five language groups (20 tracks, e.g. CINE_010) are changed.
 *   - the %i of loading%i / pop_in%i / poplogo%i / pop_out%i in _sprintf
 *     (sub_00347456, called at 0x000CA4D9, 0x000CA6A5, 0x000CA6B5,
 *     0x000CA6C5). The USA disc already has all five sets.
 *   - the bonus films: Cine_%04d in sub_0002F450 (_sprintf called at
 *     0x0002F4AD) gets 90n0 + 0x0044A9D8. With English text, 90n0 becomes
 *     90n4 (German subtitles), when RESOT_GERMAN_BONUS says the European
 *     files are installed (port/windows/src/main.c). The overlay rectangle
 *     of sub_0001EA40 knows only 90n0..90n2, in the European XBE as well.
 * Other values (english, or unset) keep the original behaviour. */
static int resot_language_index(const char *name)
{
    const char *e = getenv(name);
    if (e && e[0] >= '0' && e[0] <= '4' && !e[1])
        return e[0] - '0';           /* a raw index, for tests */
    return e && (!strcmp(e, "german") || !strcmp(e, "de")) ? 4 : -1;
}

/* In the binarized retail data every world has its text in eight language
 * streams FDnxxxxx (n = language + 1; prince.bf holds all five languages, n
 * 1 English ... 5 German). sub_00069B10 (world text load, world key in ESI)
 * and TEXT_ChangeLang build the stream key from 0x0044A9D8 at 0x00069B25 /
 * 0x00069D80, open it (sub_000D8590) and read the text groups from it, so
 * the language must be set before that read, not in the bank loader. */
static int resot_text_language(void)
{
    static int lang = -2;
    if (lang == -2) {
        lang = resot_language_index("RESOT_TEXT_LANGUAGE");
        if (lang >= 0)
            fprintf(stderr, "[LANG] text language %d\n", lang);
    }
    return lang;
}

void sub_00069B10_enter(void)
{
    int lang = resot_text_language();
    if (lang >= 0)
        GMEM32(0x0044A9D8u) = (uint32_t)lang;
}

void sub_00069660_enter(void)
{
    static int diag = -1;
    if (diag < 0) {
        const char *e = getenv("RESOT_TEXT_DIAG");
        diag = e && *e && *e != '0';
    }
    if (diag)   /* RESOT_TEXT_DIAG=1: the text group's key */
        fprintf(stderr, "[LANG] text group %08X, language %u\n",
                GMEM32(g_esp + 4), GMEM32(0x0044A9D8u));
}

static int resot_movie_language(void)
{
    static int lang = -2;
    if (lang == -2) {
        lang = resot_language_index("RESOT_MOVIE_LANGUAGE");
        if (lang >= 0)
            fprintf(stderr, "[LANG] movie language %d (German)\n", lang);
    }
    return lang;
}

/* The movie's track ids from the original index to the requested one, when
 * the open Bink file (handle at 0x0044AA1C, track count at +0xF0) has the
 * requested group. */
static uint32_t resot_movie_track(uint32_t id)
{
    int lang = resot_movie_language();
    uint32_t bink = GMEM32(0x0044AA1Cu), from = GMEM32(0x00758F24u);
    if (lang < 0 || !bink || (int32_t)GMEM32(bink + 0xF0u) < 4 * (lang + 1)
        || id < 4 * from || id >= 4 * from + 4)
        return id;
    return id - 4 * from + 4 * (uint32_t)lang;
}

void sub_003A6570_enter(void)
{
    uint32_t list, i;
    if (GMEM32(g_esp) != 0x0001E73Bu || GMEM32(g_esp + 4) != 4)
        return;
    list = GMEM32(g_esp + 8);
    for (i = 0; i < 4; i++)
        GMEM32(list + 4 * i) = resot_movie_track(GMEM32(list + 4 * i));
    if (GMEM32(list) >= 4)
        fprintf(stderr, "[LANG] movie tracks %u-%u\n", GMEM32(list), GMEM32(list) + 3);
}

void sub_003A8340_enter(void)
{
    uint32_t ret = GMEM32(g_esp);
    if (ret == 0x0001E817u || ret == 0x0001E835u || ret == 0x0001E853u || ret == 0x0001E87Fu)
        GMEM32(g_esp + 8) = resot_movie_track(GMEM32(g_esp + 8));
}

void sub_00347456_enter(void)
{
    uint32_t ret = GMEM32(g_esp);
    int lang;
    if (ret == 0x0002F4B2u) {
        uint32_t n = GMEM32(g_esp + 12);
        const char *bonus = getenv("RESOT_GERMAN_BONUS");
        if (resot_movie_language() == 4 && bonus && *bonus == '1'
            && n >= 9010u && n <= 9069u && n % 10u == 0)
            GMEM32(g_esp + 12) = n + 4u;
        return;
    }
    if (ret != 0x000CA4DEu && ret != 0x000CA6AAu && ret != 0x000CA6BAu && ret != 0x000CA6CAu)
        return;
    lang = resot_movie_language();
    if (lang >= 0)
        GMEM32(g_esp + 12) = (uint32_t)lang;
}

/* Map change. The game changes "map" (a Jade world list, .wol) through
 * Mdl0000186A_fn_ChangeMapWithCheckPoint, sub_0025CF40 (cdecl: map key,
 * check point; -1 = the map's start). It stores both in the universe AI's
 * variables (object 0xCC00008E, +0x118) and switches to loading. The main
 * menu's message handler (MenuStartDemoE3__MSG) calls it at 0x0014330D for
 * the credits, map 0x0B00A848 (world list "MenuCredits"); New Game calls
 * it after the opening film (return 0x002B18E2) with 0x2200B526 (stream
 * FF02B526, "0101_Entree"). Logged, since the level select (below) uses it. */
void sub_0025CF40_enter(void)
{
    fprintf(stderr, "[LEVEL] change map %08X check point %d (ret %08X)\n",
            GMEM32(g_esp + 4), (int)GMEM32(g_esp + 8), GMEM32(g_esp));
}

/* Retail primary WOW merge at 0x00031A73: source in EAX, aggregate
 * destination at stack +4. This caller follows the primary key load at
 * 0x00031A58; dependency merges use other callers. */
extern void resot_scene_loaded(uint32_t destination, uint32_t source, int primary, uint32_t caller);
void sub_00030FB0_enter(void)
{
    uint32_t caller = GMEM32(g_esp);
    resot_scene_loaded(GMEM32(g_esp + 4), g_eax, caller == 0x00031A78u, caller);
}

/* Retail independently verified in its XBE: sub_0001E970 returns 0 at EOF;
 * caller 0x0002F5FC tests the loop flag 0x00758544 at 0x0002F629 and calls
 * sub_0001ED10 at 0x0002F63B to close Bink and release the overlay. Report
 * EOF for Space / pad A even when the movie's original skip permission or
 * minimum viewing time forbids skipping. The game's normal completion path
 * retains timeline bookkeeping and subsequent scene/movie transitions. */
extern int resot_movie_skip_down(void);
extern void sub_0001E970_gen(void);

void sub_0001E970(void)
{
    static int was_down;
    int down = resot_movie_skip_down();
    int pressed = down && !was_down;
    was_down = down;
    if (pressed && GMEM32(0x0044AA1Cu)) { /* Bink handle, read at 0x0001E970 */
        GMEM32(0x00758544u) = 0;         /* looping movies must also finish */
        fprintf(stderr, "[VIDEO] skipped movie (Space / A)\n");
        g_eax = 0;
        g_esp += 4;                     /* original status routine: ret */
        return;
    }
    sub_0001E970_gen();
}

/* ── Widescreen: Hor+, as in the demo (source/xbox-demo/target_hooks.c) ──
 *
 * The renderer stretches the 640-pixel picture over a wider surface
 * (RECOMP_HW_ASPECT); the projection's horizontal scale shrinks by the same
 * factor so the proportions stay right and more scene shows at the sides.
 *
 * Retail differs from the demo in where the projection lives. sub_000CFC10
 * (the demo's sub_000EF340: same fptan of the camera's field of view at
 * 0x000CFD14) builds it at 0x007682B0 (x scale +0x00, y scale +0x14), copies
 * it inline (rep movsd at 0x000CFE7F) to 0x00440C00 = 0x00440BA0 + 0x60 and
 * calls sub_000CD970 with esi = 0x00440BA0, returning to 0x000CFE8B. The demo
 * narrowed the source before its copy; here both copies are narrowed, which
 * leaves the same values in both places. */
#define PROJ_BUILT    0x007682B0u
#define PROJ_HOLDER   0x00440BA0u
#define PROJ_RET      0x000CFE8Bu

static float s_widen = -1.0f;

static float widen_factor(void)
{
    if (s_widen < 0.0f) {
        const char *e = getenv("RECOMP_HW_ASPECT"), *colon;
        float aspect = 4.0f / 3.0f;
        if (e && *e) {
            aspect = (float)atof(e);
            colon = strchr(e, ':');
            if (colon && atof(colon + 1) > 0.0)
                aspect /= (float)atof(colon + 1);
        }
        s_widen = (aspect >= 4.0f / 3.0f && aspect <= 4.0f) ? aspect / (4.0f / 3.0f) : 1.0f;
        /* RESOT_NO_FOV_HOOK=1: leave the projection alone (comparison runs). */
        if (getenv("RESOT_NO_FOV_HOOK"))
            s_widen = 1.0f;
        if (s_widen > 1.0f)
            fprintf(stderr, "[BOOT] widescreen: projection widened %.4fx horizontally\n",
                    s_widen);
    }
    return s_widen;
}

void sub_000CD970_enter(void)
{
    if (widen_factor() > 1.0f && GMEM32(g_esp) == PROJ_RET && g_esi == PROJ_HOLDER) {
        GMEMF(PROJ_BUILT) /= s_widen;
        GMEMF(PROJ_HOLDER + 0x60) /= s_widen;
    }
}

/* Culling planes. sub_002FD810 is the demo's sub_0008A500 (Jade's
 * CAM_Engine_ComputePlans, byte for byte apart from addresses): half the field
 * of view (cam +0x0C times the constant at 0x00429354), then the side planes
 * from the cosine sub_000452F0 (returning to 0x002FD835) and the sine
 * sub_00045340 (returning to 0x002FD85C); the up/down calls return to
 * 0x002FD87C and 0x002FD896 and keep the original angle. The side calls get
 * the widened half angle atan(widen * tan h), so what lies only in the new
 * side strips is not culled. */
static void widen_side_plane_angle(uint32_t ret_side)
{
    float h, *arg;

    if (widen_factor() <= 1.0f || GMEM32(g_esp) != ret_side)
        return;
    arg = (float *)((uintptr_t)g_xbox_mem_offset + g_esp + 4);
    h = *arg;
    if (h > 0.0f && h < 1.5f) {
        static int told;
        *arg = atanf(s_widen * tanf(h));
        if (!told++)
            fprintf(stderr, "[BOOT] widescreen: culling half-angle %.4f -> %.4f rad\n",
                    h, *arg);
    }
}

void sub_000452F0_enter(void)
{
    widen_side_plane_angle(0x002FD835u);
}

void sub_00045340_enter(void)
{
    widen_side_plane_angle(0x002FD85Cu);
}

/* Retail XDK KickOff at 0x0036FF60..0x0037000F and
 * 0x00370010..0x0037007A. These retain the retail stores/registers and
 * replace only the WC-flush spin with the host executor's wait/kick.
 * Retail differs from demo: device+0x770 (put), +0x1C20 (DMA),
 * +0x1C28 (NV registers), +0x2478 -> +0x1DE4 (saved put bookkeeping).
 * Busy query: 0x0036AE70; sync flag: 0x0037B1A0; dummy DMA: 0x0037B190.
 */
extern void xbox_Nv2aKick(void);
extern void xbox_Nv2aWaitClear(uint32_t va, uint32_t mask);
extern void sub_0036AE70(void);

#define D3D_DEVICE_GLOBAL  0x0037B648u   /* [dev] +0x1C28: NV register base */
#define NV_PFB_WC_FLUSH    0x00100410u
#define D3D_SYNC_FLAG      0x0037B1A0u   /* nonzero: wait for an idle GPU */

static void wait_gpu_idle(uint32_t ret)
{
    do {
        g_esp -= 4;
        GMEM32(g_esp) = ret;
        sub_0036AE70();                  /* eax != 0 while PGRAPH is busy */
    } while (g_eax);
}

void sub_0036FF60(void)
{
    uint32_t dev = g_ecx, flags, put, regs;

    g_esp -= 4;
    GMEM32(g_esp) = g_esi;               /* push esi */
    g_esi = dev;
    flags = GMEM32(dev + 8);
    g_eax = flags;
    put = (flags & 4) ? GMEM32(dev + 0x770) : GMEM32(dev);
    g_edx = put;
    if (flags & 0x2000) {                /* 0x0036FFD6 */
        g_eax = GMEM32(dev);
        g_ecx = 0x0037B190u - 0x40u;
        GMEM32(dev + 0x1C20) = g_ecx;
        g_eax &= 0x0FFFFFFFu;
        GMEM32(g_ecx + 0x40) = g_eax;
        g_edx = GMEM32(dev + 0x1C20);
        GMEM32(g_edx + 0x44) = g_eax;
        g_eax = GMEM32(dev + 0x2C) - 2;
        g_ecx = GMEM32(dev + 0x30);
        GMEM32(g_ecx) = g_eax;
        g_edx = GMEM32(dev + 0x2478);
        GMEM32(dev + 0x1DE4) = g_edx;
    } else {
        regs = GMEM32(GMEM32(D3D_DEVICE_GLOBAL) + 0x1C28);
        g_eax = regs;
        g_ecx = GMEM32(regs + NV_PFB_WC_FLUSH) | 0x10000u;
        GMEM32(regs + NV_PFB_WC_FLUSH) = g_ecx;
        xbox_Nv2aWaitClear(regs + NV_PFB_WC_FLUSH, 0x10000u);   /* 0x0036FFA0 */
        g_ecx = GMEM32(dev + 0x1C20);
        g_edx = put & 0x0FFFFFFFu;
        GMEM32(g_ecx + 0x40) = g_edx;    /* DMA_PUT */
        xbox_Nv2aKick();
        g_eax = GMEM32(D3D_SYNC_FLAG);
        if (g_eax) {
            wait_gpu_idle(0x0036FFC9u);
            GMEM32(dev + 8) |= 0x2000u;
        }
    }
    g_esi = GMEM32(g_esp);               /* pop esi */
    g_esp += 4;
    g_esp += 4;                          /* ret */
}

void sub_00370010(void)
{
    uint32_t dev = g_ecx, flags, put, regs;

    flags = GMEM32(dev + 8);
    g_eax = flags;
    g_esp -= 4;
    GMEM32(g_esp) = g_esi;               /* push esi */
    put = (flags & 4) ? GMEM32(dev + 0x770) : GMEM32(dev);
    g_esi = put;
    if (!(flags & 0x2000)) {
        regs = GMEM32(GMEM32(D3D_DEVICE_GLOBAL) + 0x1C28);
        g_eax = regs;
        g_edx = GMEM32(regs + NV_PFB_WC_FLUSH) | 0x10000u;
        GMEM32(regs + NV_PFB_WC_FLUSH) = g_edx;
        xbox_Nv2aWaitClear(regs + NV_PFB_WC_FLUSH, 0x10000u);   /* 0x00370050 */
        g_ecx = GMEM32(dev + 0x1C20);
        g_esi = put & 0x0FFFFFFFu;
        GMEM32(g_ecx + 0x40) = g_esi;    /* DMA_PUT */
        xbox_Nv2aKick();
        wait_gpu_idle(0x00370075u);
    }
    g_esi = GMEM32(g_esp);               /* pop esi */
    g_esp += 4;
    g_esp += 4;                          /* ret */
}

extern uint32_t xbox_HlePadState(uint32_t port, uint8_t *out);
extern void     xbox_HlePadRumble(uint32_t port, uint16_t left, uint16_t right);

extern void sub_0041172B_gen(void);
extern void sub_0041174D_gen(void);
extern void sub_00417D4B_gen(void);
extern void sub_00417DA1_gen(void);
extern void sub_00417DAD_gen(void);
extern void sub_00417F85_gen(void);
extern void sub_00417FF8_gen(void);

#define PAD_HANDLE 0x52455330u             /* "RES0": port 0's handle */

static int usb_input(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_USB");
        on = e && *e && *e != '0';
        if (!on)
            fprintf(stderr, "[BOOT] input: XAPI pad on port 0 from keyboard, mouse,"
                            " XInput pad or joystick (no USB)\n");
    }
    return on;
}

static uint32_t stdcall_arg(int i)
{
    return GMEM32(g_esp + 4 + 4 * (uint32_t)i);
}

static void stdcall_return(uint32_t value, int nargs)
{
    g_eax = value;
    g_esp += 4 + 4 * (uint32_t)nargs;
}

/* XGetDevices */
void sub_0041172B(void)
{
    if (usb_input()) { sub_0041172B_gen(); return; }
    stdcall_return(1u, 1);                 /* a gamepad on port 0 */
}

/* XGetDeviceChanges */
void sub_0041174D(void)
{
    uint32_t ins, rem;
    if (usb_input()) { sub_0041174D_gen(); return; }
    ins = stdcall_arg(1);
    rem = stdcall_arg(2);
    if (ins) GMEM32(ins) = 0;              /* nothing comes or goes */
    if (rem) GMEM32(rem) = 0;
    stdcall_return(0, 3);
}

/* XInputOpen */
void sub_00417D4B(void)
{
    if (usb_input()) { sub_00417D4B_gen(); return; }
    stdcall_return(stdcall_arg(1) == 0 ? PAD_HANDLE : 0, 4);
}

/* XInputClose */
void sub_00417DA1(void)
{
    if (usb_input()) { sub_00417DA1_gen(); return; }
    stdcall_return(0, 1);
}

/* XInputGetCapabilities */
void sub_00417DAD(void)
{
    uint32_t caps, i;
    if (usb_input()) { sub_00417DAD_gen(); return; }
    caps = stdcall_arg(1);
    if (stdcall_arg(0) != PAD_HANDLE || !caps) {
        stdcall_return(1167, 2);           /* ERROR_DEVICE_NOT_CONNECTED */
        return;
    }
    /* SubType, a reserved word, the gamepad's supported controls, rumble. */
    for (i = 0; i < 25; i++)
        *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + caps + i) = 0xFF;
    *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + caps) = 1;   /* GC_GAMEPAD */
    *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + caps + 1) = 0;
    *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + caps + 2) = 0;
    stdcall_return(0, 2);
}

/* XInputGetState, polled once a frame on the title thread: also where the
 * reSOT Options labels catch up with a changed setting. */
static void native_refresh(void);
static void level_pending(void);
static void cheats_tick(void);
static void freecam_pad(uint8_t *state);

void sub_00417F85(void)
{
    uint32_t st;
    native_refresh();
    level_pending();
    cheats_tick();
    if (usb_input()) { sub_00417F85_gen(); return; }
    st = stdcall_arg(1);
    if (stdcall_arg(0) != PAD_HANDLE || !st) {
        stdcall_return(1167, 2);
        return;
    }
    {
        uint32_t r = xbox_HlePadState(0, (uint8_t *)((uintptr_t)g_xbox_mem_offset + st));
        if (r == 0)
            freecam_pad((uint8_t *)((uintptr_t)g_xbox_mem_offset + st));
        stdcall_return(r, 2);
    }
}

/* XInputSetState */
void sub_00417FF8(void)
{
    uint32_t fb;
    if (usb_input()) { sub_00417FF8_gen(); return; }
    fb = stdcall_arg(1);
    if (stdcall_arg(0) != PAD_HANDLE || !fb) {
        stdcall_return(1167, 2);
        return;
    }
    /* XINPUT_FEEDBACK: the header's dwStatus, then the rumble at +0x42. The
     * game sends again once dwStatus has left ERROR_IO_PENDING
     * (retail call at 0x000DC2AC), so the request completes at once. */
    xbox_HlePadRumble(0, *(volatile uint16_t *)((uintptr_t)g_xbox_mem_offset + fb + 0x42),
                      *(volatile uint16_t *)((uintptr_t)g_xbox_mem_offset + fb + 0x44));
    GMEM32(fb) = 0;
    stdcall_return(997, 2);                /* ERROR_IO_PENDING, already done */
}

/* ── Menu construction trace (RESOT_MENU_TRACE=1) ──
 *
 * The retail XBE's Jade menu library, matched to the demo's routine by
 * routine and checked in this XBE's code: 0x002A2B20 opens a page (eax =
 * current menu from 0x00758D6C, ecx = &title reference, edx = menu object,
 * xmm1 = column x, stored at +0x524; cdecl out, first line, top, step,
 * flags), 0x002A3910 adds a choice (cdecl out, menu, &text reference),
 * 0x002A2670 closes the page. 0x000F84A0 resolves a text reference
 * (edi = &{group key, index}) to a string. 0x00018310 builds a message
 * action, 0x00116660 binds an action to a choice. Logged with their return
 * addresses and resolved labels to map the retail menus. */
extern RESOT_TLS uint32_t g_ebx, g_edi, g_ebp, g_seh_ebp;
extern void sub_000F84A0(void);

typedef struct { uint32_t eax, ebx, ecx, edx, esi, edi, ebp, seh, esp; } GuestRegs;
static void regs_save(GuestRegs *r)
{
    r->eax = g_eax; r->ebx = g_ebx; r->ecx = g_ecx; r->edx = g_edx; r->esi = g_esi;
    r->edi = g_edi; r->ebp = g_ebp; r->seh = g_seh_ebp; r->esp = g_esp;
}
static void regs_load(const GuestRegs *r)
{
    g_eax = r->eax; g_ebx = r->ebx; g_ecx = r->ecx; g_edx = r->edx; g_esi = r->esi;
    g_edi = r->edi; g_ebp = r->ebp; g_seh_ebp = r->seh; g_esp = r->esp;
}

static int menu_trace(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("RESOT_MENU_TRACE"); on = e && *e && *e != '0'; }
    return on;
}

/* The game's string for a text reference in guest memory, or "". */
static void menu_text(uint32_t ref, char *out, unsigned size)
{
    GuestRegs saved;
    uint32_t s;
    unsigned i;
    out[0] = 0;
    if (!ref || GMEM32(ref) == 0xFFFFFFFFu)
        return;
    regs_save(&saved);
    g_edi = ref;
    g_esp -= 4; GMEM32(g_esp) = 0;
    sub_000F84A0();
    s = g_eax;
    regs_load(&saved);
    for (i = 0; s && i + 1 < size; i++) {
        char c = *(volatile char *)((uintptr_t)g_xbox_mem_offset + s + i);
        if (!c) break;
        out[i] = (c >= 32 && c < 127) ? c : '?';
    }
    out[s ? i : 0] = 0;
}

void sub_00116660_enter(void)
{
    if (menu_trace())
        fprintf(stderr, "[MENU] bind       ret=%08X action=%08X choice=%08X edx=%08X ebx=%08X\n",
                GMEM32(g_esp), GMEM32(g_esp + 8), GMEM32(g_esp + 12), g_edx, g_ebx);
}

void sub_000182A0_enter(void)
{
    if (menu_trace())
        fprintf(stderr, "[MENU] open-page  ret=%08X page=%08X\n", GMEM32(g_esp), g_ecx);
}

void sub_00018310_enter(void)
{
    if (menu_trace()) {
        uint32_t d = GMEM32(g_esp + 8);
        fprintf(stderr, "[MENU] message    ret=%08X kind=%d param=%08X id=%u\n",
                GMEM32(g_esp), (int)GMEM32(d + 0x28), GMEM32(d + 0x2C), GMEM32(d + 0x30));
    }
}

/* RESOT_TEXT_DUMP=<group>[,<group>...]: print every string of those text
 * groups once, at the first page the menu library opens (with the trace). */
static void text_dump(void)
{
    static int done;
    const char *e = getenv("RESOT_TEXT_DUMP");
    if (done || !e || !*e) return;
    done = 1;
    while (*e) {
        char *next;
        uint32_t group = (uint32_t)strtoul(e, &next, 16), i, empty = 0;
        GuestRegs saved;
        if (next == e) break;
        e = next;
        regs_save(&saved);
        g_esp -= 0x20;
        for (i = 0; i < 400 && empty < 8; i++) {
            char t[120];
            GMEM32(g_esp + 0x10) = group;
            GMEM32(g_esp + 0x14) = i;
            menu_text(g_esp + 0x10, t, sizeof t);
            empty = t[0] ? 0 : empty + 1;
            if (t[0])
                fprintf(stderr, "[TEXT] %08X:%u \"%s\"\n", group, i, t);
        }
        regs_load(&saved);
        while (*e == ',' || *e == ' ') e++;
    }
}

/* ── reSOT Options: native pages for the host settings (retail) ──
 *
 * The demo's design (source/xbox-demo/target_hooks.c), with this XBE's own
 * routines and conventions, each checked here:
 *   page open  0x002A2B20  eax = current menu, ecx = &title reference,
 *                          edx = menu object, xmm1 = column x (+0x524; the
 *                          game's pages pass 0.4 from 0x00429478); cdecl
 *                          (out, first line, top, step, flags). Flags 5 as
 *                          the Options sub-pages (Sound at 0x001A3564).
 *   add choice 0x002A3910  eax = current menu; cdecl (out, menu, &reference)
 *   close      0x002A2670  eax = current menu; cdecl (menu)
 *   open page  0x000182A0  ecx = page; cdecl (current, 0, 1) -> action
 *   message    0x00018310  cdecl (current, &descriptor) -> action
 *   bind       0x00116660  eax = current, edx = menu object; cdecl
 *                          (out, action, choice) (0x001A17CE)
 *   execute    0x00039B20  message action, vtable 0x0041F888 slot 1; the
 *                          descriptor copy at +0x24, as the demo's 0x00061470
 *   text       0x000F84A0  edi = &{group, index} -> string; "no text" is
 *                          0x0041F4A8
 *   refresh    0x00038DD0  ecx = text object, eax = 1: re-reads the text
 *                          (0x00038190 asks the text action only when its
 *                          flag argument is set, as 0x00038FE4 does)
 * Text objects: manager 0x007584F0 (+0x44 objects by handle, the choice's
 * text object at +0x9C), text action vtable 0x0041F838 (slot 1 0x00038760,
 * reference at +0x34/+0x38).
 *
 * The main menu (init 0x002345D0) opens its page at 0x00234B3D (Start New
 * Game, Load Saved Game, Options, Xbox Live, Extra Features), with eax =
 * [0x00767530], and closes it at 0x00234D86. "reSOT Options" is added there
 * before the close; the pages are built when the next page opens
 * (0x00234DC8, return 0x00234DCD), and again for each new main menu.
 * RESOT_HIDDEN_MENUS=0 disables them. */
#include "../../port/windows/src/options.h"
extern void sub_000DBDE0_gen(void);
/* Retail XBE: 0x000DBDE0 normalizes ECX's int16 stick pair into EAX's
 * float[2]. Its callers return to 0x000DC0B3 (left) and 0x000DC0C2 (right,
 * ESI-180). Keep the title's radial deadzone and scale only the latter
 * float result: a 200% full keyboard/controller/touch look then reaches 2.0. */
void sub_000DBDE0(void)
{
    uint32_t output=g_eax, ret=GMEM32(g_esp);
    sub_000DBDE0_gen();
    if(ret==0x000DC0C2u)
        resot_scale_look((float *)((uintptr_t)g_xbox_mem_offset+output));
}
extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
extern void sub_000F84A0_gen(void);
extern void sub_00039B20_gen(void);
extern void sub_002A2B20(void);
extern void sub_002A3910(void);
extern void sub_002A2670(void);
extern void sub_000182A0(void);
extern void sub_00018310(void);
extern void sub_00116660(void);
extern void sub_00038DD0(void);

typedef union { float f[4]; double d[2]; uint32_t u[4]; int32_t i[4]; uint64_t q[2]; } GuestXmm;
extern RESOT_TLS GuestXmm g_xmm1;          /* the runtime's RecompXmm */

#define MAIN_OPEN_RET      0x00234B42u
#define MAIN_CLOSE_RET     0x00234D8Bu
#define MAIN_NEXT_OPEN_RET 0x00234DCDu
#define TEXT_MANAGER       0x007584F0u
#define TEXT_ACTION_VTABLE 0x0041F838u
#define TEXT_NONE          0x0041F4A8u
#define MENU_PAGE_X        0.4f
#define RESOT_TEXT_GROUP   0x0B00FE00u   /* no key of the retail BF: injected */
#define RESOT_MSG_KIND     0x52E5u
#define RESOT_TEXT_SLOT    96u
#define RESOT_TEXTS        170u

static int hidden_menus(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("RESOT_HIDDEN_MENUS"); on = !e || *e != '0'; }
    return on;
}

/* cdecl guest call; generated routines pop their own return address. */
static uint32_t guest_cdecl(void (*fn)(void), const uint32_t *args, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) { g_esp -= 4; GMEM32(g_esp) = args[i]; }
    g_esp -= 4; GMEM32(g_esp) = 0;
    fn();
    g_esp += 4u * (uint32_t)n;
    return g_eax;
}

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

enum { T_ROOT, T_GRAPHICS, T_CONTROLS, T_KEY0, T_KEY1, T_KEY2, T_JOY0, T_JOY1, T_JOY2,
       T_RESET, T_GFX = 10, T_LOOK = 15, T_KEYS = 20, T_JOYS = 50,
       T_LEVELS = 80, T_PART = 81, T_LV = 88, T_CHEATS = 140, T_CHEAT = 141,
       T_DEBUG = 150, T_DBG = 151, T_PROTO = 160, T_PRO = 161, T_SOUND = 165, T_SND = 166 };
static const char *const s_static_texts[] = {
    "reSOT Options", "Graphics", "Controls",
    "Keyboard: move and fight", "Keyboard: actions", "Keyboard: menus and camera",
    "Joystick: move and fight", "Joystick: actions", "Joystick: menus and camera",
    "Reset all bindings",
};
/* RE-SoT: the same pages in German when the game's text is German
 * (RESOT_TEXT_LANGUAGE), in the font's Latin-1. */
static const char *const s_static_texts_de[] = {
    "reSOT-Optionen", "Grafik", "Steuerung",
    "Tastatur: Bewegen und K" "\xE4" "mpfen", "Tastatur: Aktionen", "Tastatur: Men" "\xFC" "s und Kamera",
    "Controller: Bewegen und K" "\xE4" "mpfen", "Controller: Aktionen", "Controller: Men" "\xFC" "s und Kamera",
    "Alle Belegungen zur" "\xFC" "cksetzen",
};
static uint32_t s_text_va;

/* RE-SoT: level select ("Special Load Map"), after popsot-fixes' Special
 * Load entry (P_SpecialLoad, docs/TECHNICAL.md:188), which the PC main menu
 * still carries hidden; the retail XBE keeps only the caption (text group
 * 0x0B0041D2, index 8) and no page. Here it is a native page set: one world
 * list per area of the game, the first room of each, and the hidden ones
 * (the POP1 bonus, the developers' WhiteRoom test room, the magic corridor
 * and fountain worlds, the credits map). A world list's map key is
 * 0x0T00LLLL for the bigfile stream FF0TLLLL (the credits: FF0BA848 holds
 * "MenuCredits", key 0x0B00A848); the room is the world list's last world
 * (read from Assets/xbox-retail/prince.bf). */
typedef struct { uint32_t stream; const char *room; } Level;
static const Level s_levels[] = {
    { 0xFF02B526u, "0101_Entree" },          { 0xFF02B676u, "0201_Statue" },
    { 0xFF0D138Eu, "1101_Hall" },            { 0xFF0D06E2u, "1102_Corridor01" },
    { 0xFF0D0A1Eu, "1201_CorridorStart" },   { 0xFF0D0D5Bu, "1301_Lobby" },
    { 0xFF0D0F5Eu, "1401_CorridorStart" },   { 0xFF0B4DDDu, "1501_ReceptionHall" },
    { 0xFF0D24F8u, "2101_Start" },           { 0xFF02BE15u, "2104_couloir1" },
    { 0xFF0B5C22u, "2201_Aviary" },          { 0xFF0B4EA6u, "2203_RopeBridges" },

    { 0xFF0D6474u, "2401_CorridorStart" },   { 0xFF012246u, "2402_Central" },
    { 0xFF012249u, "2403_Cavern" },          { 0xFF0A0E1Bu, "2501_TowerInsideA" },
    { 0xFF0F3E8Bu, "2601_Bridge" },          { 0xFF012456u, "3101_Tower" },
    { 0xFF02AC02u, "3201_Grotte" },          { 0xFF01298Au, "3301_Transition" },
    { 0xFF0152C9u, "3401_Balcony" },         { 0xFF0B64B5u, "3501_HaremAccess" },
    { 0xFF0B5131u, "5102_GreatHall" },       { 0xFF0B5136u, "5104_Observatory" },

    { 0xFF023E88u, "6101_couloir" },         { 0xFF02A3C6u, "6102_Rempart" },
    { 0xFF02BBF5u, "6201_PrisonA" },         { 0xFF02A642u, "7201_Cour" },
    { 0xFF0F42DEu, "7202_TowerOfDawn" },     { 0xFF0B569Eu, "7301_GroundFloor" },
    { 0xFF0B59ECu, "7401_HourglassRoom" },   { 0xFF02BCA5u, "7601_CouloirBas" },
    { 0xFF01268Au, "8101_TowerBase" },       { 0xFF01268Eu, "8102_TowerEntrance" },
    { 0xFF0F4E2Cu, "8201_UpperHighLevel" },  { 0xFF0B66FBu, "9101_FarahRoom" },

    { 0xFF0124B6u, "POP1" },                 { 0xFF0B784Au, "WhiteRoom" },
    { 0xFF000865u, "magic_corridor" },       { 0xFF0003DCu, "Magic_Fountain" },
    { 0xFF0BA848u, "MenuCredits" },
};
#define LEVELS ((int)(sizeof s_levels / sizeof s_levels[0]))
#define LEVEL_PARTS 4
static const int s_part_first[LEVEL_PARTS + 1] = { 0, 12, 24, 36, LEVELS };
static const char *const s_part_names[LEVEL_PARTS] = {
    "Areas 01-22", "Areas 24-51", "Areas 61-91", "Extras" };
static const char *const s_part_names_de[LEVEL_PARTS] = {
    "Abschnitte 01-22", "Abschnitte 24-51", "Abschnitte 61-91", "Extras" };
static uint32_t s_level_pending;

/* RE-SoT: cheats (below), shown on their own reSOT page; after the
 * switches the page has the sword change (item CHEATS). */
enum { CHEAT_INVULNERABLE, CHEAT_SAND, CHEAT_ONE_HIT, CHEAT_NO_SPAWN, CHEAT_ENDLESS, CHEATS };
static int s_cheat[CHEATS];
static long s_cheat_revision;
static int s_sword_pending;
static const char *const s_cheat_names[CHEATS] = {
    "Invulnerable", "Infinite sand", "One-hit kills", "No enemies (prototype)",
    "Endless enemies (prototype)" };
static const char *const s_cheat_names_de[CHEATS] = {
    "Unverwundbar", "Unendlich Sand", "Ein Treffer t" "\xF6" "tet", "Keine Gegner (Prototyp)",
    "Endlose Gegner (Prototyp)" };
static void cheat_sword_label(char *out, unsigned size);

/* RE-SoT: the reSOT Prototype page (below, after the cheats). */
enum { PRO_REGEN, PRO_NOHUD, PROS };
static int s_proto[PROS];
static const char *const s_proto_names[PROS] = {
    "Life regeneration (prototype)", "Hide interface" };
static const char *const s_proto_names_de[PROS] = {
    "Lebensregeneration (Prototyp)", "Interface ausblenden" };

/* RE-SoT: the reSOT Debug page (below, after the free camera). */
enum { DBG_PANEL, DBG_FREECAM, DBG_FREEZE, DBG_SPEED, DBG_MENUCAM, DBGS };
static void debug_label(int item, char *out, unsigned size);
static void debug_activate(int item);
static long debug_revision(void);

static void resot_text(uint32_t index, char *out, unsigned size)
{
    /* RESOT_FONT_PROBE=<first>,<count>: the main menu's reSOT entry shows the
    * character codes first..first+count-1 instead, so a frame trace with
    * RECOMP_TRACE_ALLVTX gives every glyph's rectangle (tools/hd-ui.py). */
    const char *probe = getenv("RESOT_FONT_PROBE");
    if (index == T_ROOT && probe && *probe) {
        unsigned first = (unsigned)strtoul(probe, NULL, 0), count = 0, i;
        const char *comma = strchr(probe, ',');
        const char *sep = comma ? strchr(comma + 1, ',') : NULL;
        unsigned n = 0;
        if (comma) count = (unsigned)strtoul(comma + 1, NULL, 0);
        /* ,<sep>: the separator code before every character, so a missing
         * glyph shows as two separators in a row */
        for (i = 0; i < count && n + 2 < size && first + i < 256; i++) {
            if (sep) out[n++] = (char)strtoul(sep + 1, NULL, 0);
            out[n++] = (char)(first + i);
        }
        if (sep && n + 1 < size) out[n++] = (char)strtoul(sep + 1, NULL, 0);
        out[n] = 0;
        return;
    }
    if (index < sizeof s_static_texts / sizeof s_static_texts[0])
        snprintf(out, size, "%s", resot_text_language() == 4 ? s_static_texts_de[index]
                                                             : s_static_texts[index]);
    else if (index >= T_GFX && index < T_GFX + RESOT_NATIVE_GRAPHICS_ITEMS)
        resot_native_label(RESOT_NATIVE_GRAPHICS, (int)(index - T_GFX), out, size);
    else if (index == T_LOOK)
        resot_native_label(RESOT_NATIVE_CONTROLS, 0, out, size);
    else if (index >= T_KEYS && index < T_KEYS + RESOT_NATIVE_ACTIONS)
        resot_native_label(RESOT_NATIVE_KEYS, (int)(index - T_KEYS), out, size);
    else if (index >= T_JOYS && index < T_JOYS + RESOT_NATIVE_ACTIONS)
        resot_native_label(RESOT_NATIVE_JOY, (int)(index - T_JOYS), out, size);
    else if (index == T_LEVELS)
        snprintf(out, size, "%s", resot_text_language() == 4 ? "Spezielle Karte laden"
                                                             : "Special Load Map");
    else if (index >= T_PART && index < T_PART + LEVEL_PARTS)
        snprintf(out, size, "%s", resot_text_language() == 4 ? s_part_names_de[index - T_PART]
                                                             : s_part_names[index - T_PART]);
    else if (index == T_CHEATS)
        snprintf(out, size, "Cheats");
    else if (index >= T_CHEAT && index < T_CHEAT + CHEATS) {
        int de = resot_text_language() == 4, c = (int)(index - T_CHEAT);
        snprintf(out, size, "%s: %s", de ? s_cheat_names_de[c] : s_cheat_names[c],
                 s_cheat[c] ? (de ? "An" : "On") : (de ? "Aus" : "Off"));
    } else if (index == T_CHEAT + CHEATS)
        cheat_sword_label(out, size);
    else if (index == T_DEBUG)
        snprintf(out, size, "Debug");
    else if (index == T_SOUND)
        snprintf(out, size, resot_text_language() == 4 ? "Ton" : "Sound");
#ifdef _WIN32
    else if (index >= T_SND && index < T_SND + 2)
        resot_options_audio_label((int)(index - T_SND), out, size);
#endif
    else if (index == T_PROTO)
        snprintf(out, size, resot_text_language() == 4 ? "Prototyp" : "Prototype");
    else if (index >= T_PRO && index < T_PRO + PROS) {
        int de = resot_text_language() == 4, c = (int)(index - T_PRO);
        snprintf(out, size, "%s: %s", de ? s_proto_names_de[c] : s_proto_names[c],
                 s_proto[c] ? (de ? "An" : "On") : (de ? "Aus" : "Off"));
    }
    else if (index >= T_DBG && index < T_DBG + DBGS)
        debug_label((int)(index - T_DBG), out, size);
    else if (index >= T_LV && index < T_LV + (uint32_t)LEVELS) {
        char *c;
        snprintf(out, size, "%s", s_levels[index - T_LV].room);
        for (c = out; *c; c++)
            if (*c == '_') *c = ' ';
    } else
        snprintf(out, size, "?");
}

/* References to RESOT_TEXT_GROUP are answered from a guest buffer that is
 * rewritten at every lookup, so a label shows the setting's current value. */
/* RE-SoT: fixes of the German hint text (RESOT_TEXT_FIXES=0 keeps it).
 * The German text bank's verb entries for \d4\ \d5\ \d6\ are "Tippt",
 * "Ziehen", "ziehen" (FD5003DA and every other world's FD5 stream), while
 * the sentences around them want the Ihr form of "press": "Lauft auf die
 * Ecke zu und \d6\ \p5\A", "Ihr \d6\et \p5\A zu frueh" (-> "ziehenet"),
 * "\d5\ \p5\C, um das Schwert zu ziehen". They become "Drueckt" /
 * "drueckt", the same length. Fifteen hints also put
 * the button in brackets ("\d5\ [\p5\C] zum Angriff"; the English text has
 * none); the brackets are removed. Both in guest RAM, right after the
 * world's text is loaded (sub_00069B10) or the language changed
 * (sub_00069D50); user report 2026-10-25. */
static int text_fixes(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("RESOT_TEXT_FIXES"); on = !(e && *e == '0'); }
    return on;
}

/* One committed range of guest memory [lo, hi). */
static void text_fix_range(uint32_t lo, uint32_t hi, unsigned *verbs, unsigned *buttons)
{
    static const char want[] = "\0Tippt\0Ziehen\0ziehen\0";
    static const char fixed[] = "Tippt\0Dr\xFC" "ckt\0dr\xFC" "ckt";
    uint8_t *ram = (uint8_t *)(uintptr_t)g_xbox_mem_offset;
    uint32_t a, e;
    for (a = lo + 1; a + sizeof want < hi; a++) {
        const uint8_t *q = memchr(ram + a, 'T', hi - a - sizeof want);
        if (!q) break;
        a = (uint32_t)(q - ram);
        if (!memcmp(ram + a - 1, want, sizeof want - 1)) {
            memcpy(ram + a, fixed, sizeof fixed - 1);
            (*verbs)++;
        }
    }
    for (a = lo; a + 8 < hi; a++) {                   /* "[\p5\X]" -> "\p5\X" */
        const uint8_t *q = memchr(ram + a, '[', hi - a - 8);
        if (!q) break;
        a = (uint32_t)(q - ram);
        if (ram[a + 1] != '\\' || ram[a + 2] != 'p' || ram[a + 4] != '\\' || ram[a + 6] != ']') continue;
        for (e = a + 7; e < hi - 1 && ram[e]; e++) ;
        memmove(ram + a, ram + a + 1, 5);                     /* \p5\X */
        memmove(ram + a + 5, ram + a + 7, e - (a + 7) + 1);   /* the rest, with its NUL */
        ram[e - 1] = 0;
        (*buttons)++;
    }
}

extern int guest_vmem_accessible(uint32_t address, uint32_t bytes);

/* After a world's text (or a language change) is loaded: the verbs and the
 * bracketed buttons, wherever the banks lie: the first 64 MB, then every
 * committed range of the extended guest heap above it. */
static void text_fix_ram(void)
{
    unsigned verbs = 0, buttons = 0;
    uint32_t va, run = 0;
    if (resot_text_language() != 4 || !text_fixes()) return;
    text_fix_range(0x10000u, 64u << 20, &verbs, &buttons);
    for (va = 64u << 20; va && va < ~0xFFFFu; va += 0x1000u) {
        int ok = guest_vmem_accessible(va, 0x1000u);
        if (ok && !run) run = va;
        if (!ok && run) { text_fix_range(run, va, &verbs, &buttons); run = 0; }
    }
    if (run) text_fix_range(run, va, &verbs, &buttons);
    fprintf(stderr, "[LANG] German hint fixes: %u verb tables, %u bracketed buttons\n", verbs, buttons);
}

extern void sub_00069B10_gen(void);
extern void sub_00069D50_gen(void);
void sub_00069B10(void)
{
    sub_00069B10_gen();
    text_fix_ram();
}
void sub_00069D50(void)
{
    sub_00069D50_gen();
    text_fix_ram();
}

void sub_000F84A0(void)
{
    uint32_t index;
    char text[RESOT_TEXT_SLOT];
    if (GMEM32(g_edi) != RESOT_TEXT_GROUP) {
        sub_000F84A0_gen();
        return;
    }
    index = GMEM32(g_edi + 4);
    if (!s_text_va)
        s_text_va = xbox_ContiguousAlloc(RESOT_TEXT_SLOT * RESOT_TEXTS, 16);
    if (!s_text_va || index >= RESOT_TEXTS) {
        g_eax = TEXT_NONE;
    } else {
        resot_text(index, text, sizeof text);
        memcpy((void *)((uintptr_t)g_xbox_mem_offset + s_text_va + index * RESOT_TEXT_SLOT),
               text, strlen(text) + 1);
        g_eax = s_text_va + index * RESOT_TEXT_SLOT;
    }
    g_esp += 4;                           /* ret */
}

enum { DO_OPEN, DO_MSG };
typedef struct { uint32_t text; int what; int arg; uint32_t choice; } NativeChoice;
typedef struct { uint32_t title; float top, step; int n; NativeChoice c[12]; uint32_t page; } NativePage;
enum { P_ROOT, P_GFX, P_CTRL, P_K0, P_K1, P_K2, P_J0, P_J1, P_J2, P_SOUND, P_CHEATS, P_PROTO, P_DEBUG,
       P_LEVELS, P_LV0, P_LV1, P_LV2, P_LV3, P_PAGES };
#define MSG_ID(page, item) ((uint32_t)(page) << 8 | (uint32_t)(item))
#define MSG_RESET 0xFF00u
static NativePage s_pages[P_PAGES];
static const int s_bind_first[3] = { 0, 9, 16 }, s_bind_count[3] = { 9, 7, 9 };
static uint32_t s_main_menu, s_main_cur, s_root_choice, s_level_choice;
static int s_native_built, s_native_bound;

static void native_choice(NativePage *p, int i, uint32_t text, int what, int arg)
{
    p->c[i].text = text; p->c[i].what = what; p->c[i].arg = arg; p->c[i].choice = 0;
}

static void native_layout(void)
{
    int i, j;
    NativePage *p;
    memset(s_pages, 0, sizeof s_pages);
    p = &s_pages[P_ROOT]; p->title = T_ROOT; p->top = 0.3833f; p->step = 0.0667f; p->n = 0;
    native_choice(p, p->n++, T_GRAPHICS, DO_OPEN, P_GFX);
#ifdef _WIN32
    native_choice(p, p->n++, T_SOUND, DO_OPEN, P_SOUND);
#endif
    native_choice(p, p->n++, T_CONTROLS, DO_OPEN, P_CTRL);
    native_choice(p, p->n++, T_CHEATS, DO_OPEN, P_CHEATS);
    native_choice(p, p->n++, T_PROTO, DO_OPEN, P_PROTO);
    native_choice(p, p->n++, T_DEBUG, DO_OPEN, P_DEBUG);
#ifdef _WIN32
    p = &s_pages[P_SOUND]; p->title = T_SOUND; p->top = 0.3833f; p->step = 0.0667f; p->n = 2;
    for (i = 0; i < p->n; i++)
        native_choice(p, i, T_SND + (uint32_t)i, DO_MSG, (int)MSG_ID(P_SOUND, i));
#endif
    p = &s_pages[P_PROTO]; p->title = T_PROTO; p->top = 0.3833f; p->step = 0.0667f; p->n = PROS;
    for (i = 0; i < p->n; i++)
        native_choice(p, i, T_PRO + (uint32_t)i, DO_MSG, (int)MSG_ID(P_PROTO, i));
    p = &s_pages[P_CHEATS]; p->title = T_CHEATS; p->top = 0.3f; p->step = 0.0667f;
    p->n = CHEATS + 1;
    for (i = 0; i < p->n; i++)
        native_choice(p, i, T_CHEAT + (uint32_t)i, DO_MSG, (int)MSG_ID(P_CHEATS, i));
    p = &s_pages[P_DEBUG]; p->title = T_DEBUG; p->top = 0.3833f; p->step = 0.0667f; p->n = DBGS;
    for (i = 0; i < p->n; i++)
        native_choice(p, i, T_DBG + (uint32_t)i, DO_MSG, (int)MSG_ID(P_DEBUG, i));
    p = &s_pages[P_GFX]; p->title = T_GRAPHICS; p->top = 0.3833f; p->step = 0.0667f;
    p->n = RESOT_NATIVE_GRAPHICS_ITEMS;
    for (i = 0; i < p->n; i++)
        native_choice(p, i, T_GFX + (uint32_t)i, DO_MSG, (int)MSG_ID(P_GFX, i));
    p = &s_pages[P_CTRL]; p->title = T_CONTROLS; p->top = 0.3f; p->step = 0.05f; p->n = 8;
    native_choice(p, 0, T_LOOK, DO_MSG, (int)MSG_ID(P_CTRL, 0));
    for (i = 0; i < 6; i++)
        native_choice(p, i + 1, T_KEY0 + (uint32_t)i, DO_OPEN, P_K0 + i);
    native_choice(p, 7, T_RESET, DO_MSG, (int)MSG_RESET);
    for (i = 0; i < 6; i++) {
        int joy = i >= 3, part = i % 3;
        p = &s_pages[P_K0 + i];
        p->title = T_KEY0 + (uint32_t)i; p->top = 0.26f; p->step = 0.05f;
        p->n = s_bind_count[part];
        for (j = 0; j < p->n; j++) {
            int action = s_bind_first[part] + j;
            native_choice(p, j, (joy ? T_JOYS : T_KEYS) + (uint32_t)action, DO_MSG,
                          (int)MSG_ID(P_K0 + i, action));
        }
    }
    p = &s_pages[P_LEVELS]; p->title = T_LEVELS; p->top = 0.3833f; p->step = 0.0667f;
    p->n = LEVEL_PARTS;
    for (i = 0; i < LEVEL_PARTS; i++) {
        NativePage *q = &s_pages[P_LV0 + i];
        native_choice(p, i, T_PART + (uint32_t)i, DO_OPEN, P_LV0 + i);
        q->title = T_PART + (uint32_t)i; q->top = 0.18f; q->step = 0.04f;
        q->n = s_part_first[i + 1] - s_part_first[i];
        for (j = 0; j < q->n; j++)
            native_choice(q, j, T_LV + (uint32_t)(s_part_first[i] + j), DO_MSG,
                          (int)MSG_ID(P_LV0 + i, j));
    }
}

static uint32_t menu_add_choice(uint32_t cur, uint32_t menu, uint32_t group, uint32_t index)
{
    GuestRegs saved;
    uint32_t scratch, args[3], choice;
    regs_save(&saved);
    g_esp -= 0x40;
    scratch = g_esp;
    GMEM32(scratch + 0x20) = group;
    GMEM32(scratch + 0x24) = index;
    GMEM32(scratch + 0x30) = 0;
    args[0] = scratch + 0x30; args[1] = menu; args[2] = scratch + 0x20;
    g_eax = cur;
    guest_cdecl(sub_002A3910, args, 3);
    choice = GMEM32(scratch + 0x30);
    regs_load(&saved);
    return choice;
}

static uint32_t native_build_page(uint32_t cur, uint32_t menu, NativePage *p)
{
    GuestRegs saved;
    GuestXmm xmm1 = g_xmm1;
    uint32_t scratch, args[5], page;
    int i;
    regs_save(&saved);
    g_esp -= 0x40;
    scratch = g_esp;
    GMEM32(scratch + 0x20) = RESOT_TEXT_GROUP;
    GMEM32(scratch + 0x24) = p->title;
    GMEM32(scratch + 0x30) = 0;
    args[0] = scratch + 0x30; args[1] = 0; args[2] = f2u(p->top); args[3] = f2u(p->step);
    args[4] = 5;
    g_eax = cur; g_ecx = scratch + 0x20; g_edx = menu;
    g_xmm1.f[0] = MENU_PAGE_X;
    guest_cdecl(sub_002A2B20, args, 5);
    page = GMEM32(scratch + 0x30);
    regs_load(&saved);
    g_xmm1 = xmm1;
    for (i = 0; i < p->n; i++)
        p->c[i].choice = menu_add_choice(cur, menu, RESOT_TEXT_GROUP, p->c[i].text);
    regs_save(&saved);
    args[0] = menu;
    g_eax = cur;
    guest_cdecl(sub_002A2670, args, 1);
    regs_load(&saved);
    return page;
}

static int menu_bind(uint32_t choice, uint32_t action)
{
    GuestRegs saved;
    uint32_t args[3];
    if (!choice || !action)
        return 0;
    regs_save(&saved);
    g_esp -= 0x40;
    GMEM32(g_esp + 0x30) = 0;
    args[0] = g_esp + 0x30; args[1] = action; args[2] = choice;
    g_eax = s_main_cur; g_edx = s_main_menu;
    guest_cdecl(sub_00116660, args, 3);
    regs_load(&saved);
    return 1;
}

static int menu_bind_open_page(uint32_t choice, uint32_t page)
{
    GuestRegs saved;
    uint32_t args[3], action;
    if (!choice || !page)
        return 0;
    regs_save(&saved);
    args[0] = s_main_cur; args[1] = 0; args[2] = 1;
    g_eax = s_main_cur; g_ecx = page;
    action = guest_cdecl(sub_000182A0, args, 3);
    regs_load(&saved);
    return menu_bind(choice, action);
}

static int menu_bind_message(uint32_t choice, uint32_t id)
{
    GuestRegs saved;
    uint32_t scratch, args[2], action, i;
    if (!choice)
        return 0;
    regs_save(&saved);
    g_esp -= 0x80;
    scratch = g_esp;
    for (i = 0; i < 0x34; i += 4)
        GMEM32(scratch + 0x40 + i) = 0;
    GMEM32(scratch + 0x40 + 0x28) = RESOT_MSG_KIND;
    GMEM32(scratch + 0x40 + 0x30) = id;
    args[0] = s_main_cur; args[1] = scratch + 0x40;
    g_eax = s_main_cur;
    action = guest_cdecl(sub_00018310, args, 2);
    regs_load(&saved);
    return menu_bind(choice, action);
}

static void native_build_and_bind(void)
{
    int i, j, ok = 0, all = 0, root;
    native_layout();
    for (i = 0; i < P_PAGES; i++)
        s_pages[i].page = native_build_page(s_main_cur, s_main_menu, &s_pages[i]);
    for (i = 0; i < P_PAGES; i++)
        for (j = 0; j < s_pages[i].n; j++) {
            NativeChoice *c = &s_pages[i].c[j];
            all++;
            ok += c->what == DO_OPEN ? menu_bind_open_page(c->choice, s_pages[c->arg].page)
                                     : menu_bind_message(c->choice, (uint32_t)c->arg);
        }
    root = menu_bind_open_page(s_root_choice, s_pages[P_ROOT].page);
    root += menu_bind_open_page(s_level_choice, s_pages[P_LEVELS].page);
    s_native_built = 1;
    s_native_bound = 1;
    fprintf(stderr, "[MENU] reSOT Options: %d pages, %d of %d choices bound, main entries %08X %08X,"
                    " %d bound\n", P_PAGES, ok, all, s_level_choice, s_root_choice, root);
}

/* RE-SoT: the pause menu builds its pages the same way; its first page
 * "Pause" (text 0B00411F) opens at return 0x001A323E and closes at
 * 0x001A3441, and "Optionen" opens next (0x001A3470). "reSOT Options" goes
 * to the end of the pause page and its pages are built into the pause menu,
 * so the cheats and the Debug page are at hand in play. The level select
 * stays in the main menu: started from the pause menu, the map change
 * (2402_Central from 6101_couloir) ended on the "Spielende" page. */
#define PAUSE_OPEN_RET      0x001A323Eu
#define PAUSE_CLOSE_RET     0x001A3441u
#define PAUSE_NEXT_OPEN_RET 0x001A3470u

void sub_002A2B20_enter(void)
{
    if (menu_trace()) {
        char t[80];
        text_dump();
        menu_text(g_ecx, t, sizeof t);
        fprintf(stderr, "[MENU] page open  menu=%08X cur=%08X ret=%08X line=%d top=%g step=%g"
                        " flags=%08X x=%g title=%08X:%u \"%s\"\n",
                g_edx, g_eax, GMEM32(g_esp), (int)GMEM32(g_esp + 8), GMEMF(g_esp + 12),
                GMEMF(g_esp + 16), GMEM32(g_esp + 20), g_xmm1.f[0], g_ecx ? GMEM32(g_ecx) : 0,
                g_ecx ? GMEM32(g_ecx + 4) : 0, t);
    }
    if (!hidden_menus())
        return;
    if (GMEM32(g_esp) == MAIN_OPEN_RET || GMEM32(g_esp) == PAUSE_OPEN_RET) {   /* a new menu */
        s_native_built = s_native_bound = 0;
        s_root_choice = s_level_choice = 0;
    } else if ((GMEM32(g_esp) == MAIN_NEXT_OPEN_RET || GMEM32(g_esp) == PAUSE_NEXT_OPEN_RET)
               && s_root_choice && !s_native_built) {
        s_main_menu = g_edx;
        s_main_cur = g_eax;
        native_build_and_bind();
    }
}

/* Xbox Live is not provided (xbox-retail XBE). The main menu adds its choice
 * at 0x00234D35 (return 0x00234D3A, text 0B008C4E:56) and stores the handle
 * at [ebp+0x288]; the only reader is the bind at 0x00235406 (return
 * 0x0023540B). Skipping both leaves the page as if the title never had it.
 * The skipped add writes handle 0 to its out argument. The skipped bind
 * writes binding 0 and returns as 0x00116660 does (0x00116765-0x00116799):
 * eax/ecx/edx = the [0x758D64]/[0x758D68]/[0x758D6C] values it restores.
 * RESOT_XBOX_LIVE_MENU=1 keeps the choice. The pause menu's Xbox Live
 * choice (return 0x001A33F6) is not changed. */
#define LIVE_ADD_RET  0x00234D3Au
#define LIVE_BIND_RET 0x0023540Bu
extern void sub_002A3910_gen(void);
extern void sub_00116660_gen(void);
static int hide_live(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("RESOT_XBOX_LIVE_MENU"); on = !(e && *e && *e != '0'); }
    return on;
}
void sub_002A3910(void)
{
    if (hide_live() && GMEM32(g_esp) == LIVE_ADD_RET) {
        GMEM32(GMEM32(g_esp + 4)) = 0;
        g_esp += 4;                                   /* ret (cdecl) */
        return;
    }
    sub_002A3910_gen();
}
void sub_00116660(void)
{
    if (hide_live() && GMEM32(g_esp) == LIVE_BIND_RET && GMEM32(g_esp + 12) == 0) {
        GMEM32(GMEM32(g_esp + 4)) = 0;
        g_eax = GMEM32(0x00758D64u); g_ecx = GMEM32(0x00758D68u); g_edx = GMEM32(0x00758D6Cu);
        g_esp += 4;
        return;
    }
    sub_00116660_gen();
}

void sub_002A3910_enter(void)
{
    char t[80];
    uint32_t ref = GMEM32(g_esp + 12);
    if (!menu_trace()) return;
    menu_text(ref, t, sizeof t);
    fprintf(stderr, "[MENU] choice     menu=%08X ret=%08X text=%08X:%u \"%s\"\n",
            GMEM32(g_esp + 8), GMEM32(g_esp), ref ? GMEM32(ref) : 0, ref ? GMEM32(ref + 4) : 0, t);
}

void sub_002A2670_enter(void)
{
    if (menu_trace())
        fprintf(stderr, "[MENU] page close ret=%08X arg=%08X\n", GMEM32(g_esp), GMEM32(g_esp + 4));
    if ((GMEM32(g_esp) == MAIN_CLOSE_RET || GMEM32(g_esp) == PAUSE_CLOSE_RET) && hidden_menus()
            && !s_root_choice) {
        int pause = GMEM32(g_esp) == PAUSE_CLOSE_RET;
        if (!pause)
            s_level_choice = menu_add_choice(g_eax, GMEM32(g_esp + 4), RESOT_TEXT_GROUP, T_LEVELS);
        s_root_choice = menu_add_choice(g_eax, GMEM32(g_esp + 4), RESOT_TEXT_GROUP, T_ROOT);
        fprintf(stderr, "[MENU] hidden: \"reSOT Options\" choice %08X added to the %s page\n",
                s_root_choice, pause ? "pause" : "main");
    }
}

/* Labels: as in the demo, the refresh runs for our value choices once the
 * options' revision moves, after checking that the choice's text object
 * still carries our reference. */
static int native_label_word(uint64_t va, uint32_t *value)
{
    /* RE-SoT: retail TEXT_MANAGER 0x007584F0 +0x44 indexes main-menu
     * choices; those handles can outlive the menu after a world transition.
     * The native D3D12 pause run faulted reading 0xA7F52955 via an old
     * object's +0x9C. Probe only optional host-label metadata, never catch
     * faults from lifted game execution. Wide arithmetic rejects wraparound. */
    if (!resot_guest_read_word(g_xbox_mem_offset,va,value)) {
        static unsigned logged;
        if (logged++ < 4)
            fprintf(stderr,"[MENU] label refresh skipped unreadable guest word %08X\n",(uint32_t)va);
        return 0;
    }
    return 1;
}
static int native_text_object(uint32_t choice, uint32_t *out)
{
    uint32_t mgr, table, obj, text, it, end;
    if (!choice || !native_label_word(TEXT_MANAGER,&mgr) || !mgr ||
        !native_label_word((uint64_t)mgr+0x44,&table) || !table ||
        !native_label_word((uint64_t)table+(uint64_t)choice*4,&obj) || !obj ||
        !native_label_word((uint64_t)obj+0x9C,&text) || !text ||
        !native_label_word((uint64_t)text+0x0C,&it) ||
        !native_label_word((uint64_t)text+0x10,&end))
        return 0;
    /* Host traversal limit, not an XBE constant: reject stale vector bounds. */
    if (!it || end < it || ((it | end) & 3) || end-it > 65536u)
        return 0;
    for (; it < end; it += 4) {
        uint32_t action, vtable, group;
        if (!native_label_word(it,&action)) return 0;
        if (action && native_label_word(action,&vtable) && vtable == TEXT_ACTION_VTABLE &&
            native_label_word((uint64_t)action+0x34,&group) && group == RESOT_TEXT_GROUP) {
            *out = text;
            return 1;
        }
    }
    return 0;
}

static void native_refresh(void)
{
    static long seen;
    long revision;
    int i, j, n = 0;
    if (!s_native_bound || (revision = resot_native_revision() + debug_revision()) == seen)
        return;
    seen = revision;
    for (i = 0; i < P_PAGES; i++)
        for (j = 0; j < s_pages[i].n; j++) {
            GuestRegs saved;
            uint32_t text;
            if (i >= P_LEVELS || s_pages[i].c[j].what != DO_MSG
                    || !native_text_object(s_pages[i].c[j].choice, &text))
                continue;
            regs_save(&saved);
            g_ecx = text; g_eax = 1;
            guest_cdecl(sub_00038DD0, NULL, 0);
            regs_load(&saved);
            n++;
        }
    if (menu_trace())
        fprintf(stderr, "[MENU] reSOT labels refreshed (%d, revision %ld)\n", n, revision);
}

/* Message action execute: this = ecx, the descriptor copied at +0x24. */
void sub_00039B20(void)
{
    uint32_t self = g_ecx, id;
    if (GMEM32(self + 0x24 + 0x28) != RESOT_MSG_KIND) { sub_00039B20_gen(); return; }
    id = GMEM32(self + 0x24 + 0x30);
    if (menu_trace())
        fprintf(stderr, "[MENU] reSOT choice %04X\n", id);
    if (id == MSG_RESET)
        resot_native_reset_bindings();
    else if ((id >> 8) == P_GFX)
        resot_native_activate(RESOT_NATIVE_GRAPHICS, (int)(id & 0xFF));
    else if ((id >> 8) == P_CTRL)
        resot_native_activate(RESOT_NATIVE_CONTROLS, (int)(id & 0xFF));
    else if ((id >> 8) >= P_K0 && (id >> 8) <= P_J2)
        resot_native_activate((id >> 8) >= P_J0 ? RESOT_NATIVE_JOY : RESOT_NATIVE_KEYS,
                              (int)(id & 0xFF));
    else if ((id >> 8) == P_CHEATS && (id & 0xFF) < CHEATS) {
        s_cheat[id & 0xFF] = !s_cheat[id & 0xFF];
        s_cheat_revision++;
        fprintf(stderr, "[CHEAT] %s %s\n", s_cheat_names[id & 0xFF], s_cheat[id & 0xFF] ? "on" : "off");
    } else if (id == MSG_ID(P_CHEATS, CHEATS))
        s_sword_pending = 1;                      /* once the menu is closed */
    else if ((id >> 8) == P_DEBUG)
        debug_activate((int)(id & 0xFF));
#ifdef _WIN32
    else if ((id >> 8) == P_SOUND && (id & 0xFF) < 2)
        resot_options_audio_activate((int)(id & 0xFF));
#endif
    else if ((id >> 8) == P_PROTO && (id & 0xFF) < PROS) {
        s_proto[id & 0xFF] = !s_proto[id & 0xFF];
        s_cheat_revision++;
        fprintf(stderr, "[PROTO] %s %s\n", s_proto_names[id & 0xFF], s_proto[id & 0xFF] ? "on" : "off");
    }
    else if ((id >> 8) >= P_LV0 && (id >> 8) <= P_LV3) {
        int n = s_part_first[(id >> 8) - P_LV0] + (int)(id & 0xFF);
        if (n < s_part_first[(id >> 8) - P_LV0 + 1])
            s_level_pending = (uint32_t)n + 1;
    }
    g_esp += 8;                           /* ret 4 */
}

/* Starts the chosen level the way the main menu starts the credits
 * (0x001432C0-0x0014330D): set the flag behind [0x76BF30], close the top
 * page when it is active (vtable +8, thiscall (1, 0, 1)), then eax =
 * [0x76606C], [0x758C8C] = 0 and ChangeMapWithCheckPoint(map, -1). Run from
 * the input poll, so the menu manager is not in the middle of its tick. */
#define MENU_LEAVE_FLAG  0x0076BF30u
#define MENU_MANAGER     0x007584F0u
#define MAP_CHANGE_EAX   0x0076606Cu
#define MAP_CHANGE_STATE 0x00758C8Cu
extern void sub_0025CF40(void);
typedef void (*GuestFn)(void);
extern GuestFn recomp_lookup(uint32_t xbox_va);
static void level_pending(void)
{
    GuestRegs saved;
    uint32_t args[3], flag, mgr, n, page;
    const Level *lv;
    /* RESOT_START_LEVEL=<room, e.g. 6101_couloir, or world-list stream>:
     * chosen by itself about three seconds after the main menu is built. */
    static int start_polls = -1;
    if (start_polls < 0) {
        const char *e = getenv("RESOT_START_LEVEL");
        int i;
        start_polls = 0;
        for (i = 0; e && *e && i < LEVELS; i++)
            if (!_stricmp(e, s_levels[i].room) || strtoul(e, NULL, 16) == s_levels[i].stream)
                start_polls = 1 + i;
    }
    if (start_polls > 0 && s_native_bound) {
        static int polls;
        if (++polls == 180) {
            s_level_pending = (uint32_t)start_polls;
            start_polls = 0;
        }
    }
    if (!s_level_pending)
        return;
    lv = &s_levels[s_level_pending - 1];
    s_level_pending = 0;
    fprintf(stderr, "[LEVEL] level select: %s (world list %08X)\n", lv->room, lv->stream);
    regs_save(&saved);
    g_esp -= 0x40;
    if ((flag = GMEM32(MENU_LEAVE_FLAG)) != 0)
        GMEM32(flag) = 1;
    if ((mgr = GMEM32(MENU_MANAGER)) != 0 && (n = GMEM32(mgr + 0xF0)) != 0
            && (page = GMEM32(mgr + n * 4 + 0x6C)) != 0 && GMEM32(page + 0x84) == 2) {
        GuestFn close = recomp_lookup(GMEM32(GMEM32(page) + 8));
        if (close) {
            args[0] = 1; args[1] = 0; args[2] = 1;
            g_ecx = page;
            guest_cdecl(close, args, 3);
        }
    }
    regs_load(&saved);
    regs_save(&saved);
    g_esp -= 0x40;
    args[0] = ((lv->stream & 0xF0000u) << 8) | (lv->stream & 0xFFFFu);
    args[1] = 0xFFFFFFFFu;
    g_eax = GMEM32(MAP_CHANGE_EAX);
    GMEM32(MAP_CHANGE_STATE) = 0;
    guest_cdecl(sub_0025CF40, args, 2);
    regs_load(&saved);
}

/* Cheats, after popsot-fixes (src/cheats.cpp, docs/TECHNICAL.md:474), which rebuilt
 * the PS2 prototype's cheat mode (Mdl0000186A_fn_Cheat_Set / Cheat_Test,
 * Mdl000092EB_fn_iCheats in its MAPFILE.MAP). The retail XBE keeps only the
 * test, inlined into the spawners (0x001CAE51, 0x0021F897). The variables
 * are laid out as on PC:
 * - main actors (script references 1, 2) at [0x67B6A0] / [0x67B6A4]
 *   (cleared together in 0x000E7C70); an object's AI variables are
 *   obj +0x18 -> +4 -> +0x44;
 * - prince: life +0x19C, maximum +0x11F8 (fn_RegenerateLife, 0x0026AE40);
 *   filled sand tanks +0x7AC, tanks +0xD24, sand in the current tank
 *   +0xCA8, a full tank +0x122C (fn_AddSOT, 0x0026AFA0);
 * - fn_Combat_ProcessCurrentHurt (0x001764E0, object in eax): in hurt
 *   state 4 (+0x398) it takes the damage (float +0x208) off the life, and
 *   1000.0 ([0x4294C4]) is the game's own instant kill;
 * - sand creatures (model 0x7493) count melee hits in
 *   fn_ProcessMeeleeWeaponHits (0x0017A0C0, cdecl object, message; the
 *   message's +0 is the sender) at variable +0x7C;
 * - fn_SetWeapon of the prince (0x0026CE80, cdecl prince) takes the next of
 *   the four swords itself (index +0x73C wraps after 3; swords from +0x8B8)
 *   without checking the slot.
 * Invulnerable and infinite sand are kept at their maximum at every input
 * poll; one-hit kills set the damage to 1000.0 for every hit on an object
 * that is not a main actor (so Farah is never killed by a hit).
 * The prototype's spawner switches: Cheat_Test reads the byte at variable
 * +0x210 of a global script object, [0x766C3C] in Spawn_WP__MSG (mask 3: the
 * spawn request is dropped) and [0x76766C] in Spawn_WP_bhv_spawn (mask 8:
 * a spawner that has sent all its enemies starts its count again, through
 * 0x00266E20). Nothing in retail sets them; "No enemies" sets bit 0x01,
 * "Endless enemies" bit 0x08, and only the bits set here are cleared again.
 * Switched on the reSOT Cheats page (main or pause menu) or in play with
 * Ctrl+F1 (invulnerable), Ctrl+F2 (sand), Ctrl+F3 (one-hit kills); Ctrl+F4
 * or the page's sword entry gives the next sword (from the page: once the
 * menu is closed). Not saved: every start begins with them off. */
#define MAIN_ACTOR1     0x0067B6A0u
#define MAIN_ACTOR2     0x0067B6A4u
#define V_LIFE          0x19Cu
#define V_LIFE_MAX      0x11F8u
#define V_TANKS         0x7ACu
#define V_TANKS_MAX     0xD24u
#define V_SAND          0xCA8u
#define V_SAND_FULL     0x122Cu
#define V_HURT_STATE    0x398u
#define V_HURT_DAMAGE   0x208u
#define V_HITS          0x7Cu
#define V_SWORD_INDEX   0x73Cu
#define V_WEAPONS       0x8B8u
#define SPAWN_MSG_GLOBAL   0x00766C3Cu
#define SPAWN_BHV_GLOBAL   0x0076766Cu
#define V_CHEAT_BITS       0x210u
#define CHEAT_BIT_NO_SPAWN 0x01u
#define CHEAT_BIT_ENDLESS  0x08u
#define GMEM8(va) (*(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
extern void sub_0026CE80(void);
extern int xbox_FramebufferKeyDown(int vk);

static uint32_t cheat_word(uint32_t va, int *ok)
{
    uint32_t v = 0;
    if (!va || !resot_guest_read_word(g_xbox_mem_offset, va, &v)) *ok = 0;
    return v;
}

/* An object's AI variables, or 0; `size` bytes of them must be readable. */
static uint32_t cheat_vars(uint32_t obj, uint32_t size)
{
    int ok = 1;
    uint32_t ext = cheat_word(obj + 0x18, &ok), ai, vars;
    ai = ok ? cheat_word(ext + 4, &ok) : 0;
    vars = ok ? cheat_word(ai + 0x44, &ok) : 0;
    if (ok) cheat_word(vars + size - 4, &ok);
    return ok ? vars : 0;
}

static int cheat_in_play(void)
{
    int ok = 1;
    uint32_t mgr = cheat_word(MENU_MANAGER, &ok);
    return ok && cheat_word(mgr + 0xF0, &ok) == 0 && ok;
}

static void cheat_sword_label(char *out, unsigned size)
{
    int de = resot_text_language() == 4;
    uint32_t v = cheat_vars(GMEM32(MAIN_ACTOR1), V_SAND_FULL + 4);
    if (v && GMEM32(v + V_SWORD_INDEX) < 4)
        snprintf(out, size, de ? "Schwert wechseln: %u von 4" : "Next sword: %u of 4",
                 GMEM32(v + V_SWORD_INDEX) + 1);
    else
        snprintf(out, size, de ? "Schwert wechseln" : "Next sword");
}

/* One switch bit of a spawner's global object; *mine: this bit was set here. */
static void cheat_spawn_bit(uint32_t global, uint8_t bit, int on, int *mine)
{
    int ok = 1;
    uint32_t obj = cheat_word(global, &ok), v;
    uint8_t b;
    if (!ok || !obj || !(v = cheat_vars(obj, V_CHEAT_BITS + 4)))
        return;
    b = GMEM8(v + V_CHEAT_BITS);
    if (on && !(b & bit)) {
        GMEM8(v + V_CHEAT_BITS) = (uint8_t)(b | bit);
        *mine = 1;
        fprintf(stderr, "[CHEAT] spawner switch %02X set (object %08X)\n", bit, obj);
    } else if (!on && *mine) {
        if (b & bit)
            GMEM8(v + V_CHEAT_BITS) = (uint8_t)(b & ~bit);
        *mine = 0;
    }
}

static void cheat_next_sword(void)
{
    GuestRegs saved;
    uint32_t prince = GMEM32(MAIN_ACTOR1), v, idx, next, args[1];
    if (!cheat_in_play() || !(v = cheat_vars(prince, V_SAND_FULL + 4))) {
        fprintf(stderr, "[CHEAT] next sword: no prince in play\n");
        return;
    }
    idx = GMEM32(v + V_SWORD_INDEX);
    next = idx >= 3 ? 0 : idx + 1;
    if (!GMEM32(v + V_WEAPONS + next * 4)) {
        fprintf(stderr, "[CHEAT] next sword: slot %u is empty\n", next);
        return;
    }
    regs_save(&saved);
    g_esp -= 0x40;
    args[0] = prince;
    guest_cdecl(sub_0026CE80, args, 1);
    regs_load(&saved);
    fprintf(stderr, "[CHEAT] sword %u of 4\n", GMEM32(v + V_SWORD_INDEX) + 1);
}

static void debug_tick(void);
static void proto_tick(void);
static void sound_tick(void);

static void cheats_tick(void)
{
    static const char *const names[3] = { "invulnerable", "infinite sand", "one-hit kills" };
    static int was[4], mine_spawn, mine_endless;
    int ctrl = xbox_FramebufferKeyDown(0xA2) || xbox_FramebufferKeyDown(0xA3), i;
    uint32_t v;
    static int configured;
    if (!configured) {                           /* RESOT_CHEATS=invulnerable,sand,onehit */
        const char *e = getenv("RESOT_CHEATS");
        configured = 1;
        if (e && *e) {
            s_cheat[CHEAT_INVULNERABLE] = strstr(e, "invulnerable") != NULL;
            s_cheat[CHEAT_SAND] = strstr(e, "sand") != NULL;
            s_cheat[CHEAT_ONE_HIT] = strstr(e, "onehit") != NULL;
            s_cheat[CHEAT_NO_SPAWN] = strstr(e, "nospawn") != NULL;
            s_cheat[CHEAT_ENDLESS] = strstr(e, "endless") != NULL;
            s_cheat_revision++;
        }
    }
    for (i = 0; i < 4; i++) {                    /* Ctrl+F1 .. Ctrl+F4 */
        int down = ctrl && xbox_FramebufferKeyDown(0x70 + i);
        if (down && !was[i]) {
            if (i < 3) {
                s_cheat[i] = !s_cheat[i];
                s_cheat_revision++;
                fprintf(stderr, "[CHEAT] %s %s\n", names[i], s_cheat[i] ? "on" : "off");
            } else {
                cheat_next_sword();
            }
        }
        was[i] = down;
    }
    if (s_sword_pending && cheat_in_play()) {
        s_sword_pending = 0;
        cheat_next_sword();
        s_cheat_revision++;
    }
    cheat_spawn_bit(SPAWN_MSG_GLOBAL, CHEAT_BIT_NO_SPAWN, s_cheat[CHEAT_NO_SPAWN], &mine_spawn);
    cheat_spawn_bit(SPAWN_BHV_GLOBAL, CHEAT_BIT_ENDLESS, s_cheat[CHEAT_ENDLESS], &mine_endless);
    debug_tick();
    proto_tick();
    sound_tick();
    if (!s_cheat[CHEAT_INVULNERABLE] && !s_cheat[CHEAT_SAND])
        return;
    if (!(v = cheat_vars(GMEM32(MAIN_ACTOR1), V_SAND_FULL + 4)))
        return;
    if (s_cheat[CHEAT_INVULNERABLE]) {
        int32_t life = (int32_t)GMEM32(v + V_LIFE), max = (int32_t)GMEM32(v + V_LIFE_MAX);
        if (max > 0 && max < 100000 && life > 0 && life < max)
            GMEM32(v + V_LIFE) = (uint32_t)max;
    }
    if (s_cheat[CHEAT_SAND]) {
        int32_t tanks = (int32_t)GMEM32(v + V_TANKS), max = (int32_t)GMEM32(v + V_TANKS_MAX);
        if (max > 0 && max <= 10 && tanks >= 0 && tanks <= max) {
            GMEM32(v + V_TANKS) = (uint32_t)max;
            GMEM32(v + V_SAND) = GMEM32(v + V_SAND_FULL);
        }
    }
    {                       /* once for each prince, and again ten seconds later */
        static uint32_t prince;
        static int polls;
        if (prince != GMEM32(MAIN_ACTOR1)) { prince = GMEM32(MAIN_ACTOR1); polls = 0; }
        if (polls == 0 || polls == 600)
            fprintf(stderr, "[CHEAT] prince %08X: life %d of %d, sand tanks %d of %d\n", prince,
                    (int)GMEM32(v + V_LIFE), (int)GMEM32(v + V_LIFE_MAX),
                    (int)GMEM32(v + V_TANKS), (int)GMEM32(v + V_TANKS_MAX));
        if (polls <= 600) polls++;
    }
}

/* fn_Combat_ProcessCurrentHurt: the object in eax. */
void sub_001764E0_enter(void)
{
    uint32_t obj = g_eax, v;
    if (!s_cheat[CHEAT_ONE_HIT] || obj == GMEM32(MAIN_ACTOR1) || obj == GMEM32(MAIN_ACTOR2))
        return;
    if ((v = cheat_vars(obj, V_HURT_STATE + 4)) != 0 && GMEM32(v + V_HURT_STATE) == 4)
        GMEMF(v + V_HURT_DAMAGE) = 1000.0f;
}

/* Mdl00007493_fn_ProcessMeeleeWeaponHits (cdecl object, message): a hit sent
 * by the prince (reference 1 or the actor itself) finds the counter at 0. */
void sub_0017A0C0_enter(void)
{
    uint32_t obj = GMEM32(g_esp + 4), msg = GMEM32(g_esp + 8), sender, v;
    int ok = 1;
    if (!s_cheat[CHEAT_ONE_HIT])
        return;
    sender = cheat_word(msg, &ok);
    if (!ok || (sender != 1 && sender != GMEM32(MAIN_ACTOR1)))
        return;
    if ((v = cheat_vars(obj, V_HITS + 4)) != 0 && (int32_t)GMEM32(v + V_HITS) > 0)
        GMEM32(v + V_HITS) = 0;
}

/* The game's room reverb. The XDK DirectSound keeps its settings in an
 * object of 0xB4 bytes built by sub_003E73CC (called from 0x003E7CE2,
 * stored at the DirectSound object's +8); +0x84 holds the current
 * DSI3DL2LISTENER (12 DWORDs), which the constructor sets to the library's
 * default, the I3DL2 "Generic" preset at 0x00401590. The host reverb
 * (apu_xaudio2.c, xa2_set_i3dl2_listener) follows it: the object is taken
 * at the constructor's entry and read at every input poll. */
#define DS_SETTINGS_LISTENER 0x84u
extern void xa2_set_i3dl2_listener(const void *listener48);
static uint32_t s_ds_settings;

void sub_003E73CC_enter(void)
{
    s_ds_settings = g_ecx;
    fprintf(stderr, "[SOUND] DirectSound settings object %08X\n", s_ds_settings);
}

static void sound_tick(void)
{
    uint8_t l[48];
    uint32_t i, w;
    if (!s_ds_settings) return;
    for (i = 0; i < 48; i += 4) {
        if (!resot_guest_read_word(g_xbox_mem_offset, s_ds_settings + DS_SETTINGS_LISTENER + i, &w))
            return;
        memcpy(l + i, &w, 4);
    }
    xa2_set_i3dl2_listener(l);
}

/* The Prototype page: switches of the universe script object (0xCC00008E)
 * that retail still reads but never sets, found by comparing the PS2
 * prototype's MAPFILE.MAP localref slots (__localref__cc00008e_<offset>_*)
 * with the retail readers. The universe's AI instance is [0x44AA24]
 * (0x001109C4: script reference 0xCC00008E resolves to it), its variables
 * at +0x44.
 * - not offered: +0xD8, the prototype's "Cheats" switch of the pause
 *   menu's Special page. Prince_Bhv_BeforeMainTrack still calls the
 *   prototype's fn_iCheats (0x00110DB1 -> 0x0026F1C0), which tests the
 *   switch (its slot 0x76BFEC), then prince variable +0x704 (1 in play) and
 *   then pad codes through 0x000DC4F0 (7, 5, ...); R3 ghost mode
 *   (0x00111DA6, Prince_Bhv_GhostMode 0x002825F0) and Back's death menu
 *   (0x00110DC5) test it too. With +0xD8 set, no single button changed the
 *   life or the sword in 6101 (d-pad, Black, White, triggers, thumbs, X, Y,
 *   B): the combination or the controller it wants is not known yet.
 * - +0xE0, life regeneration: fn_RegenerateLife (0x0026AE40, reads it at
 *   0x0026AEF4) gives one life point per second while it is non-zero; the
 *   universe's behaviour puts 0 there again every frame (0x001BC4A4, seen
 *   in play), so the option sets it at that function's entry.
 * - +0xBC, the prototype's "Interface" switch: read by
 *   fn_UpdateLifeBarVisibility and the tutorial manager; it starts non-zero
 *   (from the data), the option writes 0 and puts the old value back.
 * Set at every input poll while on; a value is only put back by the
 * switch that changed it. Not saved. */
#define UNIVERSE_AI     0x0044AA24u
#define U_REGEN         0xE0u
#define U_INTERFACE     0xBCu

static uint32_t universe_vars(void)
{
    int ok = 1;
    uint32_t ai = cheat_word(UNIVERSE_AI, &ok), v;
    v = ok && ai ? cheat_word(ai + 0x44, &ok) : 0;
    if (ok && v) cheat_word(v + U_REGEN, &ok);
    return ok ? v : 0;
}

static void proto_tick(void)
{
    static const uint32_t off[PROS] = { U_REGEN, U_INTERFACE };
    static int mine[PROS];
    static uint32_t saved[PROS];
    static int configured;
    uint32_t v = universe_vars(), want;
    int i;
    if (!configured) {                           /* RESOT_PROTO=regen,nohud */
        const char *e = getenv("RESOT_PROTO");
        configured = 1;
        if (e && *e) {
            s_proto[PRO_REGEN] = strstr(e, "regen") != NULL;
            s_proto[PRO_NOHUD] = strstr(e, "nohud") != NULL;
            s_cheat_revision++;
        }
    }
    if (!v) return;
    for (i = 0; i < PROS; i++) {
        uint32_t cur = GMEM32(v + off[i]);
        if (i == PRO_REGEN) continue;            /* at fn_RegenerateLife's entry */
        want = 0;                                /* the interface switch */
        if (s_proto[i]) {
            if (!mine[i]) { saved[i] = cur; mine[i] = 1; }
            if (cur != want) {
                GMEM32(v + off[i]) = want;
                fprintf(stderr, "[PROTO] universe +0x%X: %u -> %u\n", off[i], cur, want);
            }
        } else if (mine[i]) {
            GMEM32(v + off[i]) = i == PRO_NOHUD && !saved[i] ? 1 : saved[i];
            mine[i] = 0;
        }
    }
}

/* fn_RegenerateLife (object in eax). */
void sub_0026AE40_enter(void)
{
    uint32_t v;
    if (!s_proto[PRO_REGEN] || !(v = universe_vars()))
        return;
    GMEM32(v + U_REGEN) = 1;
    if (getenv("RESOT_PROTO_TRACE")) {           /* the prince's life, when it changes */
        static int32_t last = -1;
        uint32_t pv = cheat_vars(GMEM32(MAIN_ACTOR1), V_SAND_FULL + 4);
        static uint32_t last_sword;
        static int hurt;
        if (pv && !hurt && GMEM32(pv + 0x704) == 1 && !strcmp(getenv("RESOT_PROTO_TRACE"), "hurt")) {
            hurt = 1;                            /* =hurt: half the life once, in play */
            GMEM32(pv + V_LIFE) = GMEM32(pv + V_LIFE_MAX) / 2;
        }
        if (pv && ((int32_t)GMEM32(pv + V_LIFE) != last || GMEM32(pv + V_SWORD_INDEX) != last_sword)) {
            last = (int32_t)GMEM32(pv + V_LIFE);
            last_sword = GMEM32(pv + V_SWORD_INDEX);
            fprintf(stderr, "[PROTO] %.1f s: prince life %d of %d, sword %u\n",
                    (double)clock() / CLOCKS_PER_SEC, last, (int)GMEM32(pv + V_LIFE_MAX), last_sword + 1);
        }
    }
}

/* Free camera, after popsot-fixes (src/menucam.cpp, docs/TECHNICAL.md:345):
 * a free-flying camera written into the main view's camera struct.
 * Retail (Jade source WORrender.c, PS2 prototype CAM_Inverse 0x400CC0):
 * the current display is [0x758E34] (GDI_gpst_CurDD) and its main camera
 * +0xD0. WOR_SetCam (0x0002F780) sets that camera from the view's object
 * matrix (CAM_SetCameraMatrix, 0x002FDDD0), then calls CAM_Inverse
 * (0x002FDD00, camera in eax; returns to 0x0002F7EB), which builds the view
 * matrix (+0xE0) from rows I +0x90, J +0xA0, K +0xB0 (the viewing
 * direction) and the position +0xC0; the frustum is built from the camera
 * right after, so culling follows. The camera is written in that call's
 * entry, rebuilt from yaw and pitch without roll, keeping the signs of the
 * I and J rows the game's camera had when ours started.
 * ENG_gb_ForcePauseEngine, 0x007584A0, makes ENG_Loop (0x000164D0, test at
 * 0x0001663F) skip the engine call while display still runs: the world
 * freezes, the camera keeps moving. The game never sets it.
 * Input: the pad state the title polls (XInputGetState, above), so the
 * keyboard's bindings work too: left stick move, right stick look, LT / RT
 * (or Q / E) down / up, Black or Shift faster, White or Ctrl slower, Y or P
 * freezes the world. Back or F8 switches the camera on and off, as does the
 * reSOT Debug page; while it is on the game gets no input but Start, except
 * while a menu page is open in a level (in the main menu the camera takes
 * it, see the menu camera below). RESOT_FREECAM=0 disables it. */
#define CUR_DISPLAY        0x00758E34u
#define DISPLAY_CAMERA     0xD0u
#define CAM_ROW_I          0x90u
#define CAM_ROW_J          0xA0u
#define CAM_ROW_K          0xB0u
#define CAM_POS            0xC0u
#define SETCAM_INVERSE_RET 0x0002F7EBu
#define FORCE_PAUSE_ENGINE 0x007584A0u
extern void resot_current_wow(char *out, size_t size);
static struct {
    int on, started, frozen;
    float lx, ly, rx, ry, vert, speed;
    float yaw, pitch, pos[3], si, sj;
    double last;
} s_fc;

static int freecam_enabled(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("RESOT_FREECAM"); on = !(e && *e == '0'); }
    return on;
}

static float fc_axis(int16_t v)
{
    float f = (float)v / 32767.0f, a = f < 0 ? -f : f;
    if (a < 0.2f) return 0.0f;                   /* round-ish dead zone */
    return (f < 0 ? -1.0f : 1.0f) * (a - 0.2f) / 0.8f;
}

static int menu_world(void);
static void menucam_store(int which);

static void freecam_pad(uint8_t *st)
{
    static int was_toggle, was_freeze, was_x, was_b;
    uint16_t buttons;
    int16_t th[4];
    int toggle, freeze, i;
    if (!freecam_enabled())
        return;
    memcpy(&buttons, st + 4, 2);
    memcpy(th, st + 14, 8);
    toggle = (buttons & 0x20) || xbox_FramebufferKeyDown(0x77);          /* Back, F8 */
    if (toggle && !was_toggle) {
        s_fc.on = !s_fc.on;
        s_fc.started = 0;
        if (!s_fc.on && s_fc.frozen) { GMEM32(FORCE_PAUSE_ENGINE) = 0; s_fc.frozen = 0; }
        fprintf(stderr, "[FREECAM] %s\n", s_fc.on ? "on" : "off");
    }
    was_toggle = toggle;
    if (!s_fc.on)
        return;
    freeze = st[6 + 3] > 128 || xbox_FramebufferKeyDown(0x50);         /* Y, P */
    if (freeze && !was_freeze) {
        s_fc.frozen = !s_fc.frozen;
        GMEM32(FORCE_PAUSE_ENGINE) = (uint32_t)s_fc.frozen;
        fprintf(stderr, "[FREECAM] world %s\n", s_fc.frozen ? "frozen" : "running");
    }
    was_freeze = freeze;
    if (!cheat_in_play() && !menu_world()) {     /* a menu page is open: it keeps the input */
        s_fc.lx = s_fc.ly = s_fc.rx = s_fc.ry = s_fc.vert = 0.0f;
        return;
    }
    if (menu_world()) {                          /* X / B: the menu picture / the flight's end */
        int x = st[6 + 2] > 128, b = st[6 + 1] > 128;
        if (x && !was_x) menucam_store(0);
        if (b && !was_b) menucam_store(1);
        was_x = x; was_b = b;
    }
    s_fc.lx = fc_axis(th[0]); s_fc.ly = fc_axis(th[1]);
    s_fc.rx = fc_axis(th[2]); s_fc.ry = fc_axis(th[3]);
    s_fc.vert = ((float)st[6 + 7] - (float)st[6 + 6]) / 255.0f;         /* RT - LT */
    if (xbox_FramebufferKeyDown(0x45)) s_fc.vert += 1.0f;               /* E */
    if (xbox_FramebufferKeyDown(0x51)) s_fc.vert -= 1.0f;               /* Q */
    s_fc.speed = 6.0f;
    if (st[6 + 4] > 128 || xbox_FramebufferKeyDown(0xA0) || xbox_FramebufferKeyDown(0xA1))
        s_fc.speed *= 4.0f;                                              /* Black, Shift */
    if (st[6 + 5] > 128 || xbox_FramebufferKeyDown(0xA2) || xbox_FramebufferKeyDown(0xA3))
        s_fc.speed *= 0.25f;                                             /* White, Ctrl */
    /* the game sees no input but Start */
    buttons &= 0x10;
    memcpy(st + 4, &buttons, 2);
    for (i = 0; i < 16; i++)
        st[6 + i] = 0;
}

static double fc_now(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void menucam_apply(uint32_t cam);

/* CAM_Inverse (camera in eax). */
void sub_002FDD00_enter(void)
{
    uint32_t dd, cam;
    float k[3], r[3], u[3], cy, sy, cp, sp, len, dt;
    double now;
    int i;
    if (getenv("RESOT_CAM_TRACE") && GMEM32(g_esp) == SETCAM_INVERSE_RET && (dd = GMEM32(CUR_DISPLAY))
            && g_eax == dd + DISPLAY_CAMERA) {
        static unsigned n;
        if (n++ % 30 == 0) {
            char wow[64];
            resot_current_wow(wow, sizeof wow);
            fprintf(stderr, "[CAM] %s pos %.3f %.3f %.3f K %.3f %.3f %.3f\n", wow,
                    GMEMF(g_eax + CAM_POS), GMEMF(g_eax + CAM_POS + 4), GMEMF(g_eax + CAM_POS + 8),
                    GMEMF(g_eax + CAM_ROW_K), GMEMF(g_eax + CAM_ROW_K + 4), GMEMF(g_eax + CAM_ROW_K + 8));
        }
    }
    if (GMEM32(g_esp) != SETCAM_INVERSE_RET || !(dd = GMEM32(CUR_DISPLAY))
            || g_eax != dd + DISPLAY_CAMERA)
        return;
    if (!s_fc.on) {
        menucam_apply(g_eax);
        return;
    }
    cam = g_eax;
    now = fc_now();
    if (!s_fc.started) {
        float i0[3], j0[3];
        menucam_apply(cam);                      /* in the main menu: start where it shows */
        for (i = 0; i < 3; i++) {
            k[i] = GMEMF(cam + CAM_ROW_K + 4u * i);
            i0[i] = GMEMF(cam + CAM_ROW_I + 4u * i);
            j0[i] = GMEMF(cam + CAM_ROW_J + 4u * i);
            s_fc.pos[i] = GMEMF(cam + CAM_POS + 4u * i);
        }
        s_fc.yaw = atan2f(k[1], k[0]);
        s_fc.pitch = asinf(k[2] < -1.0f ? -1.0f : k[2] > 1.0f ? 1.0f : k[2]);
        /* right = K x up, up' = right x K (Z up); match the game's signs */
        r[0] = k[1]; r[1] = -k[0]; r[2] = 0.0f;
        u[0] = r[1] * k[2] - r[2] * k[1]; u[1] = r[2] * k[0] - r[0] * k[2];
        u[2] = r[0] * k[1] - r[1] * k[0];
        s_fc.si = r[0] * i0[0] + r[1] * i0[1] + r[2] * i0[2] < 0.0f ? -1.0f : 1.0f;
        s_fc.sj = u[0] * j0[0] + u[1] * j0[1] + u[2] * j0[2] < 0.0f ? -1.0f : 1.0f;
        s_fc.last = now;
        s_fc.started = 1;
        fprintf(stderr, "[FREECAM] start at %.2f %.2f %.2f, yaw %.2f pitch %.2f, signs %g %g\n",
                s_fc.pos[0], s_fc.pos[1], s_fc.pos[2], s_fc.yaw, s_fc.pitch, s_fc.si, s_fc.sj);
    }
    dt = (float)(now - s_fc.last);
    s_fc.last = now;
    if (dt < 0.0f || dt > 0.1f) dt = 0.1f;
    s_fc.yaw -= s_fc.rx * 2.0f * dt;
    s_fc.pitch += s_fc.ry * 2.0f * dt;
    if (s_fc.pitch > 1.5f) s_fc.pitch = 1.5f;
    if (s_fc.pitch < -1.5f) s_fc.pitch = -1.5f;
    cy = cosf(s_fc.yaw); sy = sinf(s_fc.yaw); cp = cosf(s_fc.pitch); sp = sinf(s_fc.pitch);
    k[0] = cp * cy; k[1] = cp * sy; k[2] = sp;
    r[0] = k[1]; r[1] = -k[0]; r[2] = 0.0f;
    len = sqrtf(r[0] * r[0] + r[1] * r[1]);
    r[0] /= len; r[1] /= len;
    u[0] = r[1] * k[2] - r[2] * k[1]; u[1] = r[2] * k[0] - r[0] * k[2];
    u[2] = r[0] * k[1] - r[1] * k[0];
    for (i = 0; i < 3; i++)
        s_fc.pos[i] += (k[i] * s_fc.ly + r[i] * s_fc.lx) * s_fc.speed * dt;
    s_fc.pos[2] += s_fc.vert * s_fc.speed * dt;
    for (i = 0; i < 3; i++) {
        GMEMF(cam + CAM_ROW_I + 4u * i) = s_fc.si * r[i];
        GMEMF(cam + CAM_ROW_J + 4u * i) = s_fc.sj * u[i];
        GMEMF(cam + CAM_ROW_K + 4u * i) = k[i];
        GMEMF(cam + CAM_POS + 4u * i) = s_fc.pos[i];
    }
}

/* Main-menu camera for wide screens, after popsot-fixes (src/menucam.cpp,
 * docs/TECHNICAL.md "Cameras"). The front end's 3D scene (world "Menu" in
 * the current-WOW name, resot_current_wow) was built for 4:3; at 16:9 the
 * set runs out at the left and right. Its camera rests at the PC version's
 * Camera02 position (-83.589 1.389 -2.662, K -0.991 -0.101 0.085, traced
 * with RESOT_CAM_TRACE=1) and New Game flies it to the same end
 * (-92.802 -0.965 3.932, K -0.906 -0.394 0.157), so popsot-fixes' rules
 * apply unchanged: in the menu the camera is shown from the start pose or
 * moved by [Menu] CameraForward / CameraUp / CameraSide (along K, up, to the
 * right; defaults 3.5 / 6.25 / 0 as on PC). [Menu] CameraMode: "pc" (the
 * default) uses the user's poses of the PC port, "own" the ones stored with
 * the free camera in the main menu (X: CameraStart, B: CameraEnd), and
 * "original" leaves the camera alone (also RESOT_MENU_CAMERA=0).
 * During the flight the moved camera follows the flight's progress (its
 * position projected on the line menu -> end) from our start to the end
 * pose (B, [Menu] CameraEnd) or the game's end; the view turns from the
 * start view to the end view, or without an end pose towards the game's
 * view corrected so the Prince (about -103.2 -5.05 1.0) stays where the
 * game shows him. With an end pose it is held while the game's camera
 * stays at the end, then blended over (within 3 units, 0.75 s) or cut to.
 * The Debug page's "Menu camera" entry switches the mode. The camera rows
 * are rewritten by WOR_SetCam before every CAM_Inverse; a camera we moved
 * that comes back unchanged is put back to its base first. */
#define MENU_CAM_X  -83.589f
#define MENU_CAM_Y    1.389f
#define MENU_CAM_Z   -2.662f
static const float s_menu_cam[3] = { MENU_CAM_X, MENU_CAM_Y, MENU_CAM_Z };
static const float s_flight_end[3] = { -92.802f, -0.965f, 3.932f };
static const float s_prince_balcony[3] = { -103.2f, -5.05f, 1.0f };
typedef struct { int set; float pos[3], yaw, pitch; } MenuPose;
static struct {
    int loaded, mode;
    float forward, up, side, offset[3];
    MenuPose pose[2];                    /* in use: 0 start (menu picture), 1 end of the flight */
    MenuPose own[2];                     /* the player's, [Menu] CameraStart / CameraEnd */
    int flight_done, arrived, handover;
    float start[3], start_yaw, start_pitch;
    double handover_start;
    uint32_t cam;                        /* the camera we moved last */
    int have_out;
    float base[3], out[3], base_rows[12];
} s_mc;
static const char *const s_pose_keys[2] = { "CameraStart", "CameraEnd" };

static float v_dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void v_cross(const float *a, const float *b, float *r)
{
    r[0] = a[1] * b[2] - a[2] * b[1]; r[1] = a[2] * b[0] - a[0] * b[2]; r[2] = a[0] * b[1] - a[1] * b[0];
}
static void v_norm(float *v)
{
    float l = sqrtf(v_dot(v, v));
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
static int v_near(const float *a, const float *b, float dist)
{
    float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    return v_dot(d, d) < dist * dist;
}
static void yaw_pitch(const float *k, float *yaw, float *pitch)
{
    float f[3] = { k[0], k[1], k[2] };
    v_norm(f);
    *yaw = atan2f(f[1], f[0]);
    *pitch = asinf(f[2] < -1.0f ? -1.0f : f[2] > 1.0f ? 1.0f : f[2]);
}

/* Rows I, J, K of the camera from yaw / pitch without roll, keeping the
 * signs the engine uses for I and J. */
static void write_rows(uint32_t cam, float yaw, float pitch)
{
    float f[3], r[3], u[3], i0[3], j0[3], si, sj;
    const float z[3] = { 0.0f, 0.0f, 1.0f };
    int i;
    f[0] = cosf(pitch) * cosf(yaw); f[1] = cosf(pitch) * sinf(yaw); f[2] = sinf(pitch);
    v_cross(f, z, r); v_norm(r); v_cross(r, f, u);
    for (i = 0; i < 3; i++) {
        i0[i] = GMEMF(cam + CAM_ROW_I + 4u * i);
        j0[i] = GMEMF(cam + CAM_ROW_J + 4u * i);
    }
    si = v_dot(i0, r) < 0.0f ? -1.0f : 1.0f;
    sj = v_dot(j0, u) < 0.0f ? -1.0f : 1.0f;
    for (i = 0; i < 3; i++) {
        GMEMF(cam + CAM_ROW_I + 4u * i) = si * r[i];
        GMEMF(cam + CAM_ROW_J + 4u * i) = sj * u[i];
        GMEMF(cam + CAM_ROW_K + 4u * i) = f[i];
    }
}

static int menu_world(void)
{
    static double next;
    static int in;
    double now = fc_now();
    if (now >= next) {
        char wow[64];
        resot_current_wow(wow, sizeof wow);
        in = !strcmp(wow, "Menu");
        next = now + 0.25;
    }
    return in;
}

static int menucam_get(const char *key, char *out, unsigned size)
{
#ifdef _WIN32
    return resot_options_get_string("Menu", key, out, size);
#else
    (void)key; if (size) out[0] = 0; return 0;
#endif
}

/* The modes: the PC port's poses (default), the player's own, the original. */
enum { MC_PC, MC_OWN, MC_ORIGINAL };
static const char *const s_mc_modes[3] = { "pc", "own", "original" };
/* The user's menu picture and flight end of popsot-fixes on PC (popfix.ini,
 * [menus] camera_start / camera_end, 2026-10-25): the same world, the same
 * coordinates. */
static const MenuPose s_pc_pose[2] = {
    { 1, { -82.927f, 2.869f, -2.608f }, -2.9409f, 0.1740f },
    { 1, { -93.671f, 0.653f, 4.524f }, -2.7889f, -0.0786f },
};

/* The poses and offsets of the current mode. */
static void menucam_use(void)
{
    int i;
    for (i = 0; i < 2; i++)
        s_mc.pose[i] = s_mc.mode == MC_PC ? s_pc_pose[i] : s_mc.own[i];
    if (s_mc.mode == MC_ORIGINAL) {
        s_mc.pose[0].set = s_mc.pose[1].set = 0;
        s_mc.forward = s_mc.up = s_mc.side = 0.0f;
    } else {
        s_mc.forward = s_mc.offset[0]; s_mc.up = s_mc.offset[1]; s_mc.side = s_mc.offset[2];
    }
    s_cheat_revision++;
}

static void menucam_load(void)
{
    char v[96];
    const char *e;
    int i;
    if (s_mc.loaded) return;
    s_mc.loaded = 1;
    s_mc.offset[0] = 3.5f; s_mc.offset[1] = 6.25f; s_mc.offset[2] = 0.0f;
    if (menucam_get("CameraForward", v, sizeof v)) s_mc.offset[0] = (float)atof(v);
    if (menucam_get("CameraUp", v, sizeof v)) s_mc.offset[1] = (float)atof(v);
    if (menucam_get("CameraSide", v, sizeof v)) s_mc.offset[2] = (float)atof(v);
    for (i = 0; i < 2; i++) {
        MenuPose *p = &s_mc.own[i];
        /* RESOT_MENU_CAMERA_START / _END=<x y z yaw pitch> for test runs */
        e = getenv(i ? "RESOT_MENU_CAMERA_END" : "RESOT_MENU_CAMERA_START");
        if (e && *e) snprintf(v, sizeof v, "%s", e);
        else if (!menucam_get(s_pose_keys[i], v, sizeof v)) continue;
        p->set = sscanf(v, "%f %f %f %f %f", &p->pos[0], &p->pos[1], &p->pos[2], &p->yaw, &p->pitch) == 5;
    }
    s_mc.mode = MC_PC;
    if (menucam_get("CameraMode", v, sizeof v))
        for (i = 0; i < 3; i++)
            if (!_stricmp(v, s_mc_modes[i])) s_mc.mode = i;
    if ((e = getenv("RESOT_MENU_CAMERA")) && *e) {          /* pc, own, original or 0 */
        s_mc.mode = *e == '0' ? MC_ORIGINAL : s_mc.mode;
        for (i = 0; i < 3; i++)
            if (!_stricmp(e, s_mc_modes[i])) s_mc.mode = i;
    }
    if (s_mc.mode == MC_OWN && !s_mc.own[0].set && !s_mc.own[1].set)
        s_mc.mode = MC_PC;
    menucam_use();
    fprintf(stderr, "[MENUCAM] mode %s, start %s, end %s, offsets %.2f %.2f %.2f\n",
            s_mc_modes[s_mc.mode], s_mc.pose[0].set ? "set" : "none", s_mc.pose[1].set ? "set" : "none",
            s_mc.forward, s_mc.up, s_mc.side);
}

static void menucam_set(const char *key, const char *value)
{
#ifdef _WIN32
    if (!resot_options_set_string("Menu", key, value))
        fprintf(stderr, "[MENUCAM] %s not saved (scripted run or no INI)\n", key);
#else
    (void)key; (void)value;
#endif
}

/* The free camera's pose as the menu picture (0) or the flight's end (1):
 * the player's own poses from now on; the other one starts as the pose that
 * was shown. */
static void menucam_store(int which)
{
    MenuPose *p = &s_mc.own[which];
    char v[96];
    menucam_load();
    if (!s_fc.started) return;
    if (s_mc.mode != MC_OWN && s_mc.pose[!which].set && !s_mc.own[!which].set) {
        const MenuPose *o = &s_mc.pose[!which];
        s_mc.own[!which] = *o;
        snprintf(v, sizeof v, "%.3f %.3f %.3f %.4f %.4f", o->pos[0], o->pos[1], o->pos[2], o->yaw, o->pitch);
        menucam_set(s_pose_keys[!which], v);
    }
    memcpy(p->pos, s_fc.pos, sizeof p->pos);
    p->yaw = s_fc.yaw;
    p->pitch = s_fc.pitch;
    p->set = 1;
    snprintf(v, sizeof v, "%.3f %.3f %.3f %.4f %.4f", p->pos[0], p->pos[1], p->pos[2], p->yaw, p->pitch);
    menucam_set(s_pose_keys[which], v);
    s_mc.mode = MC_OWN;
    menucam_set("CameraMode", s_mc_modes[MC_OWN]);
    menucam_use();
    fprintf(stderr, "[MENUCAM] %s %s\n", s_pose_keys[which], v);
}

/* The Debug page's entry: PC port -> original -> own (when stored) -> PC port. */
static void menucam_cycle(void)
{
    menucam_load();
    s_mc.mode = s_mc.mode == MC_PC ? MC_ORIGINAL
              : s_mc.mode == MC_ORIGINAL && (s_mc.own[0].set || s_mc.own[1].set) ? MC_OWN : MC_PC;
    menucam_set("CameraMode", s_mc_modes[s_mc.mode]);
    menucam_use();
    fprintf(stderr, "[MENUCAM] mode %s\n", s_mc_modes[s_mc.mode]);
}

static void menucam_label(char *out, unsigned size)
{
    static const char *const de_names[3] = { "PC-Port", "eigene", "Original" };
    static const char *const en_names[3] = { "PC port", "own", "original" };
    int de = resot_text_language() == 4;
    menucam_load();
    if (de)
        snprintf(out, size, "Men" "\xFC" "kamera: %s", de_names[s_mc.mode]);
    else
        snprintf(out, size, "Menu camera: %s", en_names[s_mc.mode]);
}

/* Turns k so that the Prince, seen from `from` along k, is seen in the same
 * place from `to`. */
static void aim_at_prince(float *k, const float *from, const float *to)
{
    float a[3], b[3], axis[3], sn, angle, c, s, axv[3], d;
    int i;
    for (i = 0; i < 3; i++) { a[i] = s_prince_balcony[i] - from[i]; b[i] = s_prince_balcony[i] - to[i]; }
    v_norm(a); v_norm(b);
    v_cross(a, b, axis);
    sn = sqrtf(v_dot(axis, axis));
    if (sn < 1e-6f) return;
    for (i = 0; i < 3; i++) axis[i] /= sn;
    angle = atan2f(sn, v_dot(a, b));
    c = cosf(angle); s = sinf(angle);
    v_cross(axis, k, axv);
    d = v_dot(axis, k);
    for (i = 0; i < 3; i++) k[i] = k[i] * c + axv[i] * s + axis[i] * d * (1.0f - c);
}

static float wrap_pi(float a)
{
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}

/* Where the main camera should be (0 = leave it); *look: also turn it. */
static int menucam_target(const float *base, const float *k, float *out, int *look, float *yaw, float *pitch)
{
    const float zero[3] = { 0.0f, 0.0f, 0.0f };
    const MenuPose *sp = &s_mc.pose[0], *ep = &s_mc.pose[1];
    float d[3], e[3], off[3], s, endyaw, endpitch, w;
    const float *end;
    int i;
    *look = 0;
    if (!sp->set && !ep->set && s_mc.forward == 0.0f && s_mc.up == 0.0f && s_mc.side == 0.0f) return 0;
    if (v_near(base, zero, 0.01f)) return 0;
    if (v_near(base, s_menu_cam, 0.05f)) {
        if (sp->set) {
            memcpy(s_mc.start, sp->pos, sizeof s_mc.start);
            s_mc.start_yaw = sp->yaw;
            s_mc.start_pitch = sp->pitch;
            *look = 1;
        } else {
            float right[3] = { k[1], -k[0], 0.0f };          /* k x Z */
            v_norm(right);
            for (i = 0; i < 3; i++) s_mc.start[i] = base[i] + s_mc.forward * k[i] + s_mc.side * right[i];
            s_mc.start[2] += s_mc.up;
            yaw_pitch(k, &s_mc.start_yaw, &s_mc.start_pitch);
        }
        memcpy(out, s_mc.start, sizeof s_mc.start);
        *yaw = s_mc.start_yaw;
        *pitch = s_mc.start_pitch;
        s_mc.flight_done = s_mc.arrived = s_mc.handover = 0;
        return 1;
    }
    if (s_mc.handover) {                 /* the game's camera moves on: blend over to it */
        float t = (float)((fc_now() - s_mc.handover_start) / 0.75), gy, gp;
        if (t >= 1.0f) { s_mc.handover = 0; return 0; }
        w = t * t * (3.0f - 2.0f * t);
        yaw_pitch(k, &gy, &gp);
        for (i = 0; i < 3; i++) out[i] = ep->pos[i] + w * (base[i] - ep->pos[i]);
        *yaw = ep->yaw + w * wrap_pi(gy - ep->yaw);
        *pitch = ep->pitch + w * (gp - ep->pitch);
        *look = 1;
        return 1;
    }
    if (s_mc.flight_done) return 0;
    if (s_mc.arrived) {                  /* the end pose, while the game's camera stays there */
        if (!ep->set) { s_mc.flight_done = 1; return 0; }
        if (!v_near(base, s_flight_end, 0.1f)) {
            int blend = v_near(base, s_flight_end, 3.0f);
            s_mc.flight_done = 1;
            fprintf(stderr, "[MENUCAM] the game's camera leaves the flight's end (%s)\n",
                    blend ? "blending over" : "cut");
            if (!blend) return 0;
            s_mc.handover = 1;
            s_mc.handover_start = fc_now();
            return menucam_target(base, k, out, look, yaw, pitch);
        }
        memcpy(out, ep->pos, sizeof ep->pos);
        *yaw = ep->yaw; *pitch = ep->pitch; *look = 1;
        return 1;
    }
    for (i = 0; i < 3; i++) { d[i] = base[i] - s_menu_cam[i]; e[i] = s_flight_end[i] - s_menu_cam[i]; }
    s = v_dot(d, e) / v_dot(e, e);
    for (i = 0; i < 3; i++) off[i] = d[i] - s * e[i];
    if (v_dot(off, off) > 16.0f || s < -0.2f) return 0;     /* not on the flight path */
    if (s >= 0.999f) {
        if (ep->set) { s_mc.arrived = 1; return menucam_target(base, k, out, look, yaw, pitch); }
        s_mc.flight_done = 1;            /* the game's camera until the menu returns */
        return 0;
    }
    if (s < 0.0f) s = 0.0f;
    end = ep->set ? ep->pos : s_flight_end;
    for (i = 0; i < 3; i++) out[i] = s_mc.start[i] + s * (end[i] - s_mc.start[i]);
    if (ep->set) {
        endyaw = ep->yaw; endpitch = ep->pitch;
        w = s * s * (3.0f - 2.0f * s);
    } else {
        float aimed[3] = { k[0], k[1], k[2] };
        aim_at_prince(aimed, base, out);
        yaw_pitch(aimed, &endyaw, &endpitch);
        w = s / 0.15f;                   /* the aim comes in during the first part of the flight */
        if (w > 1.0f) w = 1.0f;
        w = w * w * (3.0f - 2.0f * w);
    }
    *yaw = s_mc.start_yaw + w * wrap_pi(endyaw - s_mc.start_yaw);
    *pitch = s_mc.start_pitch + w * (endpitch - s_mc.start_pitch);
    *look = 1;
    return 1;
}

/* From CAM_Inverse's entry, main camera, free camera off. */
static void menucam_apply(uint32_t cam)
{
    float pos[3], k[3], base[3], target[3], yaw, pitch;
    int i, ours, look;
    static int logged;
    if (!menu_world()) {
        s_mc.have_out = 0;
        return;
    }
    menucam_load();
    if (!logged++) fprintf(stderr, "[MENUCAM] main menu world\n");
    for (i = 0; i < 3; i++) pos[i] = GMEMF(cam + CAM_POS + 4u * i);
    ours = cam == s_mc.cam && s_mc.have_out && !memcmp(pos, s_mc.out, sizeof pos);
    if (ours)                            /* the game left our camera as it was: undo our turn */
        for (i = 0; i < 12; i++) GMEMF(cam + CAM_ROW_I + 4u * i) = s_mc.base_rows[i];
    memcpy(base, ours ? s_mc.base : pos, sizeof base);
    for (i = 0; i < 3; i++) k[i] = GMEMF(cam + CAM_ROW_K + 4u * i);
    if (menucam_target(base, k, target, &look, &yaw, &pitch)) {
        for (i = 0; i < 12; i++) s_mc.base_rows[i] = GMEMF(cam + CAM_ROW_I + 4u * i);
        if (look) write_rows(cam, yaw, pitch);
        for (i = 0; i < 3; i++) GMEMF(cam + CAM_POS + 4u * i) = target[i];
        s_mc.cam = cam;
        memcpy(s_mc.base, base, sizeof base);
        memcpy(s_mc.out, target, sizeof target);
        s_mc.have_out = 1;
    } else if (ours) {
        for (i = 0; i < 3; i++) GMEMF(cam + CAM_POS + 4u * i) = s_mc.base[i];
        s_mc.have_out = 0;
    }
}

/* The reSOT Debug page: the F5 panel, the free camera and the world freeze
 * (above), and the game speed. Retail's frame clock (sub_000DB740) scales
 * the frame time by two factors before it is stored (0x000DB850, 0x000DB861:
 * the 64-bit microseconds at 0x7654E0 and TIM_gf_dt at 0x470D74):
 * TIM_gf_SpeedFactor at 0x43B12C, which the scripts set (the sand powers'
 * slow motion) and ENG_ReinitOneWorld puts back to 1.0, and a second one at
 * 0x43B100 that is 1.0 in the XBE and never written by the game. The game
 * speed is written there, so the scripts' own slow motion still works on
 * top of it. Not saved. */
#define CLOCK_FACTOR 0x0043B100u
extern int xbox_FramebufferFpsVisible(void);
extern void xbox_FramebufferToggleDebugPanel(void);
static const int s_speeds[] = { 100, 50, 25 };
static int s_speed;

static void debug_label(int item, char *out, unsigned size)
{
    int de = resot_text_language() == 4;
    const char *on = de ? "An" : "On", *off = de ? "Aus" : "Off";
    switch (item) {
    case DBG_PANEL:
        snprintf(out, size, "%s: %s", de ? "Info-Anzeige (F5)" : "Info panel (F5)",
                 xbox_FramebufferFpsVisible() ? on : off);
        break;
    case DBG_FREECAM:
        snprintf(out, size, "%s: %s", de ? "Freie Kamera (Back, F8)" : "Free camera (Back, F8)",
                 s_fc.on ? on : off);
        break;
    case DBG_FREEZE:
        snprintf(out, size, "%s: %s", de ? "Welt anhalten (Y, P)" : "Freeze world (Y, P)",
                 s_fc.frozen ? on : off);
        break;
    case DBG_SPEED:
        snprintf(out, size, "%s: %d%%", de ? "Spielgeschwindigkeit" : "Game speed", s_speeds[s_speed]);
        break;
    case DBG_MENUCAM:
        menucam_label(out, size);
        break;
    default:
        snprintf(out, size, "?");
    }
}

static void debug_activate(int item)
{
    switch (item) {
    case DBG_PANEL:
        xbox_FramebufferToggleDebugPanel();
        break;
    case DBG_FREECAM:
        if (!freecam_enabled())
            break;
        s_fc.on = !s_fc.on;
        s_fc.started = 0;
        fprintf(stderr, "[FREECAM] %s (Debug page)\n", s_fc.on ? "on" : "off");
        break;
    case DBG_FREEZE:
        s_fc.frozen = !s_fc.frozen;
        GMEM32(FORCE_PAUSE_ENGINE) = (uint32_t)s_fc.frozen;
        fprintf(stderr, "[FREECAM] world %s (Debug page)\n", s_fc.frozen ? "frozen" : "running");
        break;
    case DBG_SPEED:
        s_speed = (s_speed + 1) % (int)(sizeof s_speeds / sizeof s_speeds[0]);
        fprintf(stderr, "[DEBUG] game speed %d%%\n", s_speeds[s_speed]);
        break;
    case DBG_MENUCAM:
        menucam_cycle();
        break;
    }
    s_cheat_revision++;
}

/* Changes whenever a label of the Cheats or Debug page may read differently. */
static long debug_revision(void)
{
    return s_cheat_revision + (xbox_FramebufferFpsVisible() ? 1L << 20 : 0)
           + (s_fc.on ? 1L << 21 : 0) + (s_fc.frozen ? 1L << 22 : 0);
}

static void debug_tick(void)
{
    static float applied = 1.0f;
    float want = (float)s_speeds[s_speed] / 100.0f;
    static int configured;
    if (!configured) {                           /* RESOT_GAME_SPEED=50 or 25 */
        const char *e = getenv("RESOT_GAME_SPEED");
        int i;
        configured = 1;
        for (i = 0; e && *e && i < (int)(sizeof s_speeds / sizeof s_speeds[0]); i++)
            if (atoi(e) == s_speeds[i]) s_speed = i;
        want = (float)s_speeds[s_speed] / 100.0f;
    }
    if (want != applied) {
        GMEMF(CLOCK_FACTOR) = want;
        applied = want;
        fprintf(stderr, "[DEBUG] clock factor %.2f\n", want);
    }
    if (getenv("RESOT_TIMER_DIAG")) {            /* game clock (0x758C80) against the wall */
        static int polls;
        static double start;
        if (!start) start = fc_now();
        if (++polls % 600 == 0)
            fprintf(stderr, "[TIME] game clock %.2f s, wall %.2f s\n", GMEMF(0x00758C80u),
                    fc_now() - start);
    }
}
