#include "link_udp_server.h"

bool LinkUdpServer::begin() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    up_ = WiFi.softAP(NET_SSID, NET_PASS, NET_CHANNEL, false, 4);
    WiFi.setSleep(false);
    udp_.begin(NET_UDP_PORT);
    return up_;
}

void LinkUdpServer::poll(uint32_t) {
    // AP is static — nothing to pump
}

bool LinkUdpServer::isUp() const {
    return up_;
}

int LinkUdpServer::recvRaw(void* buf, size_t maxLen) {
    int size = udp_.parsePacket();
    if (size <= 0) return 0;
    lastIp_ = udp_.remoteIP();
    lastPort_ = udp_.remotePort();
    int n = udp_.read(reinterpret_cast<unsigned char*>(buf), maxLen);
    return n > 0 ? n : 0;
}

bool LinkUdpServer::sendTo(const IPAddress& ip, uint16_t port, const void* buf, size_t len) {
    if (!up_ || port == 0) return false;
    if (!udp_.beginPacket(ip, port)) return false;
    udp_.write(reinterpret_cast<const uint8_t*>(buf), len);
    return udp_.endPacket() != 0;
}

bool LinkUdpServer::sendRaw(const void* buf, size_t len) {
    return sendTo(lastIp_, lastPort_, buf, len);
}
