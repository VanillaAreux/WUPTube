#!/usr/bin/env python3
"""Prepare the Helix AAC sources for libwup's ARM target (idempotent; runs before the app is compiled)."""
from pathlib import Path

lib = Path('../../third_party/helix-aac/src/libhelix-aac')

# 1. portable fixed-point C routines instead of x86/ESP-only code
header = lib / 'assembly.h'
text = header.read_text()
old = '#elif defined(ARDUINO) || defined(ESP32)'
new = '#elif defined(__arm__) || defined(__aarch64__) || defined(ARDUINO) || defined(ESP32)'
if new not in text:
    if old not in text:
        raise SystemExit('Could not find the expected Helix platform-selection block in assembly.h')
    header.write_text(text.replace(old, new, 1))
print('Helix: portable fixed-point C implementation enabled for ARM')

# 2. every Helix .c file that calls printf gets <stdio.h> (clang rejects implicit declarations)
for f in sorted(lib.glob('*.c')):
    src = f.read_text()
    if 'printf(' in src and '#include <stdio.h>' not in src:
        f.write_text('#include <stdio.h>\n' + src)
        print('Helix: added <stdio.h> to', f.name)
