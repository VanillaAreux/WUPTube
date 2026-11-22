# WUP Tube Music (v0.1, untested on hardware)

Search and play YouTube Music audio on the Wii U GamePad, running on the pad itself: Wi-Fi + HTTPS, no PC or Wii U.
Audio only (the pad has no video decoder). Talks to a **Piped** server's API (plain GET, which libwup's HTTPS supports).

## Build
```
python3 patch_sdk.py /path/to/wup-homebrew      # copies the app to libwup/examples/wup-tube, adds the AAC decoder dep, lengthens URLs in https.c
cd /path/to/wup-homebrew/libwup
python3 tools/wupbuild.py examples/wup-tube     # -> build/apps/wup_tube_music.wup
python3 tools/wuplink.py build/apps/wup_tube_music.wup
python3 tools/wuplog.py                         # printf/log lines
```
Join Wi-Fi in the Homebrew Menu first (HTTPS needs the clock).

## Use
Search (on-screen keyboard) > A on a song. **+** pause, **R** next, **L** previous, anywhere. The server address is under
"Server" (default `https://pipedapi.kavin.rocks`; public Piped instances come and go, so change it if search fails).

## How it works
- `api.c`: `/search?filter=music_songs` and `/streams/<id>`; streams the JSON and keeps only the array it needs (bounded RAM).
- `player.c`: a net thread reads the m4a in 64 KB ranged requests into a 160 KB ring; a decoder thread parses the fragmented MP4
  (`mp4.c`), decodes AAC-LC with Helix, resamples 44.1 > 48 kHz and feeds `wup_audio_queue`. One HTTPS request at a time.
- `json.c`, `mp4.c` are plain C and were tested on a PC; `api.c` and `player.c` only pass a syntax check against stub headers.
  `main.c` (the LVGL screens) has not been compiled at all.

## Things most likely to need a fix
- **Helix source**: `patch_sdk.py` fetches it from `pschatzmann/arduino-libhelix` (`src/libhelix-aac`). If the layout or branch
  differs, edit the `helix-aac` line in `libwup/tools/deps.py` and the `include`/`sources` paths in `app.cfg`. Helix is under
  RealNetworks' RPSL/RCSL, not GPL: build it for yourself, don't redistribute the binary.
- **Range requests**: if the server ignores `Range`, the app falls back to one whole-file request (searching then stops the song).
- **CPU / RAM**: AAC on the ARM926 and TLS handshakes every 64 KB are the unknowns. If audio stutters, raise `CHUNK`/`RING` in `player.c`.
- Thread-stack sizes (32 KB each) and `large = yes` may need tuning.
- No seeking, no thumbnails, no playlists/login yet.
