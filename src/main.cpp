#include <M5Dial.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "comms.h"
#include "meeting.h"
#include "display.h"

static bool     was_paired        = false;
static long     prev_encoder_pos  = 0;
static uint32_t last_heartbeat_send = 0;
static bool     time_synced       = false;

// Button long-press tracking
static bool     button_down       = false;
static uint32_t button_down_ms    = 0;
static bool     long_press_handled = false;

static void send_heartbeat() {
    uint8_t hours[MAX_MEETINGS], mins[MAX_MEETINGS];
    uint8_t cnt;
    meeting_get_packed(hours, mins, cnt);
    comms_send_heartbeat(meeting_get_status(), cnt, hours, mins);
}

static void sync_ntp() {
    Serial.println("[NTP] Connecting to WiFi...");
    M5Dial.Display.fillScreen(COLOR_BG);
    M5Dial.Display.setTextColor(COLOR_WHITE);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString("Syncing clock...", SCREEN_CENTER_X, SCREEN_CENTER_Y);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < NTP_TIMEOUT_MS) {
        delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[NTP] WiFi connected, syncing time...");
        configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);

        struct tm ti;
        start = millis();
        while (!getLocalTime(&ti) && millis() - start < 5000) {
            delay(100);
        }

        if (getLocalTime(&ti)) {
            time_synced = true;
            Serial.printf("[NTP] Time: %02d:%02d:%02d\n", ti.tm_hour, ti.tm_min, ti.tm_sec);
        } else {
            Serial.println("[NTP] Failed to get time");
        }
    } else {
        Serial.println("[NTP] WiFi connection failed");
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
}

void setup() {
    auto cfg = M5.config();
    M5Dial.begin(cfg, true, true);

    Serial.println("=== Dual Meeting Status ===");

    meeting_init();
    display_init();

    sync_ntp();
    comms_init();

    prev_encoder_pos = M5Dial.Encoder.read();
    Serial.println("Setup complete. Searching for peer...");
}

static void handle_button() {
    UIMode mode = meeting_get_mode();
    uint32_t now = millis();

    if (M5Dial.BtnA.wasPressed()) {
        button_down = true;
        button_down_ms = now;
        long_press_handled = false;
    }

    // Long press detection (while held)
    if (button_down && !long_press_handled && (now - button_down_ms >= LONG_PRESS_MS)) {
        long_press_handled = true;

        if (mode == UIMode::BROWSE && meeting_get_count() > 0) {
            // Long press in browse → delete selected meeting
            int idx = meeting_get_browse_index();
            meeting_remove(idx);
            M5Dial.Speaker.tone(1000, 150);

            if (meeting_get_count() == 0) {
                meeting_set_mode(UIMode::NORMAL);
            }
            display_force_redraw();
            Serial.println("[MAIN] Meeting deleted");
        } else if (mode == UIMode::NORMAL) {
            // Long press in normal → view peer meetings
            meeting_set_mode(UIMode::PEER_MEETINGS);
            M5Dial.Speaker.tone(BUZZER_FREQ, 50);
            display_force_redraw();
            Serial.println("[MAIN] Viewing peer meetings");
        }
    }

    if (M5Dial.BtnA.wasReleased()) {
        // Short press (released before long press threshold)
        if (button_down && !long_press_handled) {
            switch (mode) {
                case UIMode::NORMAL:
                    // Short press → add new meeting
                    if (meeting_get_count() < MAX_MEETINGS) {
                        meeting_set_mode(UIMode::ADD);
                        M5Dial.Speaker.tone(BUZZER_FREQ, 50);
                        Serial.println("[MAIN] Entering ADD mode");
                    } else {
                        M5Dial.Speaker.tone(500, 300);
                        Serial.println("[MAIN] Meeting list full");
                    }
                    break;

                case UIMode::BROWSE:
                    // Short press on selected meeting → edit it
                    meeting_set_mode(UIMode::EDIT);
                    M5Dial.Speaker.tone(BUZZER_FREQ, 50);
                    Serial.println("[MAIN] Entering EDIT mode");
                    break;

                case UIMode::ADD: {
                    uint8_t h, m;
                    slot_to_time(meeting_get_edit_slot(), h, m);
                    if (meeting_add(h, m)) {
                        M5Dial.Speaker.tone(BUZZER_FREQ, 100);
                        Serial.printf("[MAIN] Meeting added: %02d:%02d\n", h, m);
                    } else {
                        M5Dial.Speaker.tone(500, 200);
                        Serial.println("[MAIN] Duplicate or full");
                    }
                    meeting_set_mode(UIMode::NORMAL);
                    break;
                }

                case UIMode::EDIT: {
                    uint8_t h, m;
                    slot_to_time(meeting_get_edit_slot(), h, m);
                    meeting_update(meeting_get_browse_index(), h, m);
                    M5Dial.Speaker.tone(BUZZER_FREQ, 100);
                    Serial.printf("[MAIN] Meeting updated to %02d:%02d\n", h, m);
                    meeting_set_mode(UIMode::NORMAL);
                    break;
                }

                case UIMode::PEER_MEETINGS:
                    // Button press exits peer meetings view
                    meeting_set_mode(UIMode::NORMAL);
                    break;
            }
            display_force_redraw();
        }
        button_down = false;
    }
}

static void handle_touch() {
    auto touch = M5Dial.Touch.getDetail();
    if (!touch.wasPressed()) return;

    UIMode mode = meeting_get_mode();

    switch (mode) {
        case UIMode::NORMAL:
            // Bottom half → view peer meetings, top half → toggle my status
            if (touch.y > SCREEN_CENTER_Y) {
                meeting_set_mode(UIMode::PEER_MEETINGS);
                M5Dial.Speaker.tone(BUZZER_FREQ, 50);
            } else {
                meeting_toggle_status();
                M5Dial.Speaker.tone(BUZZER_FREQ, 50);
            }
            break;

        case UIMode::BROWSE:
        case UIMode::ADD:
        case UIMode::EDIT:
        case UIMode::PEER_MEETINGS:
            // Touch exits back to normal
            meeting_set_mode(UIMode::NORMAL);
            break;
    }
    display_force_redraw();
}

void loop() {
    M5Dial.update();
    comms_loop();

    bool paired = comms_is_paired();

    // Pair/unpair transitions
    if (paired && !was_paired) {
        display_force_redraw();
        Serial.println("[MAIN] Peer connected!");
        uint8_t hours[MAX_MEETINGS], mins[MAX_MEETINGS];
        uint8_t cnt;
        meeting_get_packed(hours, mins, cnt);
        comms_send_status(meeting_get_status(), cnt, hours, mins);
    }
    if (!paired && was_paired) {
        display_force_redraw();
        Serial.println("[MAIN] Peer disconnected.");
    }
    was_paired = paired;

    // Input handling
    handle_touch();
    handle_button();

    // Encoder
    long encoder_pos = M5Dial.Encoder.read();
    long delta = encoder_pos - prev_encoder_pos;
    if (delta != 0) {
        prev_encoder_pos = encoder_pos;
        meeting_encoder_changed((int)delta);
        display_force_redraw();
    }

    // Meeting state
    meeting_loop();

    // Heartbeat
    uint32_t now = millis();
    if (now - last_heartbeat_send >= HEARTBEAT_MS) {
        last_heartbeat_send = now;
        send_heartbeat();
    }

    // Display
    if (!paired && !comms_is_paired()) {
        display_show_searching();
    } else {
        PeerState peer = comms_get_peer_state();
        display_update(meeting_get_status(), meeting_get_mode(),
                       meeting_get_all(), meeting_get_count(),
                       meeting_get_browse_index(), meeting_get_edit_slot(),
                       meeting_get_peer_browse_index(),
                       peer);
    }

    display_loop();
}
