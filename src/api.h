/* api: talks to a Piped server (https://github.com/TeamPiped/Piped) over libwup's HTTPS GET: search and audio streams.
 * One HTTPS request at a time in the whole app (libwup's client is a single context): net_take()/net_give(). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "wup_https.h"

typedef struct { char id[16]; char title[96]; char artist[64]; int dur; } track_t;

extern char api_base[128];                 /* "https://pipedapi.kavin.rocks" */
extern void (*api_idle)(void);             /* the UI's keep-moving call, used for requests the UI waits on */
extern void (*api_before)(void);           /* called before a UI request (the player gives up a whole-file stream) */

void api_load_cfg(void);
void api_save_cfg(void);
const char *api_err(int code);             /* words for a failure */

int api_search(const char *q, track_t *out, int max);                         /* count, or < 0 */
/* the best AAC-LC (m4a) audio stream of a video: its URL and its size in bytes (0 if unknown); 0, or < 0 */
int api_stream(const char *id, char *url, size_t n, long long *size);

/* a ranged GET for the player; headers like "Range: bytes=0-65535\r\n" */
int api_get_range(const char *url, const char *hdrs, wup_https_body_fn cb, void *arg);
