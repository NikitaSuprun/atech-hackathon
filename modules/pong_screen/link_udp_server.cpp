#include "link_udp_server.h"

bool LinkUdpServer::begin() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(NET_AP_IP), IPAddress(NET_AP_IP),
                      IPAddress(NET_AP_NETMASK));
    up_ = WiFi.softAP(NET_SSID, NET_PASS, NET_CHANNEL, false, NET_AP_MAX_CLIENTS);
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
    bool ok = false;
    if (udp_.beginPacket(ip, port)) {
        udp_.write(reinterpret_cast<const uint8_t*>(buf), len);
        ok = udp_.endPacket() != 0;
    }
    // belt & braces: also broadcast — a power-saving STA can drop AP->client
    // unicast, but broadcast is delivered after each DTIM beacon
    if (udp_.beginPacket(IPAddress(NET_AP_BROADCAST), NET_UDP_LOCAL_PORT)) {
        udp_.write(reinterpret_cast<const uint8_t*>(buf), len);
        udp_.endPacket();
    }
    return ok;
}

bool LinkUdpServer::sendRaw(const void* buf, size_t len) {
    return sendTo(lastIp_, lastPort_, buf, len);
}
