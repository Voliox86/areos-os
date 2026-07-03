#include "kernel.h"

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17
#define IP_PROTO_TCP  6

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ip_header_t;

extern int eth_send(const uint8_t* dst_mac, uint16_t type, const uint8_t* data, uint32_t len, int iface_idx);
extern int arp_resolve(uint32_t ip, uint8_t* mac, int iface_idx);
extern void udp_handle_packet(uint8_t* packet, uint32_t len, uint32_t src_ip);
extern void icmp_handle_packet(uint8_t* packet, uint32_t len, uint32_t src_ip);
extern void tcp_handle_packet(uint8_t* packet, uint32_t len, uint32_t src_ip);

static uint16_t ip_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i += 2) {
        sum += ((uint16_t)data[i] << 8) | data[i+1];
        if (sum & 0xFFFF0000) { sum = (sum & 0xFFFF) + (sum >> 16); }
    }
    return ~(sum & 0xFFFF);
}

int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t* data, uint32_t len, int iface_idx) {
    if (iface_idx < 0) {
        // Skip loopback ("lo", index 0) when auto-selecting — its netmask/gateway
        // are zero, which would disable subnet/gateway routing for e.g. TCP
        // (send_segment passes -1). Prefer the first real NIC.
        for (int i = 0; i < 8; i++) {
            if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
                iface_idx = i; break;
            }
        }
        if (iface_idx < 0) return -1;
    }

    uint8_t mac[6];
    // Broadcast destinations (limited 255.255.255.255 or the subnet directed
    // broadcast) go to the broadcast MAC directly — ARP-resolving them never
    // gets a reply and would hang the send (this broke DHCP DISCOVER).
    uint32_t nm = net_interfaces[iface_idx].netmask;
    uint32_t my_ip = net_interfaces[iface_idx].ip;
    uint32_t subnet_bcast = nm ? (my_ip | ~nm) : 0xFFFFFFFF;
    if (dst_ip == 0xFFFFFFFF || dst_ip == subnet_bcast) {
        for (int i = 0; i < 6; i++) mac[i] = 0xFF;
    } else {
        // Off-subnet destinations are reached via the default gateway: ARP-resolve
        // the gateway's MAC, not the (remote) destination's. Without this, every
        // internet destination failed ARP and no packet was sent.
        uint32_t next_hop = dst_ip;
        if (nm && (dst_ip & nm) != (my_ip & nm) && net_interfaces[iface_idx].gateway)
            next_hop = net_interfaces[iface_idx].gateway;
        if (!arp_resolve(next_hop, mac, iface_idx)) return -1;
    }

    uint32_t packet_len = sizeof(ip_header_t) + len;
    uint8_t* packet = (uint8_t*)kmalloc(packet_len);
    if (!packet) return -1;

    ip_header_t* ip = (ip_header_t*)packet;
    ip->ver_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_len = ((packet_len << 8) & 0xFF00) | ((packet_len >> 8) & 0x00FF);
    ip->id = 0;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src_ip = net_interfaces[iface_idx].ip;
    ip->dst_ip = dst_ip;

    if (len > 0 && data) memcpy(packet + sizeof(ip_header_t), data, len);

    // ip_checksum sums big-endian 16-bit words and returns the network-order
    // value as a host integer; store it byte-swapped so the header bytes are in
    // network order (a plain assignment stored it LE-reversed, giving a bad
    // checksum that made QEMU/slirp silently drop every IP packet we sent).
    uint16_t ck = ip_checksum(packet, sizeof(ip_header_t));
    ip->checksum = (uint16_t)((ck >> 8) | (ck << 8));

    int result = eth_send(mac, 0x0800, packet, packet_len, iface_idx);
    kfree(packet);
    return result;
}

void ip_handle_packet(uint8_t* packet, uint32_t len) {
    if (len < sizeof(ip_header_t)) return;
    ip_header_t* ip = (ip_header_t*)packet;

    uint32_t dst_ip = ip->dst_ip;
    for (int i = 0; i < 8; i++) {
        if (!net_interfaces[i].name[0]) continue;
        if (net_interfaces[i].ip == dst_ip || dst_ip == 0xFFFFFFFF) {
            uint8_t protocol = ip->protocol;
            uint8_t* payload = packet + sizeof(ip_header_t);
            // Use the IP total-length field, not the received frame length, to
            // size the payload — small frames carry Ethernet padding that would
            // otherwise be counted as extra payload (e.g. a 58-byte SYN-ACK gets
            // padded to 60, adding 2 phantom bytes that broke the TCP ack number).
            uint16_t ip_total = (uint16_t)((ip->total_len >> 8) | (ip->total_len << 8));
            uint32_t ip_len = (ip_total >= sizeof(ip_header_t) && ip_total <= len) ? ip_total : len;
            uint32_t payload_len = ip_len - sizeof(ip_header_t);
            uint32_t src_ip = ip->src_ip;
            if (protocol == IP_PROTO_UDP) udp_handle_packet(payload, payload_len, src_ip);
            else if (protocol == IP_PROTO_ICMP) icmp_handle_packet(payload, payload_len, src_ip);
            else if (protocol == IP_PROTO_TCP) tcp_handle_packet(payload, payload_len, src_ip);
            return;
        }
    }
}
