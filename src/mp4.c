#include "mp4.h"
#include <string.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }

static void parse_tfhd(const uint8_t *b, size_t len, mp4_frag *m) {
    if (len < 8) return;
    uint32_t fl = be32(b) & 0xffffff; size_t o = 8;                 /* version+flags, track id */
    if (fl & 0x01) o += 8;                                          /* base data offset */
    if (fl & 0x02) o += 4;                                          /* sample description index */
    if (fl & 0x08) o += 4;                                          /* default duration */
    if ((fl & 0x10) && o + 4 <= len) m->default_size = be32(b + o);
}

static void parse_trun(const uint8_t *b, size_t len, mp4_frag *m) {
    if (len < 8) return;
    uint32_t fl = be32(b) & 0xffffff, cnt = be32(b + 4); size_t o = 8;
    if (fl & 0x01) { if (o + 4 > len) return; m->data_offset = (int32_t)be32(b + o); o += 4; }
    if (fl & 0x04) o += 4;                                          /* first sample flags */
    for (uint32_t i = 0; i < cnt && o <= len; i++) {
        uint32_t sz = m->default_size;
        if (fl & 0x100) o += 4;                                     /* duration */
        if (fl & 0x200) { if (o + 4 > len) return; sz = be32(b + o); o += 4; }
        if (fl & 0x400) o += 4;                                     /* flags */
        if (fl & 0x800) o += 4;                                     /* composition offset */
        if (m->n < MP4_MAX_SAMPLES) m->sizes[m->n++] = sz;
    }
}

static void walk(const uint8_t *b, size_t len, mp4_frag *m) {
    for (size_t o = 0; o + 8 <= len;) {
        uint32_t sz = be32(b + o); const uint8_t *t = b + o + 4;
        if (sz < 8 || o + sz > len) break;
        if (!memcmp(t, "traf", 4)) walk(b + o + 8, sz - 8, m);
        else if (!memcmp(t, "tfhd", 4)) parse_tfhd(b + o + 8, sz - 8, m);
        else if (!memcmp(t, "trun", 4)) parse_trun(b + o + 8, sz - 8, m);
        o += sz;
    }
}

int mp4_parse_moof(const uint8_t *b, size_t len, mp4_frag *m) {
    memset(m, 0, sizeof *m);
    walk(b, len, m);
    return m->n > 0 ? 0 : -1;
}

int mp4_audio_params(const uint8_t *moov, size_t len, int *chans, int *rate) {
    for (size_t i = 0; i + 4 + 28 <= len; i++) {
        if (memcmp(moov + i, "mp4a", 4)) continue;
        const uint8_t *e = moov + i + 4;       /* reserved[6] index[2] | version[2] revision[2] vendor[4] | channels[2] ... rate 16.16 at 24 */
        *chans = (int)be16(e + 16); *rate = (int)be16(e + 24);
        return *chans > 0 && *rate > 0 ? 0 : -1;
    }
    return -1;
}
