#pragma once

#include <cstdint>
#include "config.h"

// Message types
enum class MsgType : uint8_t {
    DISCOVERY = 0x01,
    STATUS    = 0x02,
    HEARTBEAT = 0x03
};

// Packed meeting entry for wire protocol
struct __attribute__((packed)) PackedMeeting {
    uint8_t hour;
    uint8_t minute;
};

// Wire protocol message — includes full meeting list
struct __attribute__((packed)) PeerMessage {
    MsgType  type;
    uint8_t  status;           // MeetingStatus as uint8_t
    uint8_t  meeting_count;    // Number of active meetings (0–MAX_MEETINGS)
    PackedMeeting meetings[MAX_MEETINGS];
    uint32_t timestamp;        // millis() at send time
};

// Peer state as seen by this device
struct PeerMeeting {
    uint8_t hour   = 0;
    uint8_t minute = 0;
};

struct PeerState {
    MeetingStatus status         = MeetingStatus::FREE;
    uint8_t       meeting_count  = 0;
    PeerMeeting   meetings[MAX_MEETINGS];
    bool          connected      = false;
    uint32_t      last_seen_ms   = 0;
};

void comms_init();
void comms_loop();

void comms_send_status(MeetingStatus status, uint8_t meeting_count,
                       const uint8_t* hours, const uint8_t* minutes);
void comms_send_heartbeat(MeetingStatus status, uint8_t meeting_count,
                          const uint8_t* hours, const uint8_t* minutes);

bool        comms_is_paired();
PeerState   comms_get_peer_state();
