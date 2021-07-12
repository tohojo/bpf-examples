/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PPING_H
#define PPING_H

#include <linux/types.h>
#include <linux/in6.h>
#include <stdbool.h>

#define SEC_INGRESS_XDP "xdp"
#define SEC_INGRESS_TC "classifier/ingress"
#define SEC_EGRESS_TC "classifier/egress"

typedef __u64 fixpoint64;
#define FIXPOINT_SHIFT 16
#define DOUBLE_TO_FIXPOINT(X) ((fixpoint64)((X) * (1UL << FIXPOINT_SHIFT)))
#define FIXPOINT_TO_UINT(X) ((X) >> FIXPOINT_SHIFT)

/* For the event_type members of rtt_event and flow_event */
#define EVENT_TYPE_FLOW 1
#define EVENT_TYPE_RTT 2

enum __attribute__((__packed__)) flow_event_type {
	FLOW_EVENT_NONE,
	FLOW_EVENT_OPENING,
	FLOW_EVENT_CLOSING
};

enum __attribute__((__packed__)) flow_event_reason {
	EVENT_REASON_SYN,
	EVENT_REASON_SYN_ACK,
	EVENT_REASON_FIRST_OBS_PCKT,
	EVENT_REASON_FIN,
	EVENT_REASON_FIN_ACK,
	EVENT_REASON_RST,
	EVENT_REASON_FLOW_TIMEOUT
};

enum __attribute__((__packed__)) flow_event_source {
	EVENT_SOURCE_EGRESS,
	EVENT_SOURCE_INGRESS,
	EVENT_SOURCE_USERSPACE
};

struct bpf_config {
	__u64 rate_limit;
	fixpoint64 rtt_rate;
	bool use_srtt;
	__u8 reserved[7];
};

/*
 * Struct that can hold the source or destination address for a flow (l3+l4).
 * Works for both IPv4 and IPv6, as IPv4 addresses can be mapped to IPv6 ones
 * based on RFC 4291 Section 2.5.5.2.
 */
struct flow_address {
	struct in6_addr ip;
	__u16 port;
	__u16 reserved;
};

/*
 * Struct to hold a full network tuple
 * The ipv member is technically not necessary, but makes it easier to 
 * determine if saddr/daddr are IPv4 or IPv6 address (don't need to look at the
 * first 12 bytes of address). The proto memeber is not currently used, but 
 * could be useful once pping is extended to work for other protocols than TCP.
 */
struct network_tuple {
	struct flow_address saddr;
	struct flow_address daddr;
	__u16 proto; //IPPROTO_TCP, IPPROTO_ICMP, QUIC etc
	__u8 ipv; //AF_INET or AF_INET6
	__u8 reserved;
};

struct flow_state {
	__u64 min_rtt;
	__u64 srtt;
	__u64 last_timestamp;
	__u64 sent_pkts;
	__u64 sent_bytes;
	__u64 rec_pkts;
	__u64 rec_bytes;
	__u32 last_id;
	__u32 reserved;
};

struct packet_id {
	struct network_tuple flow;
	__u32 identifier; //tsval for TCP packets
};

/*
 * An RTT event message that can be passed from the bpf-programs to user-space.
 * The initial event_type memeber is used to allow multiplexing between
 * different event types in a single perf buffer. Memebers up to and including
 * flow are identical to other event types.
 * Uses explicit padding instead of packing based on recommendations in cilium's
 * BPF reference documentation at https://docs.cilium.io/en/stable/bpf/#llvm.
 */
struct rtt_event {
	__u64 event_type;
	__u64 timestamp;
	struct network_tuple flow;
	__u32 padding;
	__u64 rtt;
	__u64 min_rtt;
	__u64 sent_pkts;
	__u64 sent_bytes;
	__u64 rec_pkts;
	__u64 rec_bytes;
	__u32 reserved;
};

struct flow_event_info {
	enum flow_event_type event;
	enum flow_event_reason reason;
};

/*
 * A flow event message that can be passed from the bpf-programs to user-space.
 * The initial event_type memeber is used to allow multiplexing between
 * different event types in a single perf buffer. Memebers up to and including
 * flow are identical to other event types.
 */
struct flow_event {
	__u64 event_type;
	__u64 timestamp;
	struct network_tuple flow;
	struct flow_event_info event_info;
	enum flow_event_source source;
	__u8 reserved;
};

union pping_event {
	__u64 event_type;
	struct rtt_event rtt_event;
	struct flow_event flow_event;
};

#endif
