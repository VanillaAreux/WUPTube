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
for a, b in [('malloc(1024), *head', 'malloc(2048), *head'), ('snprintf(where, 1024, "%s", url)', 'snprintf(where, 2048, "%s", url)'),
             ('char next[1024];', 'char next[2048];'), ('snprintf(where, 1024, "%s", next)', 'snprintf(where, 2048, "%s", next)')]:
    s = s.replace(a, b)
open(p, 'w').write(s)
print('done: cd %s && python3 tools/wupbuild.py examples/wup-tube' % lib)
