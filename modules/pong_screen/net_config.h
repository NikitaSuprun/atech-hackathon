#pragma once
#include <stdint.h>

// CANONICAL network constants (symlinked into modules/pong_control/ — edit only here).

#define NET_SSID "nikita-pong-x7"
#define NET_PASS "suprun4242"                     // WPA2, >= 8 chars

constexpr uint8_t  NET_CHANNEL         = 6;       // set least-busy of 1/6/11 at the venue
constexpr uint16_t NET_UDP_PORT        = 47420;   // server (screen board) listen port
constexpr uint16_t NET_UDP_LOCAL_PORT  = 47421;   // controller bind; feedback arrives here
// Server IP is the ESP32 SoftAP default 192.168.4.1, pinned via WiFi.softAPConfig.

constexpr uint32_t INPUT_PERIOD_MS     = 20;      // 50 Hz controller -> screen
constexpr uint32_t FEEDBACK_PERIOD_MS  = 50;      // 20 Hz screen -> controller
constexpr uint32_t INPUT_TIMEOUT_MS    = 1000;    // server: silence -> GS_LINK_WAIT
constexpr uint32_t FEEDBACK_TIMEOUT_MS = 2500;    // controller: silence -> link-lost cues
constexpr uint32_t STA_REKICK_MS       = 5000;    // STA reconnect kick interval
constexpr uint32_t READY_HOLD_MS       = 500;     // both-hold duration (server-clocked)
