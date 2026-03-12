#include "display.h"
#include <M5Dial.h>
#include <time.h>

// Cached state for partial updates
static MeetingStatus prev_my_status     = MeetingStatus::FREE;
static UIMode        prev_mode          = UIMode::NORMAL;
static int           prev_meeting_count = 0;
static int           prev_browse_index  = 0;
static int           prev_edit_slot     = 0;
static int           prev_peer_browse   = 0;
static MeetingStatus prev_peer_status   = MeetingStatus::FREE;
static bool          prev_peer_connected = false;
static int           prev_peer_mcount   = 0;
static bool          first_draw         = true;
static uint32_t      last_refresh_ms    = 0;
static bool          force_redraw       = false;
static int           prev_clock_min     = -1;

// Searching animation state
static int search_angle = 0;

static void draw_background() {
    M5Dial.Display.fillScreen(COLOR_BG);
    M5Dial.Display.drawCircle(SCREEN_CENTER_X, SCREEN_CENTER_Y, SCREEN_RADIUS - 1, COLOR_DARK_GRAY);
}

// --- TOP: My status (small) ---
static void draw_my_status(MeetingStatus status) {
    M5Dial.Display.fillRect(0, 0, SCREEN_WIDTH, 55, COLOR_BG);

    uint16_t color;
    const char *text;

    if (status == MeetingStatus::IN_MEETING) {
        color = COLOR_RED;
        text = "IN MEETING";
    } else {
        color = COLOR_GREEN;
        text = "FREE";
    }

    M5Dial.Display.fillCircle(SCREEN_CENTER_X - 55, 30, 8, color);

    M5Dial.Display.setTextColor(COLOR_GRAY);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.drawString("ME:", SCREEN_CENTER_X + 5, 18);

    M5Dial.Display.setTextColor(color);
    M5Dial.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5Dial.Display.drawString(text, SCREEN_CENTER_X + 5, 42);
}

// --- CENTER: Peer status (large circle) ---
static void draw_peer_status(const PeerState &peer) {
    M5Dial.Display.fillRect(5, 55, SCREEN_WIDTH - 10, 110, COLOR_BG);

    uint16_t color;

    if (!peer.connected) {
        color = COLOR_GRAY;
    } else if (peer.status == MeetingStatus::IN_MEETING) {
        color = COLOR_RED;
    } else {
        color = COLOR_GREEN;
    }

    M5Dial.Display.fillCircle(SCREEN_CENTER_X, 110, 52, color);

    M5Dial.Display.setTextColor(COLOR_WHITE);
    M5Dial.Display.setTextDatum(middle_center);

    if (!peer.connected) {
        M5Dial.Display.setFont(&fonts::FreeSansBold12pt7b);
        M5Dial.Display.setTextSize(1);
        M5Dial.Display.drawString("OFFLINE", SCREEN_CENTER_X, 110);
    } else if (peer.status == MeetingStatus::IN_MEETING) {
        M5Dial.Display.setFont(&fonts::FreeSansBold9pt7b);
        M5Dial.Display.setTextSize(1);
        M5Dial.Display.drawString("IN", SCREEN_CENTER_X, 103);
        M5Dial.Display.drawString("MEETING", SCREEN_CENTER_X, 120);
    } else {
        M5Dial.Display.setFont(&fonts::FreeSansBold18pt7b);
        M5Dial.Display.setTextSize(1);
        M5Dial.Display.drawString("FREE", SCREEN_CENTER_X, 110);
    }
}

// --- BOTTOM: Peer's next meeting time ---
static void draw_normal_bottom(const PeerState &peer) {
    M5Dial.Display.fillRect(10, 160, SCREEN_WIDTH - 20, 72, COLOR_BG);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextSize(1);

    if (!peer.connected) {
        M5Dial.Display.setTextColor(COLOR_GRAY);
        M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
        M5Dial.Display.drawString("Peer not connected", SCREEN_CENTER_X, 195);
    } else if (peer.meeting_count > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Peer next: %02d:%02d",
            peer.meetings[0].hour, peer.meetings[0].minute);
        M5Dial.Display.setTextColor(COLOR_WHITE);
        M5Dial.Display.setFont(&fonts::FreeSansBold12pt7b);
        M5Dial.Display.drawString(buf, SCREEN_CENTER_X, 185);

        snprintf(buf, sizeof(buf), "%d meeting%s",
            peer.meeting_count, peer.meeting_count == 1 ? "" : "s");
        M5Dial.Display.setTextColor(COLOR_GRAY);
        M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
        M5Dial.Display.drawString(buf, SCREEN_CENTER_X, 210);
    } else {
        M5Dial.Display.setTextColor(COLOR_GRAY);
        M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
        M5Dial.Display.drawString("Peer: no meetings", SCREEN_CENTER_X, 195);
    }
}

static void draw_browse_mode(const MeetingSlot* meetings, int count, int browse_idx) {
    draw_background();

    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextSize(1);

    M5Dial.Display.setTextColor(COLOR_YELLOW);
    M5Dial.Display.setFont(&fonts::FreeSansBold9pt7b);
    char title[24];
    snprintf(title, sizeof(title), "MEETINGS (%d/%d)", browse_idx + 1, count);
    M5Dial.Display.drawString(title, SCREEN_CENTER_X, 25);

    if (browse_idx >= 0 && browse_idx < count) {
        char time_buf[8];
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
            meetings[browse_idx].hour, meetings[browse_idx].minute);

        M5Dial.Display.setTextColor(COLOR_WHITE);
        M5Dial.Display.setFont(&fonts::FreeSansBold18pt7b);
        M5Dial.Display.drawString(time_buf, SCREEN_CENTER_X, 80);

        if (count > 1) {
            int prev_i = (browse_idx - 1 + count) % count;
            int next_i = (browse_idx + 1) % count;

            char buf[8];
            M5Dial.Display.setTextColor(COLOR_DARK_GRAY);
            M5Dial.Display.setFont(&fonts::FreeSans9pt7b);

            snprintf(buf, sizeof(buf), "%02d:%02d", meetings[prev_i].hour, meetings[prev_i].minute);
            M5Dial.Display.drawString(buf, SCREEN_CENTER_X, 50);

            snprintf(buf, sizeof(buf), "%02d:%02d", meetings[next_i].hour, meetings[next_i].minute);
            M5Dial.Display.drawString(buf, SCREEN_CENTER_X, 110);
        }
    }

    M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
    M5Dial.Display.setTextSize(1);

    M5Dial.Display.setTextColor(COLOR_YELLOW);
    M5Dial.Display.drawString("Btn: edit", SCREEN_CENTER_X, 155);

    M5Dial.Display.setTextColor(COLOR_RED);
    M5Dial.Display.drawString("Hold btn: delete", SCREEN_CENTER_X, 180);

    M5Dial.Display.setTextColor(COLOR_GRAY);
    M5Dial.Display.drawString("Touch: back", SCREEN_CENTER_X, 205);
}

static void draw_add_edit_mode(UIMode mode, int edit_slot_idx, int count) {
    draw_background();

    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextSize(1);

    uint16_t accent = (mode == UIMode::ADD) ? COLOR_BLUE : COLOR_YELLOW;
    const char* title = (mode == UIMode::ADD) ? "ADD MEETING" : "EDIT TIME";

    M5Dial.Display.setTextColor(accent);
    M5Dial.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5Dial.Display.drawString(title, SCREEN_CENTER_X, 30);

    uint8_t h, m;
    slot_to_time(edit_slot_idx, h, m);

    M5Dial.Display.fillRoundRect(45, 65, 150, 60, 10, accent);
    M5Dial.Display.setTextColor(COLOR_WHITE);
    M5Dial.Display.setFont(&fonts::FreeSansBold18pt7b);

    char time_buf[8];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", h, m);
    M5Dial.Display.drawString(time_buf, SCREEN_CENTER_X, 95);

    M5Dial.Display.setTextColor(accent);
    M5Dial.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5Dial.Display.drawString("<  Turn knob  >", SCREEN_CENTER_X, 145);

    if (mode == UIMode::ADD) {
        char cap[16];
        snprintf(cap, sizeof(cap), "%d/%d slots", count, MAX_MEETINGS);
        M5Dial.Display.setTextColor(COLOR_GRAY);
        M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
        M5Dial.Display.drawString(cap, SCREEN_CENTER_X, 170);
    }

    M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
    M5Dial.Display.setTextColor(COLOR_GREEN);
    M5Dial.Display.drawString("Btn: confirm", SCREEN_CENTER_X, 195);
    M5Dial.Display.setTextColor(COLOR_GRAY);
    M5Dial.Display.drawString("Touch: cancel", SCREEN_CENTER_X, 218);
}

static void draw_peer_meetings(const PeerState &peer, int browse_idx) {
    draw_background();

    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextSize(1);

    // Title
    M5Dial.Display.setTextColor(COLOR_BLUE);
    M5Dial.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5Dial.Display.drawString("PEER", SCREEN_CENTER_X, 30);

    int cnt = peer.meeting_count;

    if (!peer.connected) {
        M5Dial.Display.setTextColor(COLOR_GRAY);
        M5Dial.Display.setFont(&fonts::FreeSansBold12pt7b);
        M5Dial.Display.drawString("Peer offline", SCREEN_CENTER_X, 120);
    } else if (cnt == 0) {
        M5Dial.Display.setTextColor(COLOR_GRAY);
        M5Dial.Display.setFont(&fonts::FreeSansBold12pt7b);
        M5Dial.Display.drawString("No meetings", SCREEN_CENTER_X, 110);
        M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
        M5Dial.Display.drawString("Peer has a free day", SCREEN_CENTER_X, 140);
    } else {
        // Show counter
        char counter[16];
        snprintf(counter, sizeof(counter), "(%d/%d)", browse_idx + 1, cnt);
        M5Dial.Display.setTextColor(COLOR_GRAY);
        M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
        M5Dial.Display.drawString(counter, SCREEN_CENTER_X, 50);

        // Show selected meeting large
        if (browse_idx >= 0 && browse_idx < cnt) {
            char time_buf[8];
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
                peer.meetings[browse_idx].hour, peer.meetings[browse_idx].minute);

            M5Dial.Display.setTextColor(COLOR_WHITE);
            M5Dial.Display.setFont(&fonts::FreeSansBold18pt7b);
            M5Dial.Display.drawString(time_buf, SCREEN_CENTER_X, 90);

            // Show prev/next
            if (cnt > 1) {
                int prev_i = (browse_idx - 1 + cnt) % cnt;
                int next_i = (browse_idx + 1) % cnt;

                char buf[8];
                M5Dial.Display.setTextColor(COLOR_DARK_GRAY);
                M5Dial.Display.setFont(&fonts::FreeSans9pt7b);

                snprintf(buf, sizeof(buf), "%02d:%02d",
                    peer.meetings[prev_i].hour, peer.meetings[prev_i].minute);
                M5Dial.Display.drawString(buf, SCREEN_CENTER_X, 65);

                snprintf(buf, sizeof(buf), "%02d:%02d",
                    peer.meetings[next_i].hour, peer.meetings[next_i].minute);
                M5Dial.Display.drawString(buf, SCREEN_CENTER_X, 120);
            }
        }

        // Peer status indicator
        uint16_t scolor = (peer.status == MeetingStatus::IN_MEETING) ? COLOR_RED : COLOR_GREEN;
        const char* stxt = (peer.status == MeetingStatus::IN_MEETING) ? "In meeting now" : "Free now";
        M5Dial.Display.fillCircle(SCREEN_CENTER_X - 60, 155, 6, scolor);
        M5Dial.Display.setTextColor(scolor);
        M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
        M5Dial.Display.drawString(stxt, SCREEN_CENTER_X + 10, 155);
    }

    // Hint at bottom
    M5Dial.Display.setTextColor(COLOR_GRAY);
    M5Dial.Display.setFont(&fonts::FreeSans9pt7b);
    M5Dial.Display.drawString("Turn: browse", SCREEN_CENTER_X, 190);
    M5Dial.Display.drawString("Touch: back", SCREEN_CENTER_X, 215);
}

static void draw_clock(bool force) {
    struct tm ti;
    if (!getLocalTime(&ti)) return;

    // Only redraw if minute changed (or forced)
    if (!force && ti.tm_min == prev_clock_min) return;
    prev_clock_min = ti.tm_min;

    // Clear small area at bottom-left inside the circle
    M5Dial.Display.fillRect(75, 224, 90, 14, COLOR_BG);

    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);

    M5Dial.Display.setTextColor(COLOR_DARK_GRAY);
    M5Dial.Display.setFont(&fonts::Font0);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.drawString(buf, SCREEN_CENTER_X, 230);
}

void display_init() {
    first_draw = true;
    force_redraw = false;
    prev_clock_min = -1;
}

void display_show_searching() {
    uint32_t now = millis();
    if (now - last_refresh_ms < 80) return;
    last_refresh_ms = now;

    if (first_draw) {
        draw_background();
        first_draw = false;
    }

    M5Dial.Display.fillRect(30, 60, SCREEN_WIDTH - 60, 130, COLOR_BG);

    search_angle = (search_angle + 30) % 360;
    for (int i = 0; i < 8; i++) {
        int angle = search_angle + i * 45;
        float rad = angle * 3.14159f / 180.0f;
        int x = SCREEN_CENTER_X + (int)(40.0f * cosf(rad));
        int y = 110 + (int)(40.0f * sinf(rad));
        uint16_t c = (i == 0) ? COLOR_WHITE : COLOR_DARK_GRAY;
        M5Dial.Display.fillCircle(x, y, 4, c);
    }

    M5Dial.Display.setTextColor(COLOR_WHITE);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString("Searching", SCREEN_CENTER_X, 170);
    M5Dial.Display.drawString("for peer...", SCREEN_CENTER_X, 190);
}

void display_force_redraw() {
    force_redraw = true;
    first_draw = true;
}

void display_update(MeetingStatus my_status, UIMode mode,
                    const MeetingSlot* meetings, int meeting_count,
                    int browse_index, int edit_slot_idx,
                    int peer_browse_idx,
                    const PeerState &peer) {
    uint32_t now = millis();
    if (!force_redraw && (now - last_refresh_ms < SCREEN_REFRESH_MS)) return;
    last_refresh_ms = now;

    bool need_full = first_draw || force_redraw || (mode != prev_mode);

    if (mode == UIMode::BROWSE) {
        if (need_full || browse_index != prev_browse_index) {
            draw_browse_mode(meetings, meeting_count, browse_index);
            prev_browse_index = browse_index;
        }
    } else if (mode == UIMode::ADD || mode == UIMode::EDIT) {
        if (need_full || edit_slot_idx != prev_edit_slot) {
            draw_add_edit_mode(mode, edit_slot_idx, meeting_count);
            prev_edit_slot = edit_slot_idx;
        }
    } else if (mode == UIMode::PEER_MEETINGS) {
        if (need_full || peer_browse_idx != prev_peer_browse) {
            draw_peer_meetings(peer, peer_browse_idx);
            prev_peer_browse = peer_browse_idx;
        }
    } else {
        // NORMAL mode — partial updates
        if (need_full) {
            draw_background();
            draw_my_status(my_status);
            draw_peer_status(peer);
            draw_normal_bottom(peer);
        } else {
            if (my_status != prev_my_status) draw_my_status(my_status);
            bool peer_changed = (peer.status != prev_peer_status) ||
                                (peer.connected != prev_peer_connected) ||
                                ((int)peer.meeting_count != prev_peer_mcount);
            if (peer_changed) {
                draw_peer_status(peer);
                draw_normal_bottom(peer);
            }
        }
    }

    // Draw clock on every screen
    draw_clock(need_full);

    // Cache state
    prev_my_status       = my_status;
    prev_mode            = mode;
    prev_meeting_count   = meeting_count;
    prev_peer_status     = peer.status;
    prev_peer_connected  = peer.connected;
    prev_peer_mcount     = peer.meeting_count;
    first_draw           = false;
    force_redraw         = false;
}

void display_loop() {
}
