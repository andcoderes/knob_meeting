#pragma once

#include <cstdint>

// --- Pin Definitions (M5Stack Dial) ---
constexpr int PIN_ENCODER_A  = 40;
constexpr int PIN_ENCODER_B  = 41;
constexpr int PIN_BUZZER     = 3;
constexpr int PIN_BUTTON     = 42;
constexpr int PIN_SDA        = 11;
constexpr int PIN_SCL        = 12;
constexpr int PIN_BACKLIGHT  = 9;

// --- Display ---
constexpr int SCREEN_WIDTH   = 240;
constexpr int SCREEN_HEIGHT  = 240;
constexpr int SCREEN_CENTER_X = 120;
constexpr int SCREEN_CENTER_Y = 120;
constexpr int SCREEN_RADIUS  = 120;

// --- Colors (RGB565) ---
constexpr uint16_t COLOR_GREEN       = 0x0664;  // #00C853
constexpr uint16_t COLOR_RED         = 0xF8A8;  // #FF1744
constexpr uint16_t COLOR_GRAY        = 0x7BEF;  // #808080
constexpr uint16_t COLOR_DARK_GRAY   = 0x3186;  // #303030
constexpr uint16_t COLOR_WHITE       = 0xFFFF;
constexpr uint16_t COLOR_BLACK       = 0x0000;
constexpr uint16_t COLOR_BG          = 0x10A2;  // Dark background
constexpr uint16_t COLOR_YELLOW      = 0xFFE0;  // For edit mode highlights
constexpr uint16_t COLOR_BLUE        = 0x34DF;  // For add mode

// --- Meeting Time Slots ---
constexpr int MEETING_HOUR_MIN   = 9;   // 9:00 AM
constexpr int MEETING_HOUR_MAX   = 17;  // 5:00 PM
constexpr int MEETING_STEP_MIN   = 5;   // 5-minute increments
constexpr int MEETING_TOTAL_SLOTS = 97; // (17-9)*12 + 1
constexpr int MAX_MEETINGS       = 8;   // Max meetings per day

// --- WiFi / NTP (for clock sync on boot) ---
#include "secrets.h"       // WIFI_SSID, WIFI_PASSWORD (not tracked in git)
#define NTP_SERVER    "pool.ntp.org"
#define GMT_OFFSET_SEC  -28800   // Pacific Standard Time (UTC-8)
#define DST_OFFSET_SEC  3600     // DST +1 hour (PDT = UTC-7)
constexpr uint32_t NTP_TIMEOUT_MS = 8000;

// --- ESP-NOW ---
constexpr int ESPNOW_CHANNEL       = 1;
constexpr uint32_t HEARTBEAT_MS    = 500;
constexpr uint32_t PEER_TIMEOUT_MS = 10000;

// --- Timing ---
constexpr uint32_t SCREEN_REFRESH_MS   = 100;
constexpr uint32_t DISCOVERY_INTERVAL_MS = 1000;
constexpr uint32_t BUZZER_DURATION_MS  = 200;
constexpr int BUZZER_FREQ              = 2000;
constexpr uint32_t LONG_PRESS_MS       = 800;

// --- Meeting Status ---
enum class MeetingStatus : uint8_t {
    FREE       = 0,
    IN_MEETING = 1
};

// --- UI Mode ---
enum class UIMode : uint8_t {
    NORMAL        = 0,  // Default view: status + next meeting
    BROWSE        = 1,  // Scrolling through my meetings list
    ADD           = 2,  // Adding a new meeting (encoder sets time)
    EDIT          = 3,  // Editing existing meeting time
    PEER_MEETINGS = 4   // Viewing peer's meeting list
};
