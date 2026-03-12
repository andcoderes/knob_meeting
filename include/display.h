#pragma once

#include "config.h"
#include "comms.h"
#include "meeting.h"

void display_init();
void display_loop();

void display_show_searching();
void display_update(MeetingStatus my_status, UIMode mode,
                    const MeetingSlot* meetings, int meeting_count,
                    int browse_index, int edit_slot,
                    int peer_browse_index,
                    const PeerState& peer);
void display_force_redraw();
