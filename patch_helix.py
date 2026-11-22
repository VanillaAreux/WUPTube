#!/usr/bin/env python3
"""Enable Helix's portable fixed-point C routines for libwup's ARM target."""
from pathlib import Path

header = Path('../../third_party/helix-aac/src/libhelix-aac/assembly.h')
source = header.read_text()
old = '#elif defined(ARDUINO) || defined(ESP32)'
new = '#elif defined(__arm__) || defined(__aarch64__) || defined(ARDUINO) || defined(ESP32)'
if new not in source:
    if old not in source:
        raise SystemExit('Could not find the expected Helix platform-selection block in assembly.h')
    source = source.replace(old, new, 1)
    header.write_text(source)
print('Helix: enabled portable fixed-point C implementation for ARM')

# Helix's SBR implementation calls printf in its error path but does not include
# the declaration. Clang treats the implicit declaration as an error in C99+.
sbr = Path('../../third_party/helix-aac/src/libhelix-aac/sbr.c')
sbr_source = sbr.read_text()
if '#include <stdio.h>' not in sbr_source:
    sbr.write_text('#include <stdio.h>\n' + sbr_source)
print('Helix: added stdio declaration for SBR diagnostics')
