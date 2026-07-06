#include "kernel.h"

#define UDP_MAX_PAYLOAD 2048

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

extern int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t* data, uint32_t len, int iface_idx);

int udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port, const uint8_t* data, uint32_t len, int iface_idx) {
    uint32_t packet_len = sizeof(udp_header_t) + len;
    uint8_t* packet = (uint8_t*)kmalloc(packet_len);
    if (!packet) return -1;

    udp_header_t* udp = (udp_header_t*)packet;
    udp->src_port = ((src_port << 8) & 0xFF00) | ((src_port >> 8) & 0x00FF);
    udp->dst_port = ((dst_port << 8) & 0xFF00) | ((dst_port >> 8) & 0x00FF);
    udp->length = ((packet_len << 8) & 0xFF00) | ((packet_len >> 8) & 0x00FF);
    udp->checksum = 0;

    if (len > 0 && data) memcpy(packet + sizeof(udp_header_t), data, len);

    int result = ip_send(dst_ip, 17, packet, packet_len, iface_idx);
    kfree(packet);
    return result;
}

typedef struct {
    uint16_t port;
    void (*handler)(uint8_t* data, uint32_t len, uint32_t src_ip, uint16_t src_port);
    int active;
} udp_listener_t;

static udp_listener_t udp_listeners[16];

void udp_register_listener(uint16_t port, void (*handler)(uint8_t*, uint32_t, uint32_t, uint16_t)) {
    // A NULL handler unregisters the listener(s) on `port`. dns.c relies on this
    // to tear down its per-query listener ("unregister on timeout"). Without the
    // deactivation below, that call fell through to the "first free slot" logic
    // and merely consumed another slot with a NULL handler, so every DNS lookup
    // leaked ~2 of the 16 slots and name resolution silently died after a handful.
    if (!handler) {
        for (int i = 0; i < 16; i++) {
            if (udp_listeners[i].active && udp_listeners[i].port == port) {
                udp_listeners[i].active = 0;
                udp_listeners[i].handler = 0;
            }
        }
        return;
    }
    // Reuse an existing slot for the same port so a re-registration (e.g. DHCP
    // renewing on DHCP_CLIENT_PORT) updates in place instead of leaking a slot.
    for (int i = 0; i < 16; i++) {
        if (udp_listeners[i].active && udp_listeners[i].port == port) {
            udp_listeners[i].handler = handler;
            return;
        }
    }
    for (int i = 0; i < 16; i++) {
        if (!udp_listeners[i].active) {
            udp_listeners[i].port = port;
            udp_listeners[i].handler = handler;
            udp_listeners[i].active = 1;
            return;
        }
    }
}

void udp_handle_packet(uint8_t* packet, uint32_t len, uint32_t src_ip) {
    if (len < sizeof(udp_header_t)) return;
    udp_header_t* udp = (udp_header_t*)packet;
    uint16_t dst_port = ((udp->dst_port << 8) & 0xFF00) | ((udp->dst_port >> 8) & 0x00FF);
    uint16_t src_port = ((udp->src_port << 8) & 0xFF00) | ((udp->src_port >> 8) & 0x00FF);
    uint8_t* payload = packet + sizeof(udp_header_t);
    uint32_t payload_len = len - sizeof(udp_header_t);
    for (int i = 0; i < 16; i++) {
        if (udp_listeners[i].active && udp_listeners[i].port == dst_port) {
            udp_listeners[i].handler(payload, payload_len, src_ip, src_port);
            return;
        }
    }
}
