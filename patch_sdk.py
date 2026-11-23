#!/usr/bin/env python3
"""Prepare a wup-homebrew checkout for WUP Tube Music (idempotent):
    python3 patch_sdk.py /path/to/wup-homebrew
 - copies this folder to libwup/examples/wup-tube
 - adds the Helix AAC decoder to libwup/tools/deps.py (fetched at the first build)
 - lets libwup's HTTPS client follow URLs up to 2 KB (stream addresses are long)"""
import os, shutil, sys
root = sys.argv[1] if len(sys.argv) > 1 else '.'
lib = os.path.join(root, 'libwup')
here = os.path.dirname(os.path.abspath(__file__))
dst = os.path.join(lib, 'examples', 'wup-tube')
if os.path.abspath(dst) != here:
    shutil.copytree(here, dst, dirs_exist_ok=True, ignore=shutil.ignore_patterns('patch_sdk.py', 'README.md'))

p = os.path.join(lib, 'tools', 'deps.py'); s = open(p).read()
if 'helix-aac' not in s:
    entry = ("    # AAC decoding for WUP Tube Music (Helix, RealNetworks: its own licence, see the repository)\n"
             "    'helix-aac': (os.path.join(paths.THIRD, 'helix-aac'), 'https://github.com/pschatzmann/arduino-libhelix.git', 'main', 'RPSL/RCSL', ['src/libhelix-aac', 'src/utils']),\n")
    s = s.replace("    'compiler-rt':", entry + "    'compiler-rt':", 1)
    open(p, 'w').write(s)

p = os.path.join(lib, 'src', 'https.c'); s = open(p).read()
# Keep the redirect URL at 2 KB, but move the HTTPS client's large scratch
# allocations out of the small, fragmentation-prone app heap. The client is
# already global-state and WUPTube serializes all HTTPS requests.
s = s.replace("""    char *where = malloc(1024), *head = malloc(HEAD_MAX);
    unsigned char *iobuf = malloc(BR_SSL_BUFSIZE_MONO);       /* (half duplex: a request, then its response) */
    if (!where || !head || !iobuf) { free(where); free(head); free(iobuf); return WUP_HTTPS_ENOMEM; }
    snprintf(where, 1024, "%s", url);""", """    static char where[2048], head[HEAD_MAX];
    static unsigned char iobuf[BR_SSL_BUFSIZE_MONO];          /* one request at a time; avoid heap allocation */
    snprintf(where, sizeof where, "%s", url);""")
s = s.replace("""            char next[1024];
            if (loc->value_len >= 8 && !strncmp(loc->value, "https://", 8)) snprintf(next, sizeof next, "%.*s", (int)loc->value_len, loc->value);
            else snprintf(next, sizeof next, "https://%s:%d%.*s", host, port, (int)loc->value_len, loc->value);
            snprintf(where, 1024, "%s", next);""", """            char next[2048];
            if (loc->value_len >= 8 && !strncmp(loc->value, "https://", 8)) snprintf(next, sizeof next, "%.*s", (int)loc->value_len, loc->value);
            else snprintf(next, sizeof next, "https://%s:%d%.*s", host, port, (int)loc->value_len, loc->value);
            snprintf(where, sizeof where, "%s", next);""")
s = s.replace("""    free(where); free(head); free(iobuf);
    return status;""", """    return status;""")
if 'static unsigned char iobuf[BR_SSL_BUFSIZE_MONO]' not in s:
    raise SystemExit('Could not apply static HTTPS buffers: expected SDK https.c allocation block not found')
open(p, 'w').write(s)

# The large-app loader runs before main(). Do not allocate the compressed
# WUP_BIGMEM segment on the small post-BSS heap: stage it in the framebuffer
# memory, which is unused until the app draws its first screen.
p = os.path.join(lib, 'src', 'big.c'); s = open(p).read()
old = '''    uint8_t *z = malloc(h.big_size);
    if (!z) return -2;
    int r = fs_read(wup_app_path(), h.big_off, z, h.big_size) == (int)h.big_size && crc32(z, h.big_size) == h.big_crc ? 0 : -3;
    unsigned n = want;
    if (!r && (tinf_uncompress(__big_start, &n, z, h.big_size) != TINF_OK || n != want)) r = -4;
    free(z);
'''
new = '''    /* The RGB565 framebuffer is free until main() draws its first frame.
       Use it as scratch space so loading large code does not depend on heap
       space left after .bss has been placed in WUP_BIGMEM. */
    if (h.big_size > 0x000D9000u) return -2; /* WUP_HANDOFF_STAGE capacity */
    uint8_t *z = (uint8_t *)WUP_HANDOFF_STAGE;
    int r = fs_read(wup_app_path(), h.big_off, z, h.big_size) == (int)h.big_size && crc32(z, h.big_size) == h.big_crc ? 0 : -3;
    unsigned n = want;
    if (!r && (tinf_uncompress(__big_start, &n, z, h.big_size) != TINF_OK || n != want)) r = -4;
'''
if old in s:
    s = s.replace(old, new)
elif 'WUP_HANDOFF_STAGE capacity' not in s:
    raise SystemExit('Could not patch libwup/src/big.c: expected loader code was not found')
open(p, 'w').write(s)
print('done: cd %s && python3 tools/wupbuild.py examples/wup-tube' % lib)
