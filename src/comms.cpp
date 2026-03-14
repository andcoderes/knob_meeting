#include "comms.h"
#include <esp_now.h>
#include <WiFi.h>
#include <cstring>

static uint8_t broadcast_addr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t peer_mac[6]      = {0};
static bool    paired           = false;
static PeerState peer_state;

static uint32_t last_heartbeat_ms  = 0;
static uint32_t last_discovery_ms  = 0;

// Forward declarations
static void on_data_recv(const uint8_t *sender_mac, const uint8_t *data, int len);
static void on_data_sent(const uint8_t *mac, esp_now_send_status_t status);
static void send_message(const uint8_t *dest, const PeerMessage &msg);
static void add_espnow_peer(const uint8_t *mac);

static void fill_message(PeerMessage &msg, MsgType type, MeetingStatus status,
                         uint8_t count, const uint8_t* hours, const uint8_t* minutes,
                         const uint8_t* durations) {
    msg.type = type;
    msg.status = static_cast<uint8_t>(status);
    msg.meeting_count = count;
    memset(msg.meetings, 0, sizeof(msg.meetings));
    for (int i = 0; i < count && i < MAX_MEETINGS; i++) {
        msg.meetings[i].hour = hours[i];
        msg.meetings[i].minute = minutes[i];
        msg.meetings[i].duration = durations ? durations[i] : 30;
    }
    msg.timestamp = millis();
}

void comms_init() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("[COMMS] ESP-NOW init failed");
        return;
    }

    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_data_sent);

    add_espnow_peer(broadcast_addr);

    Serial.println("[COMMS] ESP-NOW initialized");
    Serial.print("[COMMS] MAC: ");
    Serial.println(WiFi.macAddress());
}

void comms_loop() {
    uint32_t now = millis();

    // Discovery: broadcast until paired
    if (!paired && (now - last_discovery_ms >= DISCOVERY_INTERVAL_MS)) {
        last_discovery_ms = now;
        PeerMessage msg;
        fill_message(msg, MsgType::DISCOVERY, MeetingStatus::FREE, 0, nullptr, nullptr, nullptr);
        send_message(broadcast_addr, msg);
    }

    // Check peer timeout
    if (paired && peer_state.connected) {
        if (now - peer_state.last_seen_ms > PEER_TIMEOUT_MS) {
            peer_state.connected = false;
            Serial.println("[COMMS] Peer timed out");
        }
    }

    // If peer disconnected, try to rediscover
    if (paired && !peer_state.connected) {
        if (now - last_discovery_ms >= DISCOVERY_INTERVAL_MS) {
            last_discovery_ms = now;
            PeerMessage msg;
            fill_message(msg, MsgType::DISCOVERY, MeetingStatus::FREE, 0, nullptr, nullptr, nullptr);
            send_message(broadcast_addr, msg);
        }
    }
}

void comms_send_status(MeetingStatus status, uint8_t count,
                       const uint8_t* hours, const uint8_t* minutes,
                       const uint8_t* durations) {
    if (!paired) return;
    PeerMessage msg;
    fill_message(msg, MsgType::STATUS, status, count, hours, minutes, durations);
    send_message(peer_mac, msg);
}

void comms_send_heartbeat(MeetingStatus status, uint8_t count,
                          const uint8_t* hours, const uint8_t* minutes,
                          const uint8_t* durations) {
    uint32_t now = millis();
    if (now - last_heartbeat_ms < HEARTBEAT_MS) return;
    last_heartbeat_ms = now;

    if (!paired) return;
    PeerMessage msg;
    fill_message(msg, MsgType::HEARTBEAT, status, count, hours, minutes, durations);
    send_message(peer_mac, msg);
}

bool comms_is_paired() {
    return paired && peer_state.connected;
}

PeerState comms_get_peer_state() {
    return peer_state;
}

// --- Internal ---

static void add_espnow_peer(const uint8_t *mac) {
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = 0;
    peer_info.encrypt = false;

    if (!esp_now_is_peer_exist(mac)) {
        esp_now_add_peer(&peer_info);
    }
}

static void send_message(const uint8_t *dest, const PeerMessage &msg) {
    esp_now_send(dest, (const uint8_t *)&msg, sizeof(msg));
}

static void on_data_sent(const uint8_t *mac, esp_now_send_status_t status) {
}

static void update_peer_from_msg(const PeerMessage &msg) {
    peer_state.status = static_cast<MeetingStatus>(msg.status);
    peer_state.meeting_count = msg.meeting_count;
    if (peer_state.meeting_count > MAX_MEETINGS) peer_state.meeting_count = MAX_MEETINGS;
    for (int i = 0; i < peer_state.meeting_count; i++) {
        peer_state.meetings[i].hour     = msg.meetings[i].hour;
        peer_state.meetings[i].minute   = msg.meetings[i].minute;
        peer_state.meetings[i].duration = msg.meetings[i].duration;
    }
    peer_state.connected = true;
    peer_state.last_seen_ms = millis();
}

static void on_data_recv(const uint8_t *sender_mac, const uint8_t *data, int len) {
    if (len != sizeof(PeerMessage)) return;

    PeerMessage msg;
    memcpy(&msg, data, sizeof(msg));

    // Ignore our own broadcasts
    uint8_t my_mac[6];
    WiFi.macAddress(my_mac);
    if (memcmp(sender_mac, my_mac, 6) == 0) return;

    switch (msg.type) {
        case MsgType::DISCOVERY: {
            if (!paired || memcmp(peer_mac, sender_mac, 6) == 0) {
                if (!paired) {
                    memcpy(peer_mac, sender_mac, 6);
                    add_espnow_peer(peer_mac);
                    paired = true;
                    Serial.printf("[COMMS] Paired with %02X:%02X:%02X:%02X:%02X:%02X\n",
                        peer_mac[0], peer_mac[1], peer_mac[2],
                        peer_mac[3], peer_mac[4], peer_mac[5]);
                }
                peer_state.connected = true;
                peer_state.last_seen_ms = millis();

                // Reply so the other side pairs too
                PeerMessage reply;
                fill_message(reply, MsgType::DISCOVERY, MeetingStatus::FREE, 0, nullptr, nullptr, nullptr);
                send_message(peer_mac, reply);
            }
            break;
        }
        case MsgType::STATUS:
        case MsgType::HEARTBEAT: {
            if (!paired) {
                memcpy(peer_mac, sender_mac, 6);
                add_espnow_peer(peer_mac);
                paired = true;
                Serial.printf("[COMMS] Auto-paired with %02X:%02X:%02X:%02X:%02X:%02X\n",
                    peer_mac[0], peer_mac[1], peer_mac[2],
                    peer_mac[3], peer_mac[4], peer_mac[5]);
            }
            if (memcmp(peer_mac, sender_mac, 6) == 0) {
                update_peer_from_msg(msg);
            }
            break;
        }
    }
}
