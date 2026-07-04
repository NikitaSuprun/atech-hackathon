#pragma once
#include <stddef.h>
#include <stdint.h>
#include "link_frame.h"
#include "pong_link.h"

// ESP-NOW transport seam — STUB (interface only). ESP-NOW is connectionless and
// already datagram-framed, so packets ride RAW here: NO COBS, NO 0x00
// delimiter (that framing is only for the byte-stream serial transports). A
// full 229-B console frame fits ESP-NOW's 250-B payload in a single send, so
// there is nothing to fragment.
//
// Filling this in is the "two-file swap" the game link already anticipated:
// implement begin()/sendRaw()/recvRaw() against esp_now_init /
// esp_now_send / an esp_now_register_recv_cb ring buffer, guarded by
// CONSOLE_LINK_ESPNOW so this header stays dependency-free until then.

#ifndef CONSOLE_LINK_ESPNOW

// Dependency-free placeholder so the seam compiles everywhere. isUp() is false,
// so glue that probes the link will fall back to a serial transport.
class LinkEspNow : public PongLink {
public:
    bool begin() override { return false; }              // TODO: esp_now_init + add peer
    void poll(uint32_t) override {}                      // TODO: age the peer / retry
    bool sendRaw(const void*, size_t) override { return false; }  // TODO: esp_now_send
    int  recvRaw(void*, size_t) override { return 0; }   // TODO: drain the rx ring
    bool isUp() const override { return false; }
};

// A full 108-px frame is the largest real packet and fits one ESP-NOW send, so
// there is nothing to fragment. (The serial MTU is larger only for buffer
// headroom; an ESP-NOW build would cap outsized control messages at 250.)
static_assert(console::FRAME_MAX_PACKET <= 250, "frame packet must fit one ESP-NOW datagram");

#else
#error "LinkEspNow real implementation not provided yet (stub only)"
#endif
