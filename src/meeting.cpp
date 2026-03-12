#include "meeting.h"
#include "comms.h"
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
    uint8_t hours[MAX_MEETINGS], mins[MAX_MEETINGS];
    uint8_t cnt;
    meeting_get_packed(hours, mins, cnt);
    // Send multiple times for reliability (ESP-NOW can drop packets)
    for (int i = 0; i < 3; i++) {
        comms_send_status(current_status, cnt, hours, mins);
        if (i < 2) delay(10);
    }
}

void meeting_init() {
    current_status   = MeetingStatus::FREE;
    meeting_count    = 0;
    ui_mode          = UIMode::NORMAL;
    browse_index     = 0;
    edit_slot        = 0;
    peer_browse_index = 0;
    for (int i = 0; i < MAX_MEETINGS; i++) {
        meetings[i].active = false;
    }
}

void meeting_loop() {
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
    Serial.printf("[MEETING] Status: %s\n",
        current_status == MeetingStatus::FREE ? "FREE" : "IN_MEETING");
    send_state_to_peer();
}

int meeting_get_count() {
    return meeting_count;
}

const MeetingSlot* meeting_get_all() {
    return meetings;
}

void meeting_get_packed(uint8_t* hours, uint8_t* minutes, uint8_t &count) {
    count = (uint8_t)meeting_count;
    for (int i = 0; i < meeting_count; i++) {
        hours[i]   = meetings[i].hour;
        minutes[i] = meetings[i].minute;
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

bool meeting_add(uint8_t hour, uint8_t minute) {
    if (meeting_count >= MAX_MEETINGS) return false;

    for (int i = 0; i < meeting_count; i++) {
        if (meetings[i].hour == hour && meetings[i].minute == minute) {
            return false;
        }
    }

    meetings[meeting_count].hour   = hour;
    meetings[meeting_count].minute = minute;
    meetings[meeting_count].active = true;
    meeting_count++;
    meeting_sort();

    Serial.printf("[MEETING] Added meeting at %02d:%02d (%d total)\n", hour, minute, meeting_count);
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

void meeting_update(int index, uint8_t hour, uint8_t minute) {
    if (index < 0 || index >= meeting_count) return;

    meetings[index].hour   = hour;
    meetings[index].minute = minute;
    meeting_sort();

    Serial.printf("[MEETING] Updated meeting to %02d:%02d\n", hour, minute);
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
        edit_slot = 0; // default: 9:00

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
        edit_slot = time_to_slot(meetings[browse_index].hour,
                                 meetings[browse_index].minute);
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
        case UIMode::EDIT:
            edit_slot += delta;
            if (edit_slot >= MEETING_TOTAL_SLOTS) edit_slot = 0;
            if (edit_slot < 0) edit_slot = MEETING_TOTAL_SLOTS - 1;
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
