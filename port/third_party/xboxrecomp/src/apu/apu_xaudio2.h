/**
 * XAudio2 Audio Output Backend
 */
#ifndef APU_XAUDIO2_H
#define APU_XAUDIO2_H

#include <stdint.h>

/* Initialize XAudio2. Returns 1 on success, 0 on failure. */
int xa2_init(void);

/* Shut down XAudio2 and release all resources. */
void xa2_shutdown(void);

/* Returns 1 if XAudio2 is active. */
int xa2_is_active(void);

/* Submit interleaved 16-bit samples in the layout of xa2_layout. Returns
 * 1 if accepted. */
int xa2_submit_samples(const int16_t *samples, int num_samples);

/* RE-SoT: speaker layout and host reverb. xa2_configure before xa2_init:
 * 2 (stereo) or 6 (5.1, FL FR FC LFE BL BR) speaker channels, and whether
 * the I3DL2 reverb submix is built. xa2_layout: the speaker channels and
 * whether one more channel, the reverb send, follows them. */
void xa2_configure(int speakers, int reverb);
void xa2_layout(int *speakers, int *reverb_send);
/* The reverb's send level on (1) or off (0), at once. */
void xa2_set_reverb(int on);
/* The game's listener environment, a DSI3DL2LISTENER (48 bytes, as the
 * guest holds it: lRoom, lRoomHF, flRoomRolloffFactor, flDecayTime,
 * flDecayHFRatio, lReflections, flReflectionsDelay, lReverb,
 * flReverbDelay, flDiffusion, flDensity, flHFReference). */
void xa2_set_i3dl2_listener(const void *listener48);
/* The output's channel count (the device's, in 5.1 mode), 0 before init. */
int xa2_output_channels(void);
/* The loudest sample the master voice played since the last call (1.0 =
 * full scale), -1 without the meter. */
float xa2_take_output_peak(void);

/* Get the preferred buffer size in samples. */
int xa2_get_buffer_size(void);

/* RE-SoT: buffers queued at the last submit, frames dropped because the queue
 * was full, and times it ran dry. */
void xa2_get_stats(int *queued, int *dropped, int *starved);

#endif /* APU_XAUDIO2_H */
