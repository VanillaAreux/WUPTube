/* mp4: what a fragmented MP4 audio stream (YouTube's m4a, itag 140) needs: the sample sizes of a fragment and the
 * audio parameters. Pure functions on buffers, no SDK. Boxes are passed without their 8-byte header. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define MP4_MAX_SAMPLES 512
typedef struct {
    uint32_t sizes[MP4_MAX_SAMPLES];   /* each sample's size, in order */
    int n;
    int32_t data_offset;               /* trun's data offset: from the start of the moof box */
    uint32_t default_size;             /* tfhd's, used when trun has no sizes */
} mp4_frag;

int mp4_parse_moof(const uint8_t *b, size_t len, mp4_frag *m);                 /* 0, or < 0 */
int mp4_audio_params(const uint8_t *moov, size_t len, int *chans, int *rate);  /* from the mp4a entry: 0, or < 0 */
