#pragma once

#include "config.h"
#include <cstdint>

struct MeetingSlot {
    uint8_t hour     = 0;
    uint8_t minute   = 0;
    uint8_t duration = 30;  // duration in minutes (5-60)
    bool    active   = false;
};

void meeting_init();
void meeting_loop();

// Status
MeetingStatus meeting_get_status();
void          meeting_toggle_status();

// Meeting list management
int              meeting_get_count();
const MeetingSlot* meeting_get_all();
bool             meeting_add(uint8_t hour, uint8_t minute, uint8_t duration);
void             meeting_remove(int index);
void             meeting_update(int index, uint8_t hour, uint8_t minute, uint8_t duration);
void             meeting_sort();

// Get packed arrays for comms
void meeting_get_packed(uint8_t* hours, uint8_t* minutes, uint8_t* durations, uint8_t &count);

// Next upcoming meeting (earliest active)
bool    meeting_has_next();
uint8_t meeting_get_next_hour();
uint8_t meeting_get_next_minute();

// UI mode management
UIMode meeting_get_mode();
void   meeting_set_mode(UIMode mode);
int    meeting_get_browse_index();
void   meeting_set_browse_index(int idx);
int    meeting_get_edit_slot();
void   meeting_set_edit_slot(int slot);
int    meeting_get_peer_browse_index();
void   meeting_set_peer_browse_index(int idx);

// ADD mode sub-step (0=time, 1=duration)
int  meeting_get_add_step();
void meeting_set_add_step(int step);
int  meeting_get_duration_slot();
void meeting_set_duration_slot(int slot);

// Encoder handling (behavior depends on mode)
void meeting_encoder_changed(int delta);

// Convert slot index <-> time
void slot_to_time(int slot, uint8_t &hour, uint8_t &minute);
int  time_to_slot(uint8_t hour, uint8_t minute);
