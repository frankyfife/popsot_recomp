/* RE-SoT: texture packs. A guest texture is identified by a 64-bit FNV-1a
 * hash of its decoded level-0 pixels (BGRA, as both hosts upload them), its
 * size and its NV2A colour format, so the key does not depend on swizzle,
 * palette or address. A file <key>.tga (16 hex digits, uncompressed 32-bit
 * BGRA) in the pack folder replaces it at any size; the hosts give it a full
 * mip chain. The texture coordinates stay valid because both hosts sample
 * normalised (linear textures are normalised by their guest size).
 *
 * Pack folder: RESOT_TEXTURE_PACK, else "textures" beside the executable.
 * RESOT_TEXTURE_DUMP=1 writes every new texture once to <pack>/dump/ as
 * <key>_<w>x<h>_fmt<NN>.tga, for building packs (tools/hd-ui.py). Cube maps
 * are not replaced. */
#ifndef RESOT_TEXPACK_H
#define RESOT_TEXPACK_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

typedef struct {
    int ready, dump;
    char dir[600];
    uint64_t *keys;
    unsigned nkeys;
    uint64_t dumped[1024];
    unsigned ndumped;
} ResotTexPack;
static ResotTexPack s_texpack;

static int texpack_key_cmp(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void texpack_init(void)
{
    const char *e = getenv("RESOT_TEXTURE_PACK");
    s_texpack.ready = 1;
    e = getenv("RESOT_TEXTURE_DUMP");
    s_texpack.dump = e && *e && *e != '0';
    e = getenv("RESOT_TEXTURE_PACK");
    if (e && *e) {
        snprintf(s_texpack.dir, sizeof s_texpack.dir, "%s", e);
    } else {
#ifdef _WIN32
        char exe[MAX_PATH], *slash;
        DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
        if (!n || n >= sizeof exe) return;
        slash = strrchr(exe, '\\');
        if (slash) *slash = 0;
        snprintf(s_texpack.dir, sizeof s_texpack.dir, "%s\\textures", exe);
#else
        snprintf(s_texpack.dir, sizeof s_texpack.dir, "textures");
#endif
    }
#ifdef _WIN32
    {
        char pattern[640];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        unsigned cap = 0;
        snprintf(pattern, sizeof pattern, "%s\\*.tga", s_texpack.dir);
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            uint64_t key = 0;
            int i, ok = strlen(fd.cFileName) == 20;
            for (i = 0; ok && i < 16; i++) {
                char c = fd.cFileName[i];
                int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (v < 0) ok = 0; else key = key << 4 | (uint64_t)v;
            }
            if (!ok) continue;
            if (s_texpack.nkeys == cap) {
                uint64_t *k = (uint64_t *)realloc(s_texpack.keys, sizeof *k * (cap = cap ? cap * 2 : 64));
                if (!k) break;
                s_texpack.keys = k;
            }
            s_texpack.keys[s_texpack.nkeys++] = key;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#endif
    if (s_texpack.nkeys)
        qsort(s_texpack.keys, s_texpack.nkeys, sizeof *s_texpack.keys, texpack_key_cmp);
    if (s_texpack.nkeys || s_texpack.dump)
        fprintf(stderr, "[TEXPACK] %u replacement(s) in %s%s\n", s_texpack.nkeys, s_texpack.dir,
                s_texpack.dump ? "; dumping new textures" : "");
}

static uint64_t texpack_key(const uint32_t *px, uint32_t w, uint32_t h, uint32_t fmt)
{
    uint64_t k = 0xCBF29CE484222325ull;
    uint32_t head[3], i;
    const uint8_t *p;
    size_t n;
    head[0] = w; head[1] = h; head[2] = fmt;
    p = (const uint8_t *)head;
    for (i = 0; i < sizeof head; i++) { k ^= p[i]; k *= 0x100000001B3ull; }
    p = (const uint8_t *)px;
    n = (size_t)w * h * 4;
    while (n--) { k ^= *p++; k *= 0x100000001B3ull; }
    return k;
}

static void texpack_write_tga(const char *path, const uint32_t *px, uint32_t w, uint32_t h)
{
    uint8_t hdr[18];
    FILE *f = fopen(path, "wb");
    if (!f) return;
    memset(hdr, 0, sizeof hdr);
    hdr[2] = 2;
    hdr[12] = (uint8_t)w; hdr[13] = (uint8_t)(w >> 8);
    hdr[14] = (uint8_t)h; hdr[15] = (uint8_t)(h >> 8);
    hdr[16] = 32; hdr[17] = 0x28;                /* 8 alpha bits, top-left origin */
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(px, 4, (size_t)w * h, f);
    fclose(f);
}

/* Uncompressed 32-bit TGA into top-down BGRA; NULL when unreadable. */
static uint32_t *texpack_read_tga(const char *path, uint32_t *w, uint32_t *h)
{
    uint8_t hdr[18];
    uint32_t *px = NULL, y, ww, hh;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr || hdr[2] != 2 || hdr[16] != 32) goto done;
    ww = hdr[12] | (uint32_t)hdr[13] << 8;
    hh = hdr[14] | (uint32_t)hdr[15] << 8;
    if (!ww || !hh || ww > 16384 || hh > 16384) goto done;
    fseek(f, hdr[0], SEEK_CUR);
    px = (uint32_t *)malloc((size_t)ww * hh * 4);
    if (!px) goto done;
    for (y = 0; y < hh; y++) {
        uint32_t row = (hdr[17] & 0x20) ? y : hh - 1 - y;
        if (fread(px + (size_t)row * ww, 4, ww, f) != ww) { free(px); px = NULL; goto done; }
    }
    *w = ww; *h = hh;
done:
    fclose(f);
    return px;
}

/* The replacement for these decoded pixels (malloc'ed, top-down BGRA), or
 * NULL. Also does the dump when asked. */
static uint32_t *texpack_lookup(const uint32_t *px, uint32_t w, uint32_t h, uint32_t fmt,
                                uint32_t *rw, uint32_t *rh)
{
    uint64_t key;
    char path[700];
    if (!s_texpack.ready) texpack_init();
    if (!s_texpack.nkeys && !s_texpack.dump) return NULL;
    key = texpack_key(px, w, h, fmt);
    if (s_texpack.dump && s_texpack.ndumped < 1024) {
        unsigned i;
        for (i = 0; i < s_texpack.ndumped && s_texpack.dumped[i] != key; i++)
            ;
        if (i == s_texpack.ndumped) {
            s_texpack.dumped[s_texpack.ndumped++] = key;
#ifdef _WIN32
            snprintf(path, sizeof path, "%s\\dump", s_texpack.dir);
            CreateDirectoryA(s_texpack.dir, NULL);
            CreateDirectoryA(path, NULL);
#endif
            snprintf(path, sizeof path, "%s/dump/%016llx_%ux%u_fmt%02X.tga", s_texpack.dir,
                     (unsigned long long)key, w, h, fmt);
            texpack_write_tga(path, px, w, h);
        }
    }
    if (!s_texpack.nkeys || !bsearch(&key, s_texpack.keys, s_texpack.nkeys, sizeof key, texpack_key_cmp))
        return NULL;
    snprintf(path, sizeof path, "%s/%016llx.tga", s_texpack.dir, (unsigned long long)key);
    {
        uint32_t *rep = texpack_read_tga(path, rw, rh);
        static unsigned reports;
        if (reports++ < 32)
            fprintf(stderr, "[TEXPACK] %016llx %ux%u fmt %02X -> %ux%u%s\n", (unsigned long long)key,
                    w, h, fmt, rep ? *rw : 0, rep ? *rh : 0, rep ? "" : " (unreadable)");
        return rep;
    }
}
#endif
