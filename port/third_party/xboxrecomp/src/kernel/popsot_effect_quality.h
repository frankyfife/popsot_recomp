/* POPSoT: one quality budget for auxiliary render targets, on both hosts.
 * This changes raster resolution, never guest filter offsets or animation.
 * Unknown material draws must remain present even at Off: removing a water
 * material, for example, also removes its base colour/depth. Only independently
 * identified additive effects may be omitted by the caller. */
#ifndef POPSOT_EFFECT_QUALITY_H
#define POPSOT_EFFECT_QUALITY_H
#include <stdint.h>

/* POPSoT: Ultra (4) renders auxiliary targets at the internal resolution
 * instead of the guest's. Positions and texel coordinates are normalised by
 * guest sizes (hdr[0], tscale), so the scaled targets stay in register.
 * Except the screen-fraction chains (popsot_effect_screen_chain): their
 * blur and bloom passes take a few point taps a fixed number of guest texels
 * apart, and a guest texel of a 160x120 to 10x7 level is a wide, already
 * blurred area; at internal resolution each tap hits sharp detail and the
 * taps show as separate copies, at the 20x15 and 10x7 levels a third of the
 * screen apart (user report 2026-10-25: window glow repeated to the left;
 * a capture's 160x120 level showed the Prince two and three times). */
#define POPSOT_EFFECT_ULTRA 4

/* A 4:3 target below the display width: a fraction of the screen (320x240
 * down to the 10x7 bloom tail), as the blur and bloom chains use. */
static int popsot_effect_screen_chain(uint32_t width, uint32_t height)
{
    int d = (int)(width * 3u) - (int)(height * 4u);
    return width < 640 && height && width >= 8 && d >= -4 && d <= 4;
}

static unsigned popsot_effect_percent(int quality, uint32_t width, uint32_t height)
{
    /* POPSoT: protect display/history buffers and tiny bloom pyramid levels.
     * Use the viewport extent, not the padded pitch (retail 40 pixels: 48). */
    if (width < 40 || height < 30 || width >= 640 || height > 512) return 100;
    return quality == 0 ? 25 : quality == 1 ? 50 : quality == 2 ? 75 : 100;
}

static int popsot_shadow_target(uint32_t format, uint32_t pitch,
    uint32_t x, uint32_t y, uint32_t width, uint32_t height,
    uint32_t zeta, int depth_test, int blend)
{
    /* POPSoT: independently observed demo and retail caster passes:
     * RGB565 256x256, 512-byte pitch, no depth surface/test or blending.
     * Do not treat arbitrary square textures or small viewports as shadows. */
    return (format & 15) == 3 && pitch == 512 && !x && !y
        && width == 256 && height == 256 && !zeta && !depth_test && !blend;
}
#endif
