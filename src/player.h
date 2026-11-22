/* player: streams an m4a (AAC-LC, fragmented MP4) from a URL and plays it on the pad.
 *   net thread      ranged GETs of 64 KB into a ring (one TLS handshake per chunk), or one whole-file GET if the server
 *                   ignores Range
 *   decoder thread  reads the ring: MP4 boxes -> AAC frames (Helix) -> 48 kHz stereo (linear resampler) -> wup_audio_queue */
#pragma once

enum { PL_IDLE, PL_BUFFERING, PL_PLAYING, PL_PAUSED, PL_ENDED, PL_ERROR };

void player_play(const char *url, long long size);      /* stops what plays and starts this */
void player_stop(void);
void player_pause(int on);
int player_state(void);
int player_seconds(void);                               /* played so far */
int player_whole_file(void);                            /* 1: the server sends the whole file in one request (holds the network) */
const char *player_error(void);
