/**
 * XAudio2 Audio Output Backend
 *
 * Provides low-latency audio output via XAudio2 (Win7+).
 * Called from the APU monitor frame to submit mixed samples.
 * Falls back gracefully if XAudio2 is unavailable.
 *
 * POPSoT: stereo or 5.1, and the Xbox's I3DL2 room reverb. The GP DSP that
 * runs the reverb on the Xbox (the dsstdfx.bin effects image) is not
 * emulated; instead the I3DL2 send (mixbin 10) is the last channel of the
 * source voice, which sends it to a one-channel submix with the XAudio2
 * reverb (100 % wet, 5.1 output), set from the game's DSI3DL2LISTENER with
 * ReverbConvertI3DL2ToNative, as FuturamaPC does (its
 * src/host/audio/reverb_i3dl2.h). The speakers go to the master voice:
 * stereo as before (two-channel master), 5.1 to a master with the device's
 * channel count, mapped explicitly (identity, or front + 0.7071 * (centre +
 * rear) on two channels).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apu_xaudio2.h"

/* The XAudio2 backend is Windows-only. On Linux all xa2_* functions are
 * stubbed to report inactive; real audio output via SDL2 comes later. */
#if defined(_WIN32)

#define COBJMACROS
#include <windows.h>
#include <mmreg.h>
#include <xaudio2.h>
#include <xaudio2fx.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

#define XA2_SAMPLE_RATE   48000
#define XA2_MAX_CHANNELS  8
/* POPSoT: one buffer is one APU monitor frame (256 samples, 5.33 ms), which
 * is what the frame thread produces per tick of its own clock. The queue is
 * the only slack between that clock and the sound card's: it is primed with
 * silence when it runs dry and a frame is dropped when it grows past
 * XA2_QUEUE_MAX, so latency stays between about 20 and 85 ms. */
#define XA2_BUF_SAMPLES   256
#define XA2_NUM_BUFS      32
#define XA2_QUEUE_PRIME   4
#define XA2_QUEUE_MAX     16

static IXAudio2               *g_xa2 = NULL;
static IXAudio2MasteringVoice *g_xa2_master = NULL;
static IXAudio2SourceVoice    *g_xa2_source = NULL;
static IXAudio2SubmixVoice    *g_xa2_reverb = NULL;
static int16_t                 g_xa2_bufs[XA2_NUM_BUFS][XA2_BUF_SAMPLES * XA2_MAX_CHANNELS];
static int                     g_xa2_next_buf = 0;
static int                     g_xa2_initialized = 0;
static int                     g_xa2_frames_written = 0;
static int                     g_xa2_queued, g_xa2_dropped, g_xa2_starved;
/* the configuration (xa2_configure) and what init made of it */
static int                     g_xa2_want_speakers = 2, g_xa2_want_reverb = 1;
static int                     g_xa2_speakers = 2, g_xa2_send = 0, g_xa2_out_channels = 0;
static volatile LONG           g_xa2_reverb_on = 1;
static uint8_t                 g_xa2_listener[48];
static int                     g_xa2_have_listener;
/* POPSoT: a volume meter on the master voice, so the log says what reaches
 * the device and not only what was submitted. */
static int                     g_xa2_meter;
static float                   g_xa2_peak;

void xa2_configure(int speakers, int reverb)
{
    g_xa2_want_speakers = speakers == 6 ? 6 : 2;
    g_xa2_want_reverb = reverb != 0;
    InterlockedExchange(&g_xa2_reverb_on, reverb != 0);
}

void xa2_layout(int *speakers, int *reverb_send)
{
    *speakers = g_xa2_speakers;
    *reverb_send = g_xa2_send;
}

int xa2_output_channels(void)
{
    return g_xa2_out_channels;
}

/* speaker j (FL FR FC LFE BL BR) -> output channel i */
static float speaker_gain(int speakers, int j, int out, int i)
{
    if (speakers == 2)
        return i == j ? 1.0f : 0.0f;
    if (out >= 6)
        return i == j ? 1.0f : 0.0f;                     /* 5.1, 7.1: the same first six */
    if (out == 2) {
        if (j == 0 || j == 4) return i == 0 ? 1.0f : 0.0f;
        if (j == 1 || j == 5) return i == 1 ? 1.0f : 0.0f;
        if (j == 2) return 0.7071f;
        return 0.0f;                                       /* LFE: as FuturamaPC, dropped */
    }
    return i == 0 ? 0.5f : 0.0f;                          /* mono */
}

static void apply_reverb_send(void)
{
    float m[XA2_MAX_CHANNELS];
    int c, chans = g_xa2_speakers + g_xa2_send;
    if (!g_xa2_source || !g_xa2_reverb) return;
    for (c = 0; c < chans; c++)
        m[c] = c == g_xa2_speakers && InterlockedCompareExchange(&g_xa2_reverb_on, 0, 0) ? 1.0f : 0.0f;
    {
        HRESULT hr = IXAudio2SourceVoice_SetOutputMatrix(g_xa2_source, (IXAudio2Voice *)g_xa2_reverb,
                                                         (UINT32)chans, 1, m, XAUDIO2_COMMIT_NOW);
        if (FAILED(hr)) fprintf(stderr, "[XA2] reverb send matrix rejected: 0x%08lX\n", hr);
    }
}

void xa2_set_reverb(int on)
{
    InterlockedExchange(&g_xa2_reverb_on, on != 0);
    if (g_xa2_initialized) apply_reverb_send();
}

/* The reverb's parameter block as XAudio2_9.dll 10.0.26100 reads it: 56
 * bytes, without the SideDelay byte that xaudio2fx.h declares for
 * _WIN32_WINNT >= WIN10 (57 bytes). Measured (GetEffectParameters of the
 * defaults, 0xCD fill): 5000.0 RoomFilterFreq at offset 20, not 21, the
 * position bytes 6 6 27 at offsets 10-12. Our 57-byte block put every
 * value one field off and the effect silenced the whole output (the
 * master's volume meter read 0). Which one the DLL has is told by the
 * default PositionMatrixLeft (27) at offset 12 (legacy) or 13. */
#pragma pack(push, 1)
typedef struct {
    float WetDryMix;
    UINT32 ReflectionsDelay;
    BYTE ReverbDelay, RearDelay, PositionLeft, PositionRight, PositionMatrixLeft, PositionMatrixRight,
         EarlyDiffusion, LateDiffusion, LowEQGain, LowEQCutoff, HighEQGain, HighEQCutoff;
    float RoomFilterFreq, RoomFilterMain, RoomFilterHF, ReflectionsGain, ReverbGain, DecayTime, Density, RoomSize;
    BOOL DisableLateField;
} ReverbLegacy;
#pragma pack(pop)
static int g_xa2_reverb_legacy;

static void apply_listener(void)
{
    XAUDIO2FX_REVERB_I3DL2_PARAMETERS i3;
    XAUDIO2FX_REVERB_PARAMETERS native;
    int32_t l[12];
    float f[12];
    HRESULT hr;
    if (!g_xa2_reverb || !g_xa2_have_listener) return;
    memcpy(l, g_xa2_listener, 48);
    memcpy(f, g_xa2_listener, 48);
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define CLAMPF(v, lo, hi, nan) ((v) != (v) ? (nan) : CLAMP(v, lo, hi))
    i3.WetDryMix = 100.0f;
    i3.Room = CLAMP(l[0], -10000, 0);
    i3.RoomHF = CLAMP(l[1], -10000, 0);
    i3.RoomRolloffFactor = CLAMPF(f[2], 0.0f, 10.0f, 0.0f);
    i3.DecayTime = CLAMPF(f[3], 0.1f, 20.0f, 1.49f);
    i3.DecayHFRatio = CLAMPF(f[4], 0.1f, 2.0f, 0.83f);
    i3.Reflections = CLAMP(l[5], -10000, 1000);
    i3.ReflectionsDelay = CLAMPF(f[6], 0.0f, 0.3f, 0.007f);
    i3.Reverb = CLAMP(l[7], -10000, 2000);
    i3.ReverbDelay = CLAMPF(f[8], 0.0f, 0.1f, 0.011f);
    i3.Diffusion = CLAMPF(f[9], 0.0f, 100.0f, 100.0f);
    i3.Density = CLAMPF(f[10], 0.0f, 100.0f, 100.0f);
    i3.HFReference = CLAMPF(f[11], 20.0f, 20000.0f, 5000.0f);
#undef CLAMPF
#undef CLAMP
    ReverbConvertI3DL2ToNative(&i3, &native, FALSE);
    if (native.DecayTime < XAUDIO2FX_REVERB_MIN_DECAY_TIME) native.DecayTime = XAUDIO2FX_REVERB_MIN_DECAY_TIME;
    if (native.ReflectionsGain > XAUDIO2FX_REVERB_MAX_REFLECTIONS_GAIN)
        native.ReflectionsGain = XAUDIO2FX_REVERB_MAX_REFLECTIONS_GAIN;
    if (native.ReverbGain > XAUDIO2FX_REVERB_MAX_REVERB_GAIN) native.ReverbGain = XAUDIO2FX_REVERB_MAX_REVERB_GAIN;
    if (g_xa2_reverb_legacy) {
        ReverbLegacy lg;
        lg.WetDryMix = native.WetDryMix;
        lg.ReflectionsDelay = native.ReflectionsDelay;
        lg.ReverbDelay = native.ReverbDelay;
        lg.RearDelay = native.RearDelay;
        lg.PositionLeft = native.PositionLeft;
        lg.PositionRight = native.PositionRight;
        lg.PositionMatrixLeft = native.PositionMatrixLeft;
        lg.PositionMatrixRight = native.PositionMatrixRight;
        lg.EarlyDiffusion = native.EarlyDiffusion;
        lg.LateDiffusion = native.LateDiffusion;
        lg.LowEQGain = native.LowEQGain;
        lg.LowEQCutoff = native.LowEQCutoff;
        lg.HighEQGain = native.HighEQGain;
        lg.HighEQCutoff = native.HighEQCutoff;
        lg.RoomFilterFreq = native.RoomFilterFreq;
        lg.RoomFilterMain = native.RoomFilterMain;
        lg.RoomFilterHF = native.RoomFilterHF;
        lg.ReflectionsGain = native.ReflectionsGain;
        lg.ReverbGain = native.ReverbGain;
        lg.DecayTime = native.DecayTime;
        lg.Density = native.Density;
        lg.RoomSize = native.RoomSize;
        lg.DisableLateField = native.DisableLateField;
        hr = IXAudio2SubmixVoice_SetEffectParameters(g_xa2_reverb, 0, &lg, sizeof lg, XAUDIO2_COMMIT_NOW);
    } else
        hr = IXAudio2SubmixVoice_SetEffectParameters(g_xa2_reverb, 0, &native, sizeof native, XAUDIO2_COMMIT_NOW);
    fprintf(stderr, "[XA2] I3DL2 listener: room %d hf %d, decay %.2f s (hf ratio %.2f), reflections %d"
                    " after %.3f s, reverb %d after %.3f s, diffusion %.0f density %.0f hf %.0f Hz (%s)\n",
            (int)i3.Room, (int)i3.RoomHF, i3.DecayTime, i3.DecayHFRatio, (int)i3.Reflections,
            i3.ReflectionsDelay, (int)i3.Reverb, i3.ReverbDelay, i3.Diffusion, i3.Density, i3.HFReference,
            SUCCEEDED(hr) ? "set" : "rejected");
}

void xa2_set_i3dl2_listener(const void *listener48)
{
    if (!listener48 || (g_xa2_have_listener && !memcmp(g_xa2_listener, listener48, 48))) return;
    memcpy(g_xa2_listener, listener48, 48);
    g_xa2_have_listener = 1;
    if (g_xa2_initialized) apply_listener();
}

/* The reverb submix: one input channel, the XAudio2 reverb with 5.1 output
 * (it does mono -> mono and mono -> 5.1), sent to the master like the 5.1
 * speakers. On failure the output has no reverb send. */
static int create_reverb(void)
{
    IUnknown *fx = NULL;
    XAUDIO2_EFFECT_DESCRIPTOR desc;
    XAUDIO2_EFFECT_CHAIN chain;
    float m[6 * XA2_MAX_CHANNELS];
    int i, j, fx_out = g_xa2_out_channels >= 2 ? 6 : 1;
    HRESULT hr = XAudio2CreateReverb(&fx, 0);
    if (FAILED(hr) || !fx) {
        fprintf(stderr, "[XA2] reverb effect not created: 0x%08lX\n", hr);
        return 0;
    }
    desc.pEffect = fx;
    desc.InitialState = TRUE;
    desc.OutputChannels = (UINT32)fx_out;
    chain.EffectCount = 1;
    chain.pEffectDescriptors = &desc;
    hr = IXAudio2_CreateSubmixVoice(g_xa2, &g_xa2_reverb, 1, XA2_SAMPLE_RATE, 0, 0, NULL, &chain);
    IUnknown_Release(fx);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] reverb submix not created: 0x%08lX\n", hr);
        g_xa2_reverb = NULL;
        return 0;
    }
    for (i = 0; i < g_xa2_out_channels; i++)
        for (j = 0; j < fx_out; j++)
            m[i * fx_out + j] = fx_out == 1 ? (i < 2 ? 1.0f : 0.0f) : speaker_gain(6, j, g_xa2_out_channels, i);
    hr = IXAudio2SubmixVoice_SetOutputMatrix(g_xa2_reverb, NULL, (UINT32)fx_out, (UINT32)g_xa2_out_channels, m,
                                             XAUDIO2_COMMIT_NOW);
    if (FAILED(hr)) fprintf(stderr, "[XA2] reverb output matrix rejected: 0x%08lX\n", hr);
    {
        unsigned char def[64];
        memset(def, 0xCD, sizeof def);
        hr = IXAudio2SubmixVoice_GetEffectParameters(g_xa2_reverb, 0, def, sizeof(XAUDIO2FX_REVERB_PARAMETERS));
        if (FAILED(hr) || (def[12] != 27 && def[13] != 27)) {
            fprintf(stderr, "[XA2] reverb parameter layout unknown (0x%08lX, %02X %02X): no reverb\n",
                    hr, def[12], def[13]);
            g_xa2_reverb->lpVtbl->DestroyVoice(g_xa2_reverb);
            g_xa2_reverb = NULL;
            return 0;
        }
        g_xa2_reverb_legacy = def[12] == 27;
        fprintf(stderr, "[XA2] reverb parameters: %s layout (%u bytes)\n",
                g_xa2_reverb_legacy ? "56-byte" : "57-byte",
                (unsigned)(g_xa2_reverb_legacy ? sizeof(ReverbLegacy) : sizeof(XAUDIO2FX_REVERB_PARAMETERS)));
    }
    return 1;
}

int xa2_init(void)
{
    HRESULT hr;
    int com_initialized, chans, i, j;
    WAVEFORMATEXTENSIBLE wfx;
    XAUDIO2_VOICE_DETAILS det;
    XAUDIO2_SEND_DESCRIPTOR sends[2];
    XAUDIO2_VOICE_SENDS send_list;
    float m[XA2_MAX_CHANNELS * XA2_MAX_CHANNELS];

    if (g_xa2_initialized) return 1;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "[XA2] CoInitializeEx failed: 0x%08lX\n", hr);
        return 0;
    }
    com_initialized = SUCCEEDED(hr);

    hr = XAudio2Create(&g_xa2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !g_xa2) {
        fprintf(stderr, "[XA2] XAudio2Create failed: 0x%08lX\n", hr);
        goto fail;
    }

    /* stereo: a two-channel master as before; 5.1: the device's channels */
    hr = IXAudio2_CreateMasteringVoice(g_xa2, &g_xa2_master,
        g_xa2_want_speakers == 6 ? XAUDIO2_DEFAULT_CHANNELS : 2, XA2_SAMPLE_RATE, 0, NULL, NULL, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] CreateMasteringVoice failed: 0x%08lX\n", hr);
        goto fail;
    }
    IXAudio2MasteringVoice_GetVoiceDetails(g_xa2_master, &det);
    g_xa2_out_channels = (int)det.InputChannels;
    if (g_xa2_out_channels < 1 || g_xa2_out_channels > XA2_MAX_CHANNELS) {
        fprintf(stderr, "[XA2] %d output channels not supported\n", g_xa2_out_channels);
        goto fail;
    }
    g_xa2_speakers = g_xa2_want_speakers;
    g_xa2_send = g_xa2_want_reverb && create_reverb();
    chans = g_xa2_speakers + g_xa2_send;

    memset(&wfx, 0, sizeof wfx);
    wfx.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels       = (WORD)chans;
    wfx.Format.nSamplesPerSec  = XA2_SAMPLE_RATE;
    wfx.Format.wBitsPerSample  = 16;
    wfx.Format.nBlockAlign     = (WORD)(chans * 2);
    wfx.Format.nAvgBytesPerSec = XA2_SAMPLE_RATE * wfx.Format.nBlockAlign;
    wfx.Format.cbSize          = sizeof wfx - sizeof wfx.Format;
    wfx.Samples.wValidBitsPerSample = 16;
    wfx.dwChannelMask          = 0;                   /* the output matrices place them */
    {   /* KSDATAFORMAT_SUBTYPE_PCM, without ksmedia.h's GUID instances */
        static const GUID pcm = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
        wfx.SubFormat = pcm;
    }

    sends[0].Flags = 0; sends[0].pOutputVoice = (IXAudio2Voice *)g_xa2_master;
    sends[1].Flags = 0; sends[1].pOutputVoice = (IXAudio2Voice *)g_xa2_reverb;
    send_list.SendCount = g_xa2_send ? 2 : 1;
    send_list.pSends = sends;
    hr = IXAudio2_CreateSourceVoice(g_xa2, &g_xa2_source,
        (const WAVEFORMATEX *)&wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] CreateSourceVoice failed: 0x%08lX\n", hr);
        goto fail;
    }
    /* speakers -> master; the reverb send channel only to the reverb */
    for (i = 0; i < g_xa2_out_channels; i++)
        for (j = 0; j < chans; j++)
            m[i * chans + j] = j < g_xa2_speakers ? speaker_gain(g_xa2_speakers, j, g_xa2_out_channels, i) : 0.0f;
    hr = IXAudio2SourceVoice_SetOutputMatrix(g_xa2_source, (IXAudio2Voice *)g_xa2_master, (UINT32)chans,
                                             (UINT32)g_xa2_out_channels, m, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr)) fprintf(stderr, "[XA2] speaker matrix rejected: 0x%08lX\n", hr);
    apply_reverb_send();
    {   /* the volume meter on the master */
        IUnknown *meter = NULL;
        XAUDIO2_EFFECT_DESCRIPTOR md;
        XAUDIO2_EFFECT_CHAIN mc;
        if (SUCCEEDED(XAudio2CreateVolumeMeter(&meter, 0)) && meter) {
            md.pEffect = meter;
            md.InitialState = TRUE;
            md.OutputChannels = (UINT32)g_xa2_out_channels;
            mc.EffectCount = 1;
            mc.pEffectDescriptors = &md;
            g_xa2_meter = SUCCEEDED(IXAudio2MasteringVoice_SetEffectChain(g_xa2_master, &mc));
            IUnknown_Release(meter);
        }
    }
    if (!g_xa2_have_listener) {
        /* DirectSound's own default until the game sets one: the I3DL2
         * "Generic" preset (DSOUND section 0x00401590 of the retail XBE). */
        static const int32_t room[2] = { -1000, -100 };
        static const float rest[10] = { 0.0f, 1.49f, 0.83f, 0, 0.007f, 0, 0.011f, 100.0f, 100.0f, 5000.0f };
        uint8_t generic[48];
        int32_t refl = -2602, rev = 200;
        memcpy(generic, room, 8);
        memcpy(generic + 8, rest, 40);
        memcpy(generic + 20, &refl, 4);
        memcpy(generic + 28, &rev, 4);
        memcpy(g_xa2_listener, generic, 48);
        g_xa2_have_listener = 1;
    }
    apply_listener();

    hr = IXAudio2SourceVoice_Start(g_xa2_source, 0, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] Start failed: 0x%08lX\n", hr);
        goto fail;
    }

    g_xa2_next_buf = 0;
    g_xa2_initialized = 1;
    g_xa2_frames_written = 0;

    fprintf(stderr, "[XA2] XAudio2 initialized (%d Hz %s 16-bit to %d output channels, reverb %s,"
                    " %d x %d-sample buffers)\n",
            XA2_SAMPLE_RATE, g_xa2_speakers == 6 ? "5.1" : "stereo", g_xa2_out_channels,
            g_xa2_send ? "on the I3DL2 send" : "none", XA2_NUM_BUFS, XA2_BUF_SAMPLES);
    return 1;

fail:
    xa2_shutdown();
    /* Failed initialization still runs on the COM-initializing thread. */
    if (com_initialized) CoUninitialize();
    return 0;
}

void xa2_shutdown(void)
{
    if (g_xa2_source) {
        IXAudio2SourceVoice_Stop(g_xa2_source, 0, XAUDIO2_COMMIT_NOW);
        IXAudio2SourceVoice_FlushSourceBuffers(g_xa2_source);
        g_xa2_source->lpVtbl->DestroyVoice(g_xa2_source);
        g_xa2_source = NULL;
    }
    if (g_xa2_reverb) {
        g_xa2_reverb->lpVtbl->DestroyVoice(g_xa2_reverb);
        g_xa2_reverb = NULL;
    }
    if (g_xa2_master) {
        g_xa2_master->lpVtbl->DestroyVoice(g_xa2_master);
        g_xa2_master = NULL;
    }
    if (g_xa2) {
        IXAudio2_Release(g_xa2);
        g_xa2 = NULL;
    }

    if (g_xa2_initialized)
        fprintf(stderr, "[XA2] Shut down (%d frames written)\n", g_xa2_frames_written);
    g_xa2_initialized = 0;
    g_xa2_send = 0;
}

int xa2_is_active(void)
{
    return g_xa2_initialized;
}

/* Submit a buffer of mixed samples to XAudio2.
 * Called from APU frame thread. Returns 1 if buffer was submitted. */
int xa2_submit_samples(const int16_t *samples, int num_samples)
{
    XAUDIO2_VOICE_STATE state;
    XAUDIO2_BUFFER xbuf;
    int idx;
    int copy_samples;
    int chans = g_xa2_speakers + g_xa2_send;
    HRESULT hr;

    if (!g_xa2_initialized || !g_xa2_source) return 0;

    IXAudio2SourceVoice_GetState(g_xa2_source, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    g_xa2_queued = (int)state.BuffersQueued;
    if ((int)state.BuffersQueued >= XA2_QUEUE_MAX) {
        g_xa2_dropped++;
        return 0;
    }
    if (state.BuffersQueued == 0) {
        /* Ran dry (or first frame): a cushion of silence ahead of the audio. */
        int k;
        if (g_xa2_frames_written)
            g_xa2_starved++;
        for (k = 0; k < XA2_QUEUE_PRIME; k++) {
            idx = g_xa2_next_buf;
            memset(g_xa2_bufs[idx], 0, sizeof g_xa2_bufs[idx]);
            memset(&xbuf, 0, sizeof(xbuf));
            xbuf.AudioBytes = (UINT32)(XA2_BUF_SAMPLES * chans * sizeof(int16_t));
            xbuf.pAudioData = (const BYTE *)g_xa2_bufs[idx];
            if (SUCCEEDED(IXAudio2SourceVoice_SubmitSourceBuffer(g_xa2_source, &xbuf, NULL)))
                g_xa2_next_buf = (idx + 1) % XA2_NUM_BUFS;
        }
    }

    idx = g_xa2_next_buf;
    copy_samples = (num_samples > XA2_BUF_SAMPLES) ? XA2_BUF_SAMPLES : num_samples;
    memcpy(g_xa2_bufs[idx], samples, copy_samples * chans * sizeof(int16_t));

    memset(&xbuf, 0, sizeof(xbuf));
    xbuf.AudioBytes = (UINT32)(copy_samples * chans * sizeof(int16_t));
    xbuf.pAudioData = (const BYTE *)g_xa2_bufs[idx];

    hr = IXAudio2SourceVoice_SubmitSourceBuffer(g_xa2_source, &xbuf, NULL);
    if (FAILED(hr)) return 0;

    g_xa2_next_buf = (idx + 1) % XA2_NUM_BUFS;
    g_xa2_frames_written++;
    if (g_xa2_meter) {
        float peaks[XA2_MAX_CHANNELS] = { 0 };
        XAUDIO2FX_VOLUMEMETER_LEVELS lv;
        int c;
        lv.pPeakLevels = peaks;
        lv.pRMSLevels = NULL;
        lv.ChannelCount = (UINT32)g_xa2_out_channels;
        if (SUCCEEDED(IXAudio2MasteringVoice_GetEffectParameters(g_xa2_master, 0, &lv, sizeof lv)))
            for (c = 0; c < g_xa2_out_channels; c++)
                if (peaks[c] > g_xa2_peak) g_xa2_peak = peaks[c];
    }
    return 1;
}

float xa2_take_output_peak(void)
{
    float p = g_xa2_meter ? g_xa2_peak : -1.0f;
    g_xa2_peak = 0.0f;
    return p;
}

int xa2_get_buffer_size(void)
{
    return XA2_BUF_SAMPLES;
}

void xa2_get_stats(int *queued, int *dropped, int *starved)
{
    *queued = g_xa2_queued;
    *dropped = g_xa2_dropped;
    *starved = g_xa2_starved;
    if (g_xa2) {
        /* POPSoT: the engine's own glitches (a processing pass that was late
         * and played silence), which the queue counts above cannot see. */
        static UINT32 last_glitches;
        XAUDIO2_PERFORMANCE_DATA pd;
        IXAudio2_GetPerformanceData(g_xa2, &pd);
        fprintf(stderr, "[XA2] engine glitches %u (+%u), latency %u samples, CPU %llu of %llu cycles, %u active voices\n",
                pd.GlitchesSinceEngineStarted, pd.GlitchesSinceEngineStarted - last_glitches,
                pd.CurrentLatencyInSamples, (unsigned long long)pd.AudioCyclesSinceLastQuery,
                (unsigned long long)pd.TotalCyclesSinceLastQuery, pd.ActiveSourceVoiceCount);
        last_glitches = pd.GlitchesSinceEngineStarted;
    }
}

#else /* !_WIN32 -- POSIX stubs (no audio output yet) */

int  xa2_init(void)                                   { return 0; }
void xa2_shutdown(void)                               {}
int  xa2_is_active(void)                              { return 0; }
int  xa2_submit_samples(const int16_t *s, int n)      { (void)s; (void)n; return 0; }
int  xa2_get_buffer_size(void)                        { return 0; }
void xa2_get_stats(int *q, int *d, int *s)            { *q = *d = *s = 0; }
void xa2_configure(int speakers, int reverb)          { (void)speakers; (void)reverb; }
void xa2_layout(int *speakers, int *reverb_send)      { *speakers = 2; *reverb_send = 0; }
void xa2_set_reverb(int on)                           { (void)on; }
void xa2_set_i3dl2_listener(const void *l)            { (void)l; }
int  xa2_output_channels(void)                        { return 0; }
float xa2_take_output_peak(void)                      { return -1.0f; }

#endif /* _WIN32 */
