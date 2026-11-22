#include "json.h"
#include <string.h>
#include <stdlib.h>

static const char *ws(const char *p, const char *e) { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; return p; }

static const char *skip_str(const char *p, const char *e) {      /* p at the opening quote */
    for (p++; p < e; p++) { if (*p == '\\') p++; else if (*p == '"') return p + 1; }
    return e;
}
static const char *skip_val(const char *p, const char *e) {
    p = ws(p, e);
    if (p >= e) return e;
    if (*p == '"') return skip_str(p, e);
    if (*p == '{' || *p == '[') {
        int d = 0;
        for (; p < e; p++) {
            if (*p == '"') { p = skip_str(p, e) - 1; continue; }
            if (*p == '{' || *p == '[') d++;
            else if (*p == '}' || *p == ']') { if (--d == 0) return p + 1; }
        }
        return e;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r') p++;
    return p;
}

const char *json_key(const char *obj, size_t len, const char *key, size_t *vlen) {
    const char *p = obj, *e = obj + len;
    size_t kl = strlen(key);
    p = ws(p, e);
    if (p >= e || *p != '{') return NULL;
    p++;
    for (;;) {
        p = ws(p, e);
        if (p < e && *p == ',') p = ws(p + 1, e);
        if (p >= e || *p != '"') return NULL;
        const char *ks = p + 1, *ke = skip_str(p, e);
        size_t n = (size_t)(ke - ks) - 1;
        p = ws(ke, e);
        if (p >= e || *p != ':') return NULL;
        p = ws(p + 1, e);
        const char *vs = p, *ve = skip_val(p, e);
        if (n == kl && !memcmp(ks, key, kl)) { if (vlen) *vlen = (size_t)(ve - vs); return vs; }
        p = ve;
    }
}

static int put_utf8(char *o, unsigned c) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); return 2; }
    o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | ((c >> 6) & 63)); o[2] = (char)(0x80 | (c & 63)); return 3;
}

int json_str(const char *obj, size_t len, const char *key, char *out, size_t n) {
    size_t vl; const char *v = json_key(obj, len, key, &vl);
    if (!n) return -1;
    out[0] = 0;
    if (!v || vl < 2 || *v != '"') return -1;
    const char *p = v + 1, *e = v + vl - 1;
    size_t o = 0;
    while (p < e && o + 4 < n) {
        if (*p != '\\') { out[o++] = *p++; continue; }
        p++;
        if (p >= e) break;
        switch (*p) {
        case 'n': case 't': case 'r': out[o++] = ' '; p++; break;
        case 'u': {
            char h[5] = {0}; unsigned c;
            if (e - p < 5) { p = e; break; }
            memcpy(h, p + 1, 4); c = (unsigned)strtoul(h, NULL, 16); p += 5;
            if (c >= 0xD800 && c < 0xE000) c = '?';            /* surrogates: not worth pairing */
            o += (size_t)put_utf8(out + o, c);
            break; }
        default: out[o++] = *p++; break;                        /* \" \\ \/ */
        }
    }
    out[o] = 0;
    return 0;
}

long json_int(const char *obj, size_t len, const char *key, long dflt) {
    size_t vl; const char *v = json_key(obj, len, key, &vl);
    if (!v || !vl) return dflt;
    if (*v == '"') v++;
    if ((*v < '0' || *v > '9') && *v != '-') return dflt;
    return strtol(v, NULL, 10);
}

const char *json_arr_next(const char *p, const char *end, const char **elem, size_t *elen) {
    p = ws(p, end);
    if (p < end && *p == ',') p = ws(p + 1, end);
    if (p >= end || *p == ']') return NULL;
    const char *q = skip_val(p, end);
    *elem = p; *elen = (size_t)(q - p);
    return q;
}
