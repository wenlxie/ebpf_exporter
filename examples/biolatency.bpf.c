#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bits.bpf.h"

// Max number of disks we expect to see on the host
#define MAX_DISKS 255

// 27 buckets for latency, max range is 33.6s .. 67.1s
#define MAX_LATENCY_SLOT 27

#define MKDEV(ma, mi) ((mi & 0xff) | (ma << 8) | ((mi & ~0xff) << 12))

#define REQ_OP_BITS	8
#define REQ_OP_MASK	((1 << REQ_OP_BITS) - 1)

typedef struct disk_key {
	u32 dev;
	u8 op;
	u64 slot;
} disk_key_t;

static u64 zero = 0;

extern int LINUX_KERNEL_VERSION __kconfig;

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000);
	__type(key, struct request *);
	__type(value, u64);
} start SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, (MAX_LATENCY_SLOT + 1) * MAX_DISKS);
	__type(key, disk_key_t);
	__type(value, u64);
} io_latency SEC(".maps");

/**
 * commit d152c682f03c ("block: add an explicit ->disk backpointer to the
 * request_queue") and commit f3fa33acca9f ("block: remove the ->rq_disk
 * field in struct request") make some changes to `struct request` and
 * `struct request_queue`. Now, to get the `struct gendisk *` field in a CO-RE
 * way, we need both `struct request` and `struct request_queue`.
 * see:
 *     https://github.com/torvalds/linux/commit/d152c682f03c
 *     https://github.com/torvalds/linux/commit/f3fa33acca9f
 */
struct request_queue___x {
	struct gendisk *disk;
} __attribute__((preserve_access_index));

struct request___x {
	struct request_queue___x *q;
	struct gendisk *rq_disk;
} __attribute__((preserve_access_index));

static __always_inline struct gendisk *get_disk(void *request)
{
	struct request___x *r = request;

	if (bpf_core_field_exists(r->rq_disk))
		return BPF_CORE_READ(r, rq_disk);
	return BPF_CORE_READ(r, q, disk);
}

static __always_inline int trace_rq_start(struct request *rq)
{
	u64 ts = bpf_ktime_get_ns();
	bpf_map_update_elem(&start, &rq, &ts, 0);
	return 0;
}

SEC("raw_tp/block_rq_insert")
int rawtracepoint__block_rq_insert(struct bpf_raw_tracepoint_args *ctx)
{
	/**
	 * commit a54895fa (v5.11-rc1) changed tracepoint argument list
	 * from TP_PROTO(struct request_queue *q, struct request *rq)
	 * to TP_PROTO(struct request *rq)
	 */
	if (LINUX_KERNEL_VERSION < KERNEL_VERSION(5, 11, 0)) {
		return trace_rq_start((void *) ctx->args[1]);
	} else {
		return trace_rq_start((void *) ctx->args[0]);
	}
}

SEC("raw_tp/block_rq_issue")
int rawtracepoint__block_rq_issue(struct bpf_raw_tracepoint_args *ctx)
{
	/**
	 * commit a54895fa (v5.11-rc1) changed tracepoint argument list
	 * from TP_PROTO(struct request_queue *q, struct request *rq)
	 * to TP_PROTO(struct request *rq)
	 */
	if (LINUX_KERNEL_VERSION < KERNEL_VERSION(5, 11, 0)) {
		return trace_rq_start((void *) ctx->args[1]);
	} else {
		return trace_rq_start((void *) ctx->args[0]);
	}
}

SEC("raw_tp/block_rq_complete")
int rawtracepoint__block_rq_complete(struct bpf_raw_tracepoint_args *ctx)
{
	struct request *rq = (struct request *) ctx->args[0];
	struct gendisk *disk;
	u64 *count, *tsp, flags, delta_us, ts = bpf_ktime_get_ns();

	tsp = bpf_map_lookup_elem(&start, &rq);
	if (!tsp) {
		return 0;
	}

	// Delta in microseconds
	delta_us = (ts - *tsp) / 1000;

	// Latency histogram key
	u64 latency_slot = log2l(delta_us);

	// Cap latency bucket at max value
	if (latency_slot > MAX_LATENCY_SLOT) {
		latency_slot = MAX_LATENCY_SLOT;
	}

	disk = get_disk(rq);
	flags = BPF_CORE_READ(rq, cmd_flags);

	disk_key_t latency_key = {};
	latency_key.slot = latency_slot;
	latency_key.dev = disk ? MKDEV(BPF_CORE_READ(disk, major), BPF_CORE_READ(disk, first_minor)) : 0;
	latency_key.op = flags & REQ_OP_MASK;

	count = bpf_map_lookup_elem(&io_latency, &latency_key);
	if (!count) {
		bpf_map_update_elem(&io_latency, &latency_key, &zero, BPF_NOEXIST);
		count = bpf_map_lookup_elem(&io_latency, &latency_key);
		if (!count) {
			goto cleanup;
		}
	}
	__sync_fetch_and_add(count, 1);

	latency_key.slot = MAX_LATENCY_SLOT + 1;
	count = bpf_map_lookup_elem(&io_latency, &latency_key);
	if (!count) {
		bpf_map_update_elem(&io_latency, &latency_key, &zero, BPF_NOEXIST);
		count = bpf_map_lookup_elem(&io_latency, &latency_key);
		if (!count) {
			goto cleanup;
		}
	}
	__sync_fetch_and_add(count, delta_us);

cleanup:
	bpf_map_delete_elem(&start, &rq);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";