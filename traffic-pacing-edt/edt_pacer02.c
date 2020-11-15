/* SPDX-License-Identifier: GPL-2.0+ */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/compiler.h>
#include "iproute2_compat.h"

#define VLAN_MAX_DEPTH 2
#include <xdp/parsing_helpers.h>

char _license[] SEC("license") = "GPL";

#define NS_PER_SEC 1000000000

/* skb->len in bytes, thus easier to keep rate in bytes */
#define RATE_IN_BITS	(1000 * 1000 * 1000ULL)
//#define RATE_IN_BITS	(200 * 1000 * 1000ULL)
//#define RATE_IN_BITS	(500 * 1000 * 1000ULL)
#define RATE_IN_BYTES	(RATE_IN_BITS / 8)

//#define T_HORIZON_DROP	(2000 * 1000 * 1000ULL)
//#define T_HORIZON_DROP	(200000 * 1000 * 1000ULL)
#define T_HORIZON_DROP	(15 * 1000 * 1000ULL)

#define T_HORIZON_ECN	(5 * 1000 * 1000ULL)

struct edt_val {
	__u64	rate;
	__u64	t_last;
	__u64	t_horizon_drop;
	__u64	t_horizon_ecn;
} __aligned(64); /* Align struct to cache-size to avoid false-sharing */

/* The tc tool (iproute2) use another ELF map layout than libbpf (struct
 * bpf_map_def), see struct bpf_elf_map from iproute2.
 */
struct bpf_elf_map SEC("maps") time_delay_map = {
	.type		= BPF_MAP_TYPE_ARRAY,
	.size_key	= sizeof(__u32),
	.size_value	= sizeof(struct edt_val),
	.max_elem	= 1,
	//.pinning	= PIN_GLOBAL_NS,
};

/* Role of EDT (Earliest Departure Time) is to schedule departure of packets to
 * be send in the future.
 */
static __always_inline int sched_departure(struct __sk_buff *skb)
{
	struct edt_val *edt;
	__u64 t_queue_sz;
	__u64 t_xmit_ns;
	__u64 t_next;
	__u64 t_curr;
	int key = 0;
	__u64 now;

	edt = bpf_map_lookup_elem(&time_delay_map, &key);
	if (!edt)
		return BPF_DROP;

	/* Calc transmission time it takes to send packet 'bytes'.
	 *
	 * Details on getting precise bytes on wire.  The skb->len does include
	 * length of GRO/GSO segments, but not the segment headers that gets
	 * added on transmit.  Fortunately skb->wire_len at TC-egress hook (not
	 * ingress) include these headers. (See: qdisc_pkt_len_init())
	 */
	t_xmit_ns = ((__u64)skb->wire_len) * NS_PER_SEC / RATE_IN_BYTES;
	// t_xmit_ns = ((__u64)skb->wire_len) * NS_PER_SEC / edt->rate;

	now = bpf_ktime_get_ns();

	/* Allow others to set skb tstamp prior to us */
	t_curr  = skb->tstamp;
	if (t_curr < now)
		t_curr = now;

	/* The 't_last' timestamp can be in the future. Packets scheduled a head
	 * of his packet can be seen as the queue size measured in time, via
	 * correlating this to 'now' timestamp.
	 */
	t_next = READ_ONCE(edt->t_last) + t_xmit_ns;

	/* If packet doesn't get scheduled into the future, then there is
	 * no-queue and we are not above rate limit. Send packet immediately and
	 * move forward t_last timestamp to now.
	 */
	if (t_next <= t_curr) {
		WRITE_ONCE(edt->t_last, t_curr);
		return BPF_OK;
	}

	/* Calc queue size measured in time */
	t_queue_sz = t_next - now;

	/* FQ-pacing qdisc also have horizon, but cannot use that, because this
	 * BPF-prog will have updated map (t_last) on packet and assumes it got
	 * its part of bandwidth.
	 */
	if (t_queue_sz >= T_HORIZON_DROP /* edt->t_horizon_drop */)
		return BPF_DROP;

	/* ECN marking horizon */
	if (t_queue_sz >= T_HORIZON_ECN)
		bpf_skb_ecn_set_ce(skb);

	/* Advance "time queue" */
	WRITE_ONCE(edt->t_last, t_next);

	/* Schedule packet to be send at future timestamp */
	skb->tstamp = t_next;
	return BPF_OK;
}

SEC("classifier") int tc_edt_simple(struct __sk_buff *skb)
{
	void *data     = (void *)(long)skb->data;
	void *data_end = (void *)(long)skb->data_end;
	struct collect_vlans vlans = { 0 };
	struct ethhdr *eth;
	int ret = BPF_OK;

	/* These keep track of the next header type and iterator pointer */
	struct hdr_cursor nh;
	int eth_type;
	nh.pos = data;

	eth_type = parse_ethhdr_vlan(&nh, data_end, &eth, &vlans);
	if (eth_type < 0)
		return XDP_ABORTED;

	/* Keep ARP resolution working */
	if (eth_type == bpf_htons(ETH_P_ARP)) {
		ret = BPF_OK;
		goto out;
	}

	if (!proto_is_vlan(eth->h_proto)) {
		/* Skip non-VLAN frames */
		return BPF_OK;
	}

	/* Match on vlan16 and only apply EDT on that */
	// FIXME: handle if VLAN is not inlined in packet
	if (vlans.id[0] == 16)
		return sched_departure(skb);

 out:
	return ret;
}