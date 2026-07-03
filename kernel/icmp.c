#include "kernel.h"

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    uint8_t  data[];
} icmp_header_t;

extern int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t* data, uint32_t len, int iface_idx);

static uint16_t icmp_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i += 2) {
        sum += ((uint16_t)data[i] << 8) | (i + 1 < len ? data[i+1] : 0);
        if (sum & 0xFFFF0000) sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~(sum & 0xFFFF);
}

int icmp_send_echo(uint32_t dst_ip, uint16_t id, uint16_t seq, int iface_idx) {
    uint32_t packet_len = sizeof(icmp_header_t) + 56;
    uint8_t* packet = (uint8_t*)kmalloc(packet_len);
    if (!packet) return -1;
    icmp_header_t* icmp = (icmp_header_t*)packet;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = ((id << 8) & 0xFF00) | ((id >> 8) & 0x00FF);
    icmp->seq = ((seq << 8) & 0xFF00) | ((seq >> 8) & 0x00FF);
    for (uint32_t i = 0; i < 56; i++) icmp->data[i] = i;
    icmp->checksum = icmp_checksum(packet, packet_len);
    int result = ip_send(dst_ip, 1, packet, packet_len, iface_idx);
    kfree(packet);
    return result;
}

static volatile int ping_reply_received = 0;

void icmp_handle_packet(uint8_t* packet, uint32_t len, uint32_t src_ip) {
    (void)src_ip;
    if (len < sizeof(icmp_header_t)) return;
    icmp_header_t* icmp = (icmp_header_t*)packet;
    if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
        ping_reply_received = 1;
        uint16_t id = ((icmp->id << 8) & 0xFF00) | ((icmp->id >> 8) & 0x00FF);
        uint16_t seq = ((icmp->seq << 8) & 0xFF00) | ((icmp->seq >> 8) & 0x00FF);
        printf("[ICMP] Echo reply from %d.%d.%d.%d: id=%d seq=%d\n",
            src_ip&0xFF, (src_ip>>8)&0xFF, (src_ip>>16)&0xFF, (src_ip>>24)&0xFF, id, seq);
    }
}

int icmp_ping(uint32_t dst_ip, int count, int iface_idx) {
    ping_reply_received = 0;
    for (int i = 0; i < count; i++) {
        icmp_send_echo(dst_ip, 1, i + 1, iface_idx);
        for (volatile int d = 0; d < 10000000; d++);
        if (ping_reply_received) return 1;
    }
    return 0;
}
