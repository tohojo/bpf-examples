/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TIMESTAMP_MAP_H
#define TIMESTAMP_MAP_H
#include <linux/types.h>

struct ipv4_flow {
	__u32 saddr;
	__u32 daddr;
	__u16 sport;
	__u16 dport;
};

struct ts_key {
	struct ipv4_flow flow;
	__u32 tsval;
};

struct ts_timestamp {
	__u64 timestamp;
	//__u64 ttl; // Delete entry after ttl, allows more dynamic map cleaning where entries for flows with short RTTs can be removed earlier
	__u8 used;
	// __u8 pad[7]; // Need to pad it due to compiler optimization, see "Remove struct padding with aligning members by using #pragma pack." at https://docs.cilium.io/en/v1.9/bpf/
};

struct rtt_event {
	struct ipv4_flow flow;
	__u64 rtt;
};

#endif