#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wup.h"
#include "api.h"
#include "json.h"

char api_base[128] = "https://pipedapi.kavin.rocks";
void (*api_idle)(void);
void (*api_before)(void);
static int net_sem;
/* Reused for the one-at-a-time API request; keep large JSON storage off the fragmented heap. */
static char api_json[20 * 1024];
#define CFG "tube.cfg"

void api_load_cfg(void) {
    FILE *f = fopen(CFG, "r");
    if (!f) return;
    char b[128];
    if (fgets(b, sizeof b, f)) {
        b[strcspn(b, "\r\n")] = 0;
        if (!strncmp(b, "https://", 8) && strlen(b) > 10) { strcpy(api_base, b); }
    }
    fclose(f);
}
void api_save_cfg(void) {
    FILE *f = fopen(CFG, "w");
    if (!f) return;
    fprintf(f, "%s\n", api_base);
    fclose(f);
}

const char *api_err(int c) {
    switch (c) {
    case WUP_HTTPS_ENOTIME: return "The clock is not set: join Wi-Fi in the menu first";
    case WUP_HTTPS_EDNS: return "Could not find the server";
    case WUP_HTTPS_ECONNECT: return "Could not connect";
    case WUP_HTTPS_ETLS: return "Secure connection failed";
    case WUP_HTTPS_ETIMEOUT: return "The server took too long";
    case WUP_HTTPS_ENOMEM: return "Out of memory";
    case -100: return "Another request is still running";
    case -101: return "The server's answer was not understood";
    case -102: return "Nothing playable found";
    case 429: return "The server is busy (429)";
    case 500: case 502: case 503: return "The server has a problem";
    default: return c > 0 ? "The server refused the request" : "Network error";
    }
}

/* ---- the one-request-at-a-time lock */
static int net_take(int ui) {
    if (!net_sem) net_sem = wup_sem_new(1, 1);
    for (int tries = 0; wup_sem_wait(net_sem, 50) < 0; tries++) {
        if (ui && api_idle) api_idle();
        if (tries > 600) return -100;                      /* ~30 s */
    }
    return 0;
}
static void net_give(void) { wup_sem_signal(net_sem); }

int api_get_range(const char *url, const char *hdrs, wup_https_body_fn cb, void *arg) {
    if (net_take(0)) return -100;
    wup_https_opts o = { .headers = hdrs, .timeout_ms = 15000 };
    int r = wup_https_get(url, &o, cb, arg);
    net_give();
    return r;
}

/* ---- capture one top-level array's elements from a streamed JSON body ("items":[ ... ]) */
typedef struct {
    const char *needle; int m;                 /* finding  "key":[  */
    char *buf; size_t cap, len, last;          /* the captured array; last: the end of its last whole element */
    int on, depth, instr, esc, done;
} cap_t;

static int cap_body(void *arg, const uint8_t *d, size_t n, long long total) {
    cap_t *c = arg; (void)total;
    size_t nl = strlen(c->needle);
    for (size_t i = 0; i < n && !c->done; i++) {
        char ch = (char)d[i];
        if (!c->on) {
            if (ch == c->needle[c->m]) { if (++c->m == (int)nl) { c->on = 1; c->buf[0] = '['; c->len = 1; c->last = 1; c->depth = 1; } }
            else c->m = ch == c->needle[0] ? 1 : 0;
            continue;
        }
        if (c->len + 2 >= c->cap) {                      /* full: end at the last whole element */
            c->len = c->last; c->buf[c->len++] = ']'; c->done = 1; break;
        }
        c->buf[c->len++] = ch;
        if (c->instr) { if (c->esc) c->esc = 0; else if (ch == '\\') c->esc = 1; else if (ch == '"') c->instr = 0; continue; }
        if (ch == '"') c->instr = 1;
        else if (ch == '{' || ch == '[') c->depth++;
        else if (ch == '}' || ch == ']') {
            c->depth--;
            if (c->depth == 1) c->last = c->len;
            if (c->depth == 0) { c->done = 1; }
        }
    }
    return c->done;                                      /* 1 stops the download: we have what we want */
}

static int fetch_array(const char *url, const char *needle, char *buf, size_t cap, size_t *len) {
    cap_t c = { .needle = needle, .buf = buf, .cap = cap };
    if (api_before) api_before();
    if (net_take(1)) return -100;
    wup_https_opts o = { .timeout_ms = 20000, .idle = api_idle };
    int r = wup_https_get(url, &o, cap_body, &c);
    net_give();
    if (!c.on) return r == 200 || r == WUP_HTTPS_ESTOPPED ? -101 : (r > 0 ? r : r);
    if (!c.done) { c.len = c.last; if (c.len < 1) c.len = 1; buf[c.len++] = ']'; }
    *len = c.len;
    return 0;
}

static void urlenc(const char *s, char *o, size_t n) {
    static const char hex[] = "0123456789ABCDEF";
    size_t k = 0;
    for (; *s && k + 4 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') o[k++] = (char)c;
        else if (c == ' ') o[k++] = '+';
        else { o[k++] = '%'; o[k++] = hex[c >> 4]; o[k++] = hex[c & 15]; }
    }
    o[k] = 0;
}

int api_search(const char *q, track_t *out, int max) {
    char enc[160], url[400];
    urlenc(q, enc, sizeof enc);
    snprintf(url, sizeof url, "%s/search?q=%s&filter=music_songs", api_base, enc);
    size_t cap = sizeof api_json, len = 0;
    char *buf = api_json;
    int r = fetch_array(url, "\"items\":[", buf, cap, &len);
    if (r) return r;
    int n = 0; const char *p = buf + 1, *end = buf + len, *el; size_t el_n;
    while (n < max && (p = json_arr_next(p, end, &el, &el_n))) {
        track_t *t = &out[n];
        char u[128]; const char *v;
        if (json_str(el, el_n, "url", u, sizeof u) || !(v = strstr(u, "v="))) continue;
        snprintf(t->id, sizeof t->id, "%s", v + 2);
        t->id[strcspn(t->id, "&")] = 0;
        if (json_str(el, el_n, "title", t->title, sizeof t->title)) continue;
        if (json_str(el, el_n, "uploaderName", t->artist, sizeof t->artist)) t->artist[0] = 0;
        t->dur = (int)json_int(el, el_n, "duration", 0);
        n++;
    }
    return n;
}

int api_stream(const char *id, char *url, size_t n, long long *size) {
    char u[400];
    snprintf(u, sizeof u, "%s/streams/%s", api_base, id);
    size_t cap = sizeof api_json, len = 0;
    char *buf = api_json;
    int r = fetch_array(u, "\"audioStreams\":[", buf, cap, &len);
    if (r) return r;
    const char *p = buf + 1, *end = buf + len, *el; size_t el_n;
    long best = -1; int found = 0;
    while ((p = json_arr_next(p, end, &el, &el_n))) {
        char mime[96], codec[32];
        if (json_str(el, el_n, "mimeType", mime, sizeof mime) || !strstr(mime, "audio/mp4")) continue;
        if (!json_str(el, el_n, "codec", codec, sizeof codec) && strncmp(codec, "mp4a.40.2", 9)) continue;   /* LC only: HE-AAC needs SBR */
        long br = json_int(el, el_n, "bitrate", 128000);
        long score = br > 160000 ? 1000000 - br : br;                      /* the highest up to 160 kbps */
        if (score > best) {
            char tmp[1500];
            if (json_str(el, el_n, "url", tmp, sizeof tmp) || strncmp(tmp, "https://", 8) || strlen(tmp) >= n) continue;
            strcpy(url, tmp); best = score; found = 1;
            *size = json_int(el, el_n, "contentLength", 0);
        }
    }
    return found ? 0 : -102;
}
