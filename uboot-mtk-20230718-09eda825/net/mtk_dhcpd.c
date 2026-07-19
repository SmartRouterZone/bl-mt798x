// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * Minimal DHCPv4 server for MediaTek bootmenu and web failsafe.
 */

#include <common.h>
#include <cyclic.h>
#include <net.h>

#include <net/mtk_dhcpd.h>

#define DHCPD_SERVER_PORT		67
#define DHCPD_CLIENT_PORT		68
#define DHCPD_MIN_BOOTP_LEN		300
#define DHCPD_MAX_CLIENTS		8
#define DHCPD_LEASE_TIME		3600

#define BOOTREQUEST			1
#define BOOTREPLY			2
#define HTYPE_ETHER			1
#define HLEN_ETHER			6

#define DHCPDISCOVER			1
#define DHCPOFFER			2
#define DHCPREQUEST			3
#define DHCPACK				5
#define DHCPNAK				6

#define DHCP_OPTION_PAD			0
#define DHCP_OPTION_SUBNET_MASK		1
#define DHCP_OPTION_ROUTER		3
#define DHCP_OPTION_DNS_SERVER		6
#define DHCP_OPTION_REQ_IPADDR		50
#define DHCP_OPTION_LEASE_TIME		51
#define DHCP_OPTION_MSG_TYPE		53
#define DHCP_OPTION_SERVER_ID		54
#define DHCP_OPTION_MESSAGE		56
#define DHCP_OPTION_END			255

#define DHCP_FLAG_BROADCAST		0x8000

struct dhcpd_pkt {
	u8 op;
	u8 htype;
	u8 hlen;
	u8 hops;
	u32 xid;
	u16 secs;
	u16 flags;
	u32 ciaddr;
	u32 yiaddr;
	u32 siaddr;
	u32 giaddr;
	u8 chaddr[16];
	u8 sname[64];
	u8 file[128];
	u8 vend[312];
} __packed;

struct dhcpd_lease {
	bool used;
	u8 mac[HLEN_ETHER];
	struct in_addr ip;
};

static const u8 dhcp_magic_cookie[] = { 99, 130, 83, 99 };
static struct dhcpd_lease leases[DHCPD_MAX_CLIENTS];
static rxhand_f *prev_udp_handler;
static struct cyclic_info *dhcpd_cyclic;
static bool dhcpd_running;
static bool dhcpd_started_eth;

static struct in_addr dhcpd_get_server_ip(void)
{
	return string_to_ip(CONFIG_IPADDR);
}

static struct in_addr dhcpd_get_netmask(void)
{
	return string_to_ip(CONFIG_NETMASK);
}

static struct in_addr dhcpd_get_gateway(void)
{
	if (net_gateway.s_addr)
		return net_gateway;

	return dhcpd_get_server_ip();
}

static struct in_addr dhcpd_get_dns(void)
{
	if (net_dns_server.s_addr)
		return net_dns_server;

	return dhcpd_get_server_ip();
}

static bool dhcpd_get_pool_range(u32 *start, u32 *end)
{
	struct in_addr server_ip = dhcpd_get_server_ip();
	struct in_addr netmask = dhcpd_get_netmask();
	u32 ip = ntohl(server_ip.s_addr);
	u32 mask = ntohl(netmask.s_addr);
	u32 network = ip & mask;
	u32 broadcast = network | ~mask;
	u32 host = CONFIG_MTK_DHCPD_POOL_START_HOST & ~mask;
	u32 first;
	u32 last;
	u32 pool_start;
	u32 pool_size = CONFIG_MTK_DHCPD_POOL_SIZE;

	if (broadcast <= network + 1)
		return false;

	first = network + 1;
	last = broadcast - 1;
	pool_start = network | host;
	if (pool_start < first || pool_start > last)
		pool_start = first;

	if (!pool_size)
		pool_size = 1;

	*start = pool_start;
	if (pool_size - 1 > last - pool_start)
		*end = last;
	else
		*end = pool_start + pool_size - 1;

	return true;
}

static struct dhcpd_lease *dhcpd_find_lease(const u8 *mac)
{
	int i;

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (leases[i].used && !memcmp(leases[i].mac, mac, HLEN_ETHER))
			return &leases[i];
	}

	return NULL;
}

static struct dhcpd_lease *dhcpd_find_free_lease(void)
{
	int i;

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (!leases[i].used)
			return &leases[i];
	}

	return NULL;
}

static bool dhcpd_ip_in_pool(u32 ip)
{
	u32 start;
	u32 end;

	if (!dhcpd_get_pool_range(&start, &end))
		return false;

	return ip >= start && ip <= end;
}

static struct dhcpd_lease *dhcpd_find_ip(u32 ip)
{
	int i;

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (leases[i].used && ntohl(leases[i].ip.s_addr) == ip)
			return &leases[i];
	}

	return NULL;
}

static u32 dhcpd_mac_hash(const u8 *mac)
{
	u32 hash = 2166136261u;
	int i;

	for (i = 0; i < HLEN_ETHER; i++) {
		hash ^= mac[i];
		hash *= 16777619u;
	}

	return hash;
}

static bool dhcpd_reserve_ip(const u8 *mac, struct in_addr ip)
{
	struct dhcpd_lease *lease = dhcpd_find_lease(mac);
	struct dhcpd_lease *owner = dhcpd_find_ip(ntohl(ip.s_addr));

	if (owner && owner != lease)
		return false;

	if (!lease)
		lease = dhcpd_find_free_lease();
	if (!lease)
		return false;

	lease->used = true;
	memcpy(lease->mac, mac, HLEN_ETHER);
	lease->ip = ip;

	return true;
}

static struct in_addr dhcpd_alloc_ip(const u8 *mac)
{
	struct dhcpd_lease *lease = dhcpd_find_lease(mac);
	struct in_addr server_ip = dhcpd_get_server_ip();
	struct in_addr result = { 0 };
	u32 start;
	u32 end;
	u32 pool_size;
	u32 offset;
	u32 i;

	if (lease && dhcpd_ip_in_pool(ntohl(lease->ip.s_addr)))
		return lease->ip;

	if (!lease && !dhcpd_find_free_lease())
		return result;

	if (!dhcpd_get_pool_range(&start, &end))
		return result;

	pool_size = end - start + 1;
	offset = dhcpd_mac_hash(mac) % pool_size;

	for (i = 0; i < pool_size; i++) {
		u32 candidate = start + ((offset + i) % pool_size);

		if (candidate == ntohl(server_ip.s_addr) || dhcpd_find_ip(candidate))
			continue;

		result.s_addr = htonl(candidate);
		if (dhcpd_reserve_ip(mac, result))
			return result;
		break;
	}

	result.s_addr = 0;
	return result;
}

static const u8 *dhcpd_find_option(const struct dhcpd_pkt *pkt,
				   unsigned int len, u8 wanted, u8 *found_len)
{
	unsigned int fixed = offsetof(struct dhcpd_pkt, vend);
	const u8 *option;
	unsigned int remaining;

	if (len < fixed + sizeof(dhcp_magic_cookie))
		return NULL;

	option = pkt->vend;
	remaining = len - fixed;
	if (memcmp(option, dhcp_magic_cookie, sizeof(dhcp_magic_cookie)))
		return NULL;

	option += sizeof(dhcp_magic_cookie);
	remaining -= sizeof(dhcp_magic_cookie);

	while (remaining) {
		u8 code = *option++;
		u8 option_len;

		remaining--;
		if (code == DHCP_OPTION_PAD)
			continue;
		if (code == DHCP_OPTION_END)
			break;
		if (!remaining)
			break;

		option_len = *option++;
		remaining--;
		if (option_len > remaining)
			break;
		if (code == wanted) {
			*found_len = option_len;
			return option;
		}

		option += option_len;
		remaining -= option_len;
	}

	return NULL;
}

static bool dhcpd_get_option_u8(const struct dhcpd_pkt *pkt, unsigned int len,
				u8 code, u8 *value)
{
	u8 option_len;
	const u8 *option = dhcpd_find_option(pkt, len, code, &option_len);

	if (!option || option_len != 1)
		return false;

	*value = option[0];
	return true;
}

static bool dhcpd_get_option_ip(const struct dhcpd_pkt *pkt, unsigned int len,
				u8 code, struct in_addr *value)
{
	u8 option_len;
	const u8 *option = dhcpd_find_option(pkt, len, code, &option_len);

	if (!option || option_len != sizeof(value->s_addr))
		return false;

	memcpy(&value->s_addr, option, sizeof(value->s_addr));
	return true;
}

static bool dhcpd_same_subnet(struct in_addr a, struct in_addr b,
			      struct in_addr netmask)
{
	return (a.s_addr & netmask.s_addr) == (b.s_addr & netmask.s_addr);
}

static u8 *dhcpd_add_u8(u8 *option, u8 code, u8 value)
{
	*option++ = code;
	*option++ = 1;
	*option++ = value;

	return option;
}

static u8 *dhcpd_add_u32(u8 *option, u8 code, __be32 value)
{
	*option++ = code;
	*option++ = sizeof(value);
	memcpy(option, &value, sizeof(value));

	return option + sizeof(value);
}

static u8 *dhcpd_add_ip(u8 *option, u8 code, struct in_addr value)
{
	return dhcpd_add_u32(option, code, value.s_addr);
}

static void dhcpd_send_reply(const struct dhcpd_pkt *request, u8 message_type,
			     struct in_addr client_ip, const char *nak_message)
{
	struct in_addr server_ip = dhcpd_get_server_ip();
	struct in_addr netmask = dhcpd_get_netmask();
	struct in_addr broadcast = { .s_addr = 0xffffffff };
	struct dhcpd_pkt *reply;
	uchar *packet = net_tx_packet;
	uchar *payload;
	u8 *option;
	int eth_header_size;
	int payload_len;

	eth_header_size = net_set_ether(packet, net_bcast_ethaddr, PROT_IP);
	payload = packet + eth_header_size + IP_UDP_HDR_SIZE;
	reply = (struct dhcpd_pkt *)payload;
	memset(reply, 0, sizeof(*reply));

	reply->op = BOOTREPLY;
	reply->htype = HTYPE_ETHER;
	reply->hlen = HLEN_ETHER;
	reply->xid = request->xid;
	reply->secs = request->secs;
	reply->flags = htons(DHCP_FLAG_BROADCAST);
	reply->yiaddr = client_ip.s_addr;
	reply->siaddr = server_ip.s_addr;
	memcpy(reply->chaddr, request->chaddr, sizeof(reply->chaddr));

	option = reply->vend;
	memcpy(option, dhcp_magic_cookie, sizeof(dhcp_magic_cookie));
	option += sizeof(dhcp_magic_cookie);
	option = dhcpd_add_u8(option, DHCP_OPTION_MSG_TYPE, message_type);
	option = dhcpd_add_ip(option, DHCP_OPTION_SERVER_ID, server_ip);

	if (message_type != DHCPNAK) {
		option = dhcpd_add_ip(option, DHCP_OPTION_SUBNET_MASK, netmask);
		option = dhcpd_add_ip(option, DHCP_OPTION_ROUTER,
				      dhcpd_get_gateway());
		option = dhcpd_add_ip(option, DHCP_OPTION_DNS_SERVER,
				      dhcpd_get_dns());
		option = dhcpd_add_u32(option, DHCP_OPTION_LEASE_TIME,
				       htonl(DHCPD_LEASE_TIME));
	} else if (nak_message) {
		size_t message_len = min(strlen(nak_message), (size_t)240);

		*option++ = DHCP_OPTION_MESSAGE;
		*option++ = message_len;
		memcpy(option, nak_message, message_len);
		option += message_len;
	}

	*option++ = DHCP_OPTION_END;
	payload_len = option - payload;
	if (payload_len < DHCPD_MIN_BOOTP_LEN)
		payload_len = DHCPD_MIN_BOOTP_LEN;

	net_set_udp_header(packet + eth_header_size, broadcast,
			   DHCPD_CLIENT_PORT, DHCPD_SERVER_PORT, payload_len);
	net_send_packet(packet, eth_header_size + IP_UDP_HDR_SIZE + payload_len);
}

static void dhcpd_handle_packet(uchar *packet, unsigned int dest_port,
				struct in_addr src_ip, unsigned int src_port,
				unsigned int len)
{
	const struct dhcpd_pkt *request = (const struct dhcpd_pkt *)packet;
	struct in_addr server_ip = dhcpd_get_server_ip();
	struct in_addr netmask = dhcpd_get_netmask();
	struct in_addr requested_ip;
	struct in_addr selected_server;
	struct in_addr client_ip;
	struct in_addr no_ip = { 0 };
	u8 message_type;

	(void)src_ip;

	if (!dhcpd_running || dest_port != DHCPD_SERVER_PORT ||
	    src_port != DHCPD_CLIENT_PORT)
		return;
	if (len < offsetof(struct dhcpd_pkt, vend) + sizeof(dhcp_magic_cookie))
		return;
	if (request->op != BOOTREQUEST || request->htype != HTYPE_ETHER ||
	    request->hlen != HLEN_ETHER)
		return;
	if (!dhcpd_get_option_u8(request, len, DHCP_OPTION_MSG_TYPE,
				 &message_type))
		return;

	debug_cond(DEBUG_DEV_PKT, "dhcpd: message %u from %pM\n",
		   message_type, request->chaddr);

	switch (message_type) {
	case DHCPDISCOVER:
		client_ip = dhcpd_alloc_ip(request->chaddr);
		if (client_ip.s_addr)
			dhcpd_send_reply(request, DHCPOFFER, client_ip, NULL);
		break;
	case DHCPREQUEST:
		if (dhcpd_get_option_ip(request, len, DHCP_OPTION_SERVER_ID,
					&selected_server) &&
		    selected_server.s_addr != server_ip.s_addr)
			return;

		if (!dhcpd_get_option_ip(request, len, DHCP_OPTION_REQ_IPADDR,
					 &requested_ip))
			requested_ip.s_addr = request->ciaddr;

		if (!requested_ip.s_addr) {
			client_ip = dhcpd_alloc_ip(request->chaddr);
			if (client_ip.s_addr)
				dhcpd_send_reply(request, DHCPACK, client_ip, NULL);
			else
				dhcpd_send_reply(request, DHCPNAK, no_ip,
						 "pool exhausted");
			break;
		}

		if (!dhcpd_same_subnet(requested_ip, server_ip, netmask)) {
			dhcpd_send_reply(request, DHCPNAK, no_ip, "bad subnet");
			break;
		}
		if (!dhcpd_ip_in_pool(ntohl(requested_ip.s_addr))) {
			dhcpd_send_reply(request, DHCPNAK, no_ip, "outside pool");
			break;
		}
		if (!dhcpd_reserve_ip(request->chaddr, requested_ip)) {
			dhcpd_send_reply(request, DHCPNAK, no_ip, "address in use");
			break;
		}

		dhcpd_send_reply(request, DHCPACK, requested_ip, NULL);
		break;
	default:
		break;
	}
}

static void dhcpd_udp_handler(uchar *packet, unsigned int dest_port,
			      struct in_addr src_ip, unsigned int src_port,
			      unsigned int len)
{
	dhcpd_handle_packet(packet, dest_port, src_ip, src_port, len);

	if (prev_udp_handler)
		prev_udp_handler(packet, dest_port, src_ip, src_port, len);
}

static int dhcpd_prepare_network(void)
{
	struct udevice *eth;
	int ret;

	ret = net_init();
	if (ret)
		return ret;

	eth = eth_get_dev();
	if (!eth)
		return -ENODEV;

	if (!eth_is_active(eth)) {
		eth_set_current();
		ret = eth_init();
		if (ret)
			return ret;
		dhcpd_started_eth = true;
	}

	net_ip = dhcpd_get_server_ip();
	net_netmask = dhcpd_get_netmask();
	if (!net_gateway.s_addr)
		net_gateway = net_ip;
	if (!net_dns_server.s_addr)
		net_dns_server = net_ip;

	return 0;
}

static void dhcpd_cyclic_poll(void *ctx)
{
	(void)ctx;
	mtk_dhcpd_poll();
}

int mtk_dhcpd_start(void)
{
	rxhand_f *handler;
	int ret;

	ret = dhcpd_prepare_network();
	if (ret)
		return ret;

	if (dhcpd_running) {
		handler = net_get_udp_handler();
		if (handler != dhcpd_udp_handler) {
			prev_udp_handler = handler;
			net_set_udp_handler(dhcpd_udp_handler);
		}
		return 0;
	}

	dhcpd_cyclic = cyclic_register(dhcpd_cyclic_poll, 10000,
				       "mtk_dhcpd", NULL);
	if (!dhcpd_cyclic) {
		if (dhcpd_started_eth)
			eth_halt();
		dhcpd_started_eth = false;
		return -ENOMEM;
	}

	memset(leases, 0, sizeof(leases));
	prev_udp_handler = net_get_udp_handler();
	net_set_udp_handler(dhcpd_udp_handler);
	dhcpd_running = true;

	return 0;
}

void mtk_dhcpd_poll(void)
{
	struct udevice *eth;

	if (!dhcpd_running)
		return;

	eth = eth_get_dev();
	if (eth && eth_is_active(eth))
		eth_rx();
}

void mtk_dhcpd_stop(void)
{
	struct udevice *eth;

	if (!dhcpd_running)
		return;

	if (net_get_udp_handler() == dhcpd_udp_handler)
		net_set_udp_handler(prev_udp_handler);
	prev_udp_handler = NULL;
	dhcpd_running = false;
	if (dhcpd_cyclic) {
		cyclic_unregister(dhcpd_cyclic);
		dhcpd_cyclic = NULL;
	}

	eth = eth_get_dev();
	if (dhcpd_started_eth && eth && eth_is_active(eth))
		eth_halt();
	dhcpd_started_eth = false;
}

bool mtk_dhcpd_is_running(void)
{
	return dhcpd_running;
}
