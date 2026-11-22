#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wup.h"
#include "api.h"
#include "mp4.h"
#include "player.h"
#include "aacdec.h"                                     /* Helix AAC decoder */

#define RING     (160 * 1024)
#define CHUNK    (64 * 1024)
#define FRAMEMAX 2048
#define BOX_MAX  (24 * 1024)

static uint8_t *ring;
static volatile size_t wr, rd;                          /* counters, not wrapped */
static volatile int gen;                                /* a thread of an older generation ends at once */
static volatile int state = PL_IDLE, paused, net_done, whole_file;
static volatile unsigned played;                        /* frames queued at 48 kHz */
static const char *err_msg = "";
static char the_url[1600];
static long long the_size;
static int th_net = -1, th_dec = -1;

static size_t avail(void) { return wr - rd; }
static size_t room(void) { return RING - (wr - rd); }

/* ---- net thread */
static struct { int g; long long total_seen; int first; size_t pushed; } nq;

static int net_body(void *arg, const uint8_t *d, size_t n, long long total) {
    int g = (int)(intptr_t)arg;
    if (g != gen) return 1;
    if (nq.first) { nq.first = 0; if (total > CHUNK) whole_file = 1; }    /* the server ignored Range */
    while (room() < n) { if (g != gen) return 1; wup_thread_sleep(20); }
    for (size_t i = 0; i < n;) {
        size_t o = wr % RING, k = n - i < RING - o ? n - i : RING - o;
        memcpy(ring + o, d + i, k); i += k; wr += k;
    }
    nq.pushed += n;
    return 0;
}

static void net_thread(void *arg) {
    int g = (int)(intptr_t)arg, fails = 0;
    long long pos = 0;
    while (g == gen && !net_done) {
        if (room() < CHUNK && !whole_file) { wup_thread_sleep(60); continue; }
        char h[64];
        snprintf(h, sizeof h, "Range: bytes=%lld-%lld\r\n", pos, pos + CHUNK - 1);
        nq.pushed = 0; nq.first = (pos == 0);
        int r = api_get_range(the_url, h, net_body, (void *)(intptr_t)g);
        if (g != gen) return;
        pos += (long long)nq.pushed;
        if (whole_file) { if (r == 200 || r == WUP_HTTPS_ESTOPPED) net_done = 1; else if (r < 0 || r >= 400) { err_msg = "The connection dropped"; state = PL_ERROR; net_done = 1; } continue; }
        if (r == 416) { net_done = 1; break; }
        if (r == 206 || r == 200) {
            fails = 0;
            if (nq.pushed < CHUNK || (the_size && pos >= the_size)) net_done = 1;
            continue;
        }
        if (++fails > 4) { err_msg = api_err(r); state = PL_ERROR; net_done = 1; break; }
        wup_thread_sleep(700 * fails);
    }
}

/* ---- ring reads for the decoder: blocking, 0 when the stream or the song has ended */
static int rd_bytes(void *dst, size_t n, int g) {
    uint8_t *d = dst;
    while (n) {
        size_t a = avail();
        if (!a) {
            if (g != gen) return 0;
            if (net_done) return 0;
            if (state == PL_PLAYING) state = PL_BUFFERING;
            wup_thread_sleep(10);
            continue;
        }
        size_t k = a < n ? a : n;
        size_t o = rd % RING, k1 = k < RING - o ? k : RING - o;
        if (d) { memcpy(d, ring + o, k1); if (k > k1) memcpy(d + k1, ring, k - k1); d += k; }
        rd += k; n -= k;
    }
    return 1;
}
static int skip_bytes(unsigned long long n, int g) {
    while (n) { size_t k = n > 4096 ? 4096 : (size_t)n; if (!rd_bytes(NULL, k, g)) return 0; n -= k; }
    return 1;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* ---- output: resample to 48 kHz and queue */
static struct { int rate, chans; int32_t t; int16_t pl, pr; } rs;
static int16_t pcm[2048 * 2], out[1400 * 2];

static int queue_all(const int16_t *s, int frames, int g) {
    while (frames > 0) {
        while (paused && g == gen) wup_thread_sleep(30);
        if (g != gen) return 0;
        int k = wup_audio_queue(s, frames);
        if (k <= 0) { wup_thread_sleep(8); continue; }
        s += k * 2; frames -= k; played += (unsigned)k;
        if (state == PL_BUFFERING) state = PL_PLAYING;
    }
    return 1;
}

static int emit(const int16_t *x, int n, int g) {       /* n frames, interleaved (mono doubled) */
    int32_t step = (int32_t)(((int64_t)rs.rate << 16) / 48000);
    int o = 0, st = rs.chans == 2;
    while ((rs.t >> 16) < n - 1) {
        int i = rs.t >> 16, f = rs.t & 0xffff;
        int al = i < 0 ? rs.pl : x[st ? i * 2 : i], ar = i < 0 ? rs.pr : x[st ? i * 2 + 1 : i];
        int bl = x[st ? (i + 1) * 2 : i + 1], br = x[st ? (i + 1) * 2 + 1 : i + 1];
        out[o * 2] = (int16_t)(al + (((bl - al) * f) >> 16));
        out[o * 2 + 1] = (int16_t)(ar + (((br - ar) * f) >> 16));
        o++; rs.t += step;
        if (o == 1400) { if (!queue_all(out, o, g)) return 0; o = 0; }
    }
    rs.t -= n << 16;
    rs.pl = x[st ? (n - 1) * 2 : n - 1]; rs.pr = x[st ? (n - 1) * 2 + 1 : n - 1];
    return o ? queue_all(out, o, g) : 1;
}

/* ---- decoder thread */
static void dec_thread(void *arg) {
    int g = (int)(intptr_t)arg;
    uint8_t *box = malloc(BOX_MAX), *frame = malloc(FRAMEMAX);
    mp4_frag *mf = malloc(sizeof *mf);
    HAACDecoder dec = AACInitDecoder();
    int ok = box && frame && mf && dec && wup_audio_open() == 0;
    int have_params = 0;
    memset(&rs, 0, sizeof rs);
    if (!ok) { err_msg = "Out of memory"; state = PL_ERROR; goto done; }
    for (;;) {
        uint8_t h[16];
        if (!rd_bytes(h, 8, g)) break;
        unsigned long long size = be32(h); int hl = 8;
        if (size == 1) { if (!rd_bytes(h + 8, 8, g)) break; size = (unsigned long long)be32(h + 8) << 32 | be32(h + 12); hl = 16; }
        unsigned long long payload = size >= (unsigned long long)hl ? size - hl : 0;
        if (size == 0) payload = ~0ull >> 1;                          /* to the end of the file */
        if (!memcmp(h + 4, "moov", 4) && payload <= BOX_MAX) {
            if (!rd_bytes(box, (size_t)payload, g)) break;
            int ch, rate;
            if (mp4_audio_params(box, (size_t)payload, &ch, &rate) || ch > 2) { err_msg = "Not a stereo or mono AAC stream"; state = PL_ERROR; break; }
            AACFrameInfo fi; memset(&fi, 0, sizeof fi);
            fi.nChans = ch; fi.sampRateCore = rate; fi.profile = AAC_PROFILE_LC;
            if (AACSetRawBlockParams(dec, 0, &fi)) { err_msg = "The decoder refused the stream"; state = PL_ERROR; break; }
            rs.rate = rate; rs.chans = ch; have_params = 1;
        } else if (!memcmp(h + 4, "moof", 4) && payload <= BOX_MAX) {
            if (!rd_bytes(box, (size_t)payload, g)) break;
            if (mp4_parse_moof(box, (size_t)payload, mf)) continue;
            /* the samples follow in the mdat right after this moof */
            uint8_t mh[16];
            if (!rd_bytes(mh, 8, g)) break;
            unsigned long long msize = be32(mh); int mhl = 8;
            if (msize == 1) { if (!rd_bytes(mh + 8, 8, g)) break; msize = (unsigned long long)be32(mh + 8) << 32 | be32(mh + 12); mhl = 16; }
            if (memcmp(mh + 4, "mdat", 4) || !have_params) { err_msg = "Unexpected stream layout"; state = PL_ERROR; break; }
            unsigned long long left = msize - mhl;
            long lead = (long)mf->data_offset - (long)(size + mhl);   /* bytes between the mdat's start and the first sample */
            if (lead > 0 && (unsigned long long)lead <= left) { if (!skip_bytes((unsigned long long)lead, g)) break; left -= (unsigned long long)lead; }
            for (int i = 0; i < mf->n && left >= mf->sizes[i]; i++) {
                uint32_t sz = mf->sizes[i];
                left -= sz;
                if (sz > FRAMEMAX) { if (!skip_bytes(sz, g)) goto done; continue; }
                if (!rd_bytes(frame, sz, g)) goto done;
                unsigned char *in = frame; int bl = (int)sz;
                if (AACDecode(dec, &in, &bl, pcm) == 0) {
                    AACFrameInfo fi; AACGetLastFrameInfo(dec, &fi);
                    int n = fi.nChans ? fi.outputSamps / fi.nChans : 0;
                    if (n > 0 && !emit(pcm, n, g)) goto done;
                }
            }
            if (!skip_bytes(left, g)) break;
        } else {
            if (!skip_bytes(payload, g)) break;
        }
    }
    if (g == gen && state != PL_ERROR) {
        while (wup_audio_queued() > 0 && g == gen) wup_thread_sleep(30);
        if (g == gen) state = PL_ENDED;
    }
done:
    if (dec) AACFreeDecoder(dec);
    free(box); free(frame); free(mf);
    if (g == gen && state != PL_ENDED) { /* error or stop: leave the state as set */ }
    wup_audio_close();
}

/* ---- control */
void player_stop(void) {
    gen++;
    if (th_dec >= 0) { wup_thread_join(th_dec, 1500); th_dec = -1; }
    if (th_net >= 0) { wup_thread_detach(th_net); th_net = -1; }   /* it ends itself at its next callback or wait */
    state = PL_IDLE; paused = 0;
}

void player_play(const char *url, long long size) {
    player_stop();
    if (!ring) ring = malloc(RING);
    if (!ring) { err_msg = "Out of memory"; state = PL_ERROR; return; }
    snprintf(the_url, sizeof the_url, "%s", url);
    the_size = size; wr = rd = 0; net_done = 0; whole_file = 0; played = 0; err_msg = "";
    state = PL_BUFFERING;
    int g = gen;
    th_dec = wup_thread_start(dec_thread, (void *)(intptr_t)g, 32 * 1024, 0);
    th_net = wup_thread_start(net_thread, (void *)(intptr_t)g, 32 * 1024, 0);
    if (th_dec < 0 || th_net < 0) { gen++; err_msg = "Could not start the player"; state = PL_ERROR; }
}

void player_pause(int on) { if (state == PL_PLAYING || state == PL_PAUSED || state == PL_BUFFERING) { paused = on; if (state != PL_BUFFERING) state = on ? PL_PAUSED : PL_PLAYING; } }
int player_state(void) { return state; }
int player_seconds(void) { return (int)(played / 48000); }
int player_whole_file(void) { return whole_file && !net_done; }
const char *player_error(void) { return err_msg; }
