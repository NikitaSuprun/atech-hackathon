#pragma once
#include <stdint.h>
#include <stddef.h>

// CANONICAL transport seam (symlinked into modules/pong_control/ — edit only here).
// Implementations: link_udp_server (SoftAP + WiFiUDP, reply-to-source) on the screen
// board, link_udp_client (STA + WiFiUDP) on the controller. If venue WiFi proves
// hostile, ESP-NOW implementations of this same interface are a two-file swap —
// the 24/26-byte payloads fit ESP-NOW's 250-byte limit unchanged.

struct PongLink {
    virtual bool begin() = 0;                               // non-blocking init
    virtual void poll(uint32_t nowMs) = 0;                  // pump reconnect state machine
    virtual bool sendRaw(const void* buf, size_t len) = 0;  // to server / learned peer
    virtual int  recvRaw(void* buf, size_t maxLen) = 0;     // one datagram; 0 = none
    virtual bool isUp() const = 0;                          // AP started / STA associated
    virtual ~PongLink() {}
};
