#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "pong_link.h"
#include "net_config.h"

// SoftAP + UDP server side of the link (screen board). Reply-to-source:
// after each successful recvRaw() the glue reads lastRemoteIp()/lastRemotePort()
// to learn each controller's return address, then replies via sendTo().
class LinkUdpServer : public PongLink {
public:
    bool begin() override;
    void poll(uint32_t nowMs) override;
    // sends to the last datagram's source (fine for a single controller)
    bool sendRaw(const void* buf, size_t len) override;
    int recvRaw(void* buf, size_t maxLen) override;
    bool isUp() const override;

    // concrete-class extras for multi-peer feedback
    bool sendTo(const IPAddress& ip, uint16_t port, const void* buf, size_t len);
    IPAddress lastRemoteIp() const { return lastIp_; }
    uint16_t lastRemotePort() const { return lastPort_; }

private:
    WiFiUDP udp_;
    IPAddress lastIp_;
    uint16_t lastPort_ = 0;
    bool up_ = false;
};
