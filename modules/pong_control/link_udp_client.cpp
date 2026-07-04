#include "link_udp_client.h"

bool LinkUdpClient::begin() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(NET_SSID, NET_PASS);
    lastKickMs_ = millis();
    return true;
}

void LinkUdpClient::poll(uint32_t nowMs) {
    if (WiFi.status() != WL_CONNECTED) {
        wasConnected_ = false;
        if (nowMs - lastKickMs_ >= STA_REKICK_MS) {
            WiFi.disconnect(false);
            WiFi.begin(NET_SSID, NET_PASS);
            lastKickMs_ = nowMs;
        }
        return;
    }
    if (!wasConnected_) {
        wasConnected_ = true;
        // rebind on every association; WiFiUDP::begin stops a stale socket first
        udp_.begin(NET_UDP_LOCAL_PORT);
        udpStarted_ = true;
    }
}

bool LinkUdpClient::isUp() const {
    return udpStarted_ && WiFi.status() == WL_CONNECTED;
}

bool LinkUdpClient::sendRaw(const void* buf, size_t len) {
    if (!isUp()) return false;
    if (udp_.beginPacket(IPAddress(192, 168, 4, 1), NET_UDP_PORT) != 1) return false;
    udp_.write((const uint8_t*)buf, len);
    return udp_.endPacket() == 1;
}

int LinkUdpClient::recvRaw(void* buf, size_t maxLen) {
    if (!udpStarted_) return 0;
    if (udp_.parsePacket() <= 0) return 0;
    int n = udp_.read((unsigned char*)buf, maxLen);
    return n > 0 ? n : 0;
}
