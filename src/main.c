/* WUP Tube Music: search and play YouTube Music audio on the Wii U GamePad, on the pad itself (Wi-Fi + HTTPS),
 * through a Piped server. Hotkeys on every screen: PLUS pause/resume, R next, L previous. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wup.h"
#include "wup_kit.h"
#include "api.h"
#include "player.h"

WUP_APP("WUP Tube Music");

#define MAXT 25
static track_t tracks[MAXT];
static int ntracks, cur = -1;
static char query[96];
static int sstate;                         /* search screen: 0 working, 1 done, 2 failed */
static int sresult;                        /* its count, or the error */
static int np_state;                       /* now playing: 0 loading, 1 playing, 2 failed */
static char np_err[96];
static int pending = -1;                   /* a track to start, done in the main loop */
static lv_obj_t *np_title, *np_info;

static void home(lv_obj_t *scr);
static void search_screen(lv_obj_t *scr);
static void results_screen(lv_obj_t *scr);
static void np_screen(lv_obj_t *scr);
static void settings_screen(lv_obj_t *scr);
static void text_screen(lv_obj_t *scr);

/* ---- a text box with the keyboard, for the search and the server address */
static const char *text_title; static char *text_dst; static size_t text_cap; static void (*text_done)(void);
static lv_obj_t *text_ta;

static void kb_ready(lv_event_t *e) {
    (void)e;
    snprintf(text_dst, text_cap, "%s", lv_textarea_get_text(text_ta));
    kit_pop();
    if (text_done) text_done();
}
static void open_text(const char *title, char *dst, size_t cap, void (*done)(void)) {
    text_title = title; text_dst = dst; text_cap = cap; text_done = done;
    kit_push(text_screen);
}
static void text_screen(lv_obj_t *scr) {
    lv_obj_t *l = kit_page(scr, text_title, "B Cancel    A Key    Done: the check mark");
    text_ta = lv_textarea_create(l);
    lv_textarea_set_one_line(text_ta, true);
    lv_textarea_set_text(text_ta, text_dst);
    lv_obj_set_width(text_ta, lv_pct(100));
    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_obj_set_size(kb, lv_pct(100), 210);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, text_ta);
    lv_obj_add_event_cb(kb, kb_ready, LV_EVENT_READY, NULL);
    lv_group_t *g = lv_group_get_default();
    if (g) { lv_group_add_obj(g, kb); lv_group_focus_obj(kb); }
}

/* ---- starting a track */
static void err_note(const char *what, int code) { snprintf(np_err, sizeof np_err, "%s", what ? what : api_err(code)); }

static void start_track(int i) {                       /* blocking: the stream address, then the player */
    char url[1600]; long long size = 0;
    cur = i; np_state = 0;
    player_stop();
    int r = api_stream(tracks[i].id, url, sizeof url, &size);
    if (r) { np_state = 2; err_note(NULL, r); return; }
    player_play(url, size);
    np_state = 1;
}

static void skip(int d) {
    if (cur < 0) return;
    int n = cur + d;
    if (n < 0 || n >= ntracks) return;
    wup_notice("Loading...", 1);
    pending = n;
}

static void on_track(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    pending = i; np_state = 0; cur = i;
    kit_push(np_screen);
}

/* ---- screens */
static void do_search(void) {
    track_t *t = tracks; int n = api_search(query, t, MAXT);
    sresult = n; sstate = n < 0 ? 2 : 1;
    if (n >= 0) { ntracks = n; cur = -1; }
    kit_show();
}
static void results_screen(lv_obj_t *scr) {
    lv_obj_t *l = kit_page(scr, "Results", "B Back    A Play");
    if (sstate == 0) { kit_busy(l, "Searching", query, NULL); kit_defer(do_search); return; }
    if (sstate == 2) { kit_outcome(l, "Search failed", api_err(sresult)); return; }
    if (!ntracks) { kit_empty(l, "Nothing found", query); return; }
    char val[80];
    for (int i = 0; i < ntracks; i++) {
        snprintf(val, sizeof val, "%s  %d:%02d", tracks[i].artist, tracks[i].dur / 60, tracks[i].dur % 60);
        kit_row(l, NULL, tracks[i].title, val, on_track, (void *)(intptr_t)i);
    }
}
static void after_query(void) { if (query[0]) { sstate = 0; kit_push(results_screen); } }
static void on_search(lv_event_t *e) { (void)e; open_text("Search music", query, sizeof query, after_query); }
static void on_np(lv_event_t *e) { (void)e; if (cur >= 0) kit_push(np_screen); }
static void on_settings(lv_event_t *e) { (void)e; kit_push(settings_screen); }

static void home(lv_obj_t *scr) {
    lv_obj_t *l = kit_page(scr, "WUP Tube Music", "B Exit    A Select    +  Pause    L R  Skip");
    kit_row(l, NULL, "Search", NULL, on_search, NULL);
    if (cur >= 0) kit_row(l, NULL, "Now playing", tracks[cur].title, on_np, NULL);
    kit_row(l, NULL, "Server", api_base, on_settings, NULL);
    if (!wup_time_known()) kit_note(l, "No clock yet: join Wi-Fi in the Homebrew Menu first (HTTPS needs the date).");
}

static void after_server(void) { api_save_cfg(); }
static void on_server(lv_event_t *e) { (void)e; open_text("Piped API address (https://...)", api_base, sizeof api_base, after_server); }
static void settings_screen(lv_obj_t *scr) {
    lv_obj_t *l = kit_page(scr, "Server", "B Back    A Change");
    kit_row(l, NULL, "Piped API address", api_base, on_server, NULL);
    kit_note(l, "Any Piped instance's API address. Public ones come and go: if search fails, try another.");
}

static void np_toggle(void) { player_pause(player_state() != PL_PAUSED); }
static void np_gone(lv_event_t *e) { (void)e; np_title = np_info = NULL; }
static void np_screen(lv_obj_t *scr) {
    lv_obj_t *l = kit_page(scr, "Now playing", "B Back    A Pause    L Prev    R Next");
    kit_on_a(np_toggle);
    if (np_state == 0) { kit_busy(l, "Loading", cur >= 0 ? tracks[cur].title : "", NULL); return; }
    if (np_state == 2) { kit_outcome(l, "Could not play", np_err); return; }
    np_title = lv_label_create(l);
    lv_label_set_text(np_title, tracks[cur].title);
    lv_label_set_long_mode(np_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(np_title, lv_pct(100));
    np_info = lv_label_create(l);
    lv_label_set_text(np_info, "");
    lv_obj_add_event_cb(np_info, np_gone, LV_EVENT_DELETE, NULL);
}

/* ---- a tick every 100 ms: hotkeys, status line, next song */
static void tick(lv_timer_t *t) {
    (void)t;
    static unsigned prev;
    unsigned b = wup_buttons(), pressed = b & ~prev; prev = b;
    if (pressed & WUP_PLUS) np_toggle();
    if (pressed & WUP_R) skip(1);
    if (pressed & WUP_L) skip(-1);
    int st = player_state();
    if (st == PL_ENDED && pending < 0) { player_stop(); if (cur + 1 < ntracks) skip(1); }
    if (st == PL_ERROR && np_state == 1) { np_state = 2; err_note(player_error(), 0); player_stop(); }
    if (np_info && np_state == 1) {
        static const char *names[] = { "", "Buffering...", "Playing", "Paused", "Finished", "Error" };
        int s = player_seconds(), d = cur >= 0 ? tracks[cur].dur : 0;
        char buf[160];
        snprintf(buf, sizeof buf, "%s\n%s\n%d:%02d / %d:%02d%s", tracks[cur].artist, names[st], s / 60, s % 60, d / 60, d % 60,
                 player_whole_file() ? "\n(this server sends whole files: searching stops the song)" : "");
        lv_label_set_text(np_info, buf);
    }
}

static void before_ui_request(void) { if (player_whole_file()) player_stop(); }

/* ---- start-up diagnostics: a plain splash (no LVGL) so a failure to start can be seen on the pad */
static unsigned heap_probe_kb(void) {                  /* the biggest block malloc can give, in 16 KB steps */
    unsigned best = 0;
    for (unsigned s = 16; s <= 700; s += 16) { void *p = malloc((size_t)s * 1024); if (!p) break; free(p); best = s; }
    return best;
}
static void splash(const char *l1, const char *l2) {
    wup_screen();
    gfx_vgrad24(0, 0, GFX_W, GFX_H, C24(24, 44, 96), C24(6, 10, 30));
    gfx_text_c(&font_big, GFX_W / 2, 190, l1, RGB(255, 255, 255));
    gfx_text_c(&font_body, GFX_W / 2, 260, l2, RGB(190, 200, 225));
    gfx_present_all();
}

int main(void) {
    char info[96];
    unsigned heap = heap_probe_kb();
    snprintf(info, sizeof info, "Free memory: %u KB%s", heap, heap < 250 ? "  (low!)" : "");
    splash("WUP Tube Music", info);
    while (wup_buttons() & WUP_B) wup_sleep(16);          /* not a B still held from the menu */
    wup_sleep(1500);
    uint32_t t0 = wup_us();
    api_load_cfg();
    wup_net_init();
    api_idle = kit_idle;
    api_before = before_ui_request;
    kit_start(home);
    lv_timer_create(tick, 100, NULL);
    while (kit_step()) {
        if (pending >= 0) {
            int i = pending; pending = -1;
            start_track(i);
            if (kit_current() == np_screen || kit_current() == home) kit_show();
            else if (np_state == 2) wup_notice(np_err, 0);
        }
    }
    player_stop();
    if ((uint32_t)(wup_us() - t0) < 4000000u) {            /* ended at once: say so, then go back to the menu */
        splash("The app ended right away", "B at the first screen closes it. Press B to return.");
        while (!(wup_buttons() & WUP_B)) wup_sleep(16);
    }
    return 0;
}
