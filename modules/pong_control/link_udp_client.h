#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "pong_link.h"
#include "net_config.h"

// STA-side PongLink: async association with a periodic reconnect kick, UDP
// datagrams to the SoftAP server at 192.168.4.1. Never blocks.
class LinkUdpClient : public PongLink {
public:
    bool begin() override;
    void poll(uint32_t nowMs) override;
    bool sendRaw(const void* buf, size_t len) override;
    int  recvRaw(void* buf, size_t maxLen) override;
    bool isUp() const override;

private:
    WiFiUDP udp_;
    uint32_t lastKickMs_ = 0;
    bool udpStarted_ = false;
    bool wasConnected_ = false;
};
