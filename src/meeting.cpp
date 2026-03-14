#include "meeting.h"
#include "comms.h"
#include "display.h"
#include <M5Dial.h>
#include <time.h>

static MeetingStatus current_status = MeetingStatus::FREE;
static MeetingSlot   meetings[MAX_MEETINGS];
static int           meeting_count = 0;

// UI state
static UIMode ui_mode           = UIMode::NORMAL;
static int    browse_index      = 0;
static int    edit_slot         = 0;
static int    peer_browse_index = 0;

// ADD mode sub-step: 0 = selecting start time, 1 = selecting duration
static int    add_step          = 0;
static int    duration_slot     = 5;  // default index 5 → 30 min

// Auto-status tracking
static uint32_t last_time_check_ms = 0;
static bool     cleared_at_5pm     = false;
static bool     manual_override    = false;  // user manually toggled status
static bool     prev_in_meeting    = false;  // previous auto-status state

void slot_to_time(int slot, uint8_t &hour, uint8_t &minute) {
    int total_minutes = MEETING_HOUR_MIN * 60 + slot * MEETING_STEP_MIN;
    hour   = total_minutes / 60;
    minute = total_minutes % 60;
}

int time_to_slot(uint8_t hour, uint8_t minute) {
    int total = (hour * 60 + minute) - (MEETING_HOUR_MIN * 60);
    return total / MEETING_STEP_MIN;
}

static void send_state_to_peer() {
    uint8_t hours[MAX_MEETINGS], mins[MAX_MEETINGS], durs[MAX_MEETINGS];
    uint8_t cnt;
    meeting_get_packed(hours, mins, durs, cnt);
    // Send multiple times for reliability (ESP-NOW can drop packets)
    for (int i = 0; i < 3; i++) {
        comms_send_status(current_status, cnt, hours, mins, durs);
        if (i < 2) delay(10);
    }
}

void meeting_init() {
    current_status    = MeetingStatus::FREE;
    meeting_count     = 0;
    ui_mode           = UIMode::NORMAL;
    browse_index      = 0;
    edit_slot         = 0;
    peer_browse_index = 0;
    add_step          = 0;
    duration_slot     = 5;  // 30 min default
    cleared_at_5pm    = false;
    for (int i = 0; i < MAX_MEETINGS; i++) {
        meetings[i].active = false;
    }
}

void meeting_loop() {
    uint32_t now_ms = millis();
    if (now_ms - last_time_check_ms < 1000) return;  // check once per second
    last_time_check_ms = now_ms;

    struct tm ti;
    if (!getLocalTime(&ti)) return;

    int now_minutes = ti.tm_hour * 60 + ti.tm_min;

    // --- Clear all meetings at 5 PM (disabled for now) ---
    // if (ti.tm_hour >= MEETING_HOUR_MAX && !cleared_at_5pm) {
    //     if (meeting_count > 0) {
    //         Serial.println("[MEETING] 5 PM cleanup: removing all meetings");
    //         meeting_count = 0;
    //         for (int i = 0; i < MAX_MEETINGS; i++) {
    //             meetings[i].active = false;
    //         }
    //         if (ui_mode == UIMode::BROWSE) {
    //             ui_mode = UIMode::NORMAL;
    //         }
    //         send_state_to_peer();
    //         display_force_redraw();
    //     }
    //     cleared_at_5pm = true;
    //     return;
    // }
    // Reset flag after midnight so it works next day
    if (ti.tm_hour < MEETING_HOUR_MAX) {
        cleared_at_5pm = false;
    }

    // --- Remove past meetings (end time <= now) ---
    bool removed_any = false;
    for (int i = meeting_count - 1; i >= 0; i--) {
        int end_min = meetings[i].hour * 60 + meetings[i].minute + meetings[i].duration;
        if (end_min <= now_minutes) {
            Serial.printf("[MEETING] Auto-removed past meeting %02d:%02d\n",
                meetings[i].hour, meetings[i].minute);
            for (int j = i; j < meeting_count - 1; j++) {
                meetings[j] = meetings[j + 1];
            }
            meetings[meeting_count - 1].active = false;
            meeting_count--;
            removed_any = true;
        }
    }
    if (removed_any) {
        if (browse_index >= meeting_count && meeting_count > 0) {
            browse_index = meeting_count - 1;
        }
        if (meeting_count == 0 && ui_mode == UIMode::BROWSE) {
            ui_mode = UIMode::NORMAL;
        }
        send_state_to_peer();
        display_force_redraw();
    }

    // --- Auto-status: only change on meeting boundary transitions ---
    bool in_any_meeting = false;
    for (int i = 0; i < meeting_count; i++) {
        int start_min = meetings[i].hour * 60 + meetings[i].minute;
        int end_min   = start_min + meetings[i].duration;
        if (now_minutes >= start_min && now_minutes < end_min) {
            in_any_meeting = true;
            break;
        }
    }

    // Detect transition (meeting just started or just ended)
    if (in_any_meeting != prev_in_meeting) {
        prev_in_meeting = in_any_meeting;
        manual_override = false;  // clear override on boundary change
        MeetingStatus new_status = in_any_meeting ? MeetingStatus::IN_MEETING : MeetingStatus::FREE;
        if (new_status != current_status) {
            current_status = new_status;
            Serial.printf("[MEETING] Auto-status: %s\n",
                current_status == MeetingStatus::FREE ? "FREE" : "IN_MEETING");
            send_state_to_peer();
            display_force_redraw();
        }
    }
}

MeetingStatus meeting_get_status() {
    return current_status;
}

void meeting_toggle_status() {
    if (current_status == MeetingStatus::FREE) {
        current_status = MeetingStatus::IN_MEETING;
    } else {
        current_status = MeetingStatus::FREE;
    }
    manual_override = true;
    Serial.printf("[MEETING] Status (manual): %s\n",
        current_status == MeetingStatus::FREE ? "FREE" : "IN_MEETING");
    send_state_to_peer();
}

int meeting_get_count() {
    return meeting_count;
}

const MeetingSlot* meeting_get_all() {
    return meetings;
}

void meeting_get_packed(uint8_t* hours, uint8_t* minutes, uint8_t* durations, uint8_t &count) {
    count = (uint8_t)meeting_count;
    for (int i = 0; i < meeting_count; i++) {
        hours[i]     = meetings[i].hour;
        minutes[i]   = meetings[i].minute;
        durations[i] = meetings[i].duration;
    }
}

void meeting_sort() {
    for (int i = 1; i < meeting_count; i++) {
        MeetingSlot tmp = meetings[i];
        int j = i - 1;
        while (j >= 0 && (meetings[j].hour * 60 + meetings[j].minute) >
                         (tmp.hour * 60 + tmp.minute)) {
            meetings[j + 1] = meetings[j];
            j--;
        }
        meetings[j + 1] = tmp;
    }
}

bool meeting_add(uint8_t hour, uint8_t minute, uint8_t duration) {
    if (meeting_count >= MAX_MEETINGS) return false;

    for (int i = 0; i < meeting_count; i++) {
        if (meetings[i].hour == hour && meetings[i].minute == minute) {
            return false;
        }
    }

    meetings[meeting_count].hour     = hour;
    meetings[meeting_count].minute   = minute;
    meetings[meeting_count].duration = duration;
    meetings[meeting_count].active   = true;
    meeting_count++;
    meeting_sort();

    Serial.printf("[MEETING] Added meeting at %02d:%02d (%d min, %d total)\n",
        hour, minute, duration, meeting_count);
    send_state_to_peer();
    return true;
}

void meeting_remove(int index) {
    if (index < 0 || index >= meeting_count) return;

    Serial.printf("[MEETING] Removed meeting at %02d:%02d\n",
        meetings[index].hour, meetings[index].minute);

    for (int i = index; i < meeting_count - 1; i++) {
        meetings[i] = meetings[i + 1];
    }
    meetings[meeting_count - 1].active = false;
    meeting_count--;

    if (browse_index >= meeting_count && meeting_count > 0) {
        browse_index = meeting_count - 1;
    }

    send_state_to_peer();
}

void meeting_update(int index, uint8_t hour, uint8_t minute, uint8_t duration) {
    if (index < 0 || index >= meeting_count) return;

    meetings[index].hour     = hour;
    meetings[index].minute   = minute;
    meetings[index].duration = duration;
    meeting_sort();

    Serial.printf("[MEETING] Updated meeting to %02d:%02d (%d min)\n", hour, minute, duration);
    send_state_to_peer();
}

bool meeting_has_next() {
    return meeting_count > 0;
}

uint8_t meeting_get_next_hour() {
    if (meeting_count == 0) return 0;
    return meetings[0].hour;
}

uint8_t meeting_get_next_minute() {
    if (meeting_count == 0) return 0;
    return meetings[0].minute;
}

UIMode meeting_get_mode() {
    return ui_mode;
}

void meeting_set_mode(UIMode mode) {
    ui_mode = mode;
    if (mode == UIMode::ADD) {
        add_step = 0;          // start with time selection
        duration_slot = 5;     // default 30 min
        edit_slot = 0;         // default: 9:00

        struct tm ti;
        if (getLocalTime(&ti)) {
            int now_minutes = ti.tm_hour * 60 + ti.tm_min;
            int range_start = MEETING_HOUR_MIN * 60;  // 9:00
            int range_end   = MEETING_HOUR_MAX * 60;   // 17:00

            if (now_minutes >= range_start && now_minutes < range_end) {
                // Round up to next 5-min mark
                int next = now_minutes + (MEETING_STEP_MIN - (now_minutes % MEETING_STEP_MIN));
                if (next > range_end) next = range_end;
                edit_slot = (next - range_start) / MEETING_STEP_MIN;
            }
            // else: outside 9-17, keep default 9:00
        }
    }
    if (mode == UIMode::EDIT && browse_index < meeting_count) {
        add_step = 0;  // start with time selection
        edit_slot = time_to_slot(meetings[browse_index].hour,
                                 meetings[browse_index].minute);
        duration_slot = (meetings[browse_index].duration / MEETING_STEP_MIN) - 1;
        if (duration_slot < 0) duration_slot = 0;
        if (duration_slot >= MEETING_DURATION_SLOTS) duration_slot = MEETING_DURATION_SLOTS - 1;
    }
    if (mode == UIMode::PEER_MEETINGS) {
        peer_browse_index = 0;
    }
}

int meeting_get_browse_index() {
    return browse_index;
}

void meeting_set_browse_index(int idx) {
    if (meeting_count == 0) {
        browse_index = 0;
        return;
    }
    browse_index = idx;
    if (browse_index >= meeting_count) browse_index = 0;
    if (browse_index < 0) browse_index = meeting_count - 1;
}

int meeting_get_edit_slot() {
    return edit_slot;
}

void meeting_set_edit_slot(int slot) {
    edit_slot = slot;
}

int meeting_get_peer_browse_index() {
    return peer_browse_index;
}

void meeting_set_peer_browse_index(int idx) {
    peer_browse_index = idx;
}

int meeting_get_add_step() {
    return add_step;
}

void meeting_set_add_step(int step) {
    add_step = step;
}

int meeting_get_duration_slot() {
    return duration_slot;
}

void meeting_set_duration_slot(int slot) {
    duration_slot = slot;
}

void meeting_encoder_changed(int delta) {
    switch (ui_mode) {
        case UIMode::NORMAL:
            if (meeting_count > 0) {
                ui_mode = UIMode::BROWSE;
                browse_index = 0;
                meeting_set_browse_index(browse_index + delta);
            }
            break;

        case UIMode::BROWSE:
            meeting_set_browse_index(browse_index + delta);
            break;

        case UIMode::ADD:
            if (add_step == 0) {
                // Step 0: adjust start time
                edit_slot += delta;
                if (edit_slot >= MEETING_TOTAL_SLOTS) edit_slot = 0;
                if (edit_slot < 0) edit_slot = MEETING_TOTAL_SLOTS - 1;
            } else {
                // Step 1: adjust duration (0-11 → 5-60 min)
                duration_slot += delta;
                if (duration_slot >= MEETING_DURATION_SLOTS) duration_slot = 0;
                if (duration_slot < 0) duration_slot = MEETING_DURATION_SLOTS - 1;
            }
            break;

        case UIMode::EDIT:
            if (add_step == 0) {
                edit_slot += delta;
                if (edit_slot >= MEETING_TOTAL_SLOTS) edit_slot = 0;
                if (edit_slot < 0) edit_slot = MEETING_TOTAL_SLOTS - 1;
            } else {
                duration_slot += delta;
                if (duration_slot >= MEETING_DURATION_SLOTS) duration_slot = 0;
                if (duration_slot < 0) duration_slot = MEETING_DURATION_SLOTS - 1;
            }
            break;

        case UIMode::PEER_MEETINGS: {
            PeerState peer = comms_get_peer_state();
            int cnt = peer.meeting_count;
            if (cnt > 0) {
                peer_browse_index += delta;
                if (peer_browse_index >= cnt) peer_browse_index = 0;
                if (peer_browse_index < 0) peer_browse_index = cnt - 1;
            }
            break;
        }
    }
}
