/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>
#include "bits.bpf.h"
#include "directreclaim.h"

extern int LINUX_KERNEL_VERSION __kconfig;

static struct hist direct_reclaim_latency_hist;

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, MAX_ENTRIES);
  __type(key, u32);
  __type(value, u64);
} start SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, MAX_ENTRIES);
  __type(key, struct hist_key);
  __type(value, struct hist);
} direct_reclaim_latency SEC(".maps");


SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_begin")
int tracepoint__vmscan__mm_vmscan_direct_reclaim_begin(void *ctx)
{
    u32 pid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&start, &pid, &ts, 0);
    return 0;
}

SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_end")
int tracepoint__vmscan__mm_vmscan_direct_reclaim_end(void *ctx)
{
   u32 pid = bpf_get_current_pid_tgid();
    u64 *tsp;
    struct hist_key hkey = {};
    struct hist *histl = NULL;
    tsp = bpf_map_lookup_elem(&start, &pid);
    if (!tsp)
    	return 0;
    u64 latency_us = (bpf_ktime_get_ns() - *tsp) / 1000;
    u64 latency_slot = log2l(latency_us);

    // Cap latency bucket at max value
    if (latency_slot > max_latency_slot) {
       latency_slot = max_latency_slot;
    }

    hkey.slot = latency_slot;
    histl = bpf_map_lookup_elem(&direct_reclaim_latency, &hkey);
    if (!histl) {
    	bpf_map_update_elem(&direct_reclaim_latency, &hkey, &direct_reclaim_latency_hist, 0);
    	histl = bpf_map_lookup_elem(&direct_reclaim_latency, &hkey);
    	if (!histl) {
            goto cleanup;
    	}
    }
    __sync_fetch_and_add(&histl->counters, 1);

    hkey.slot = max_latency_slot + 1;
    histl = bpf_map_lookup_elem(&direct_reclaim_latency, &(hkey));
    if (!histl) {
       bpf_map_update_elem(&direct_reclaim_latency, &hkey, &direct_reclaim_latency_hist, 0);
       histl = bpf_map_lookup_elem(&direct_reclaim_latency, &hkey);
       if (!histl) {
          goto cleanup;
       }
    }
    __sync_fetch_and_add(&histl->counters, latency_us);

cleanup:
	bpf_map_delete_elem(&start, &pid);
	return 0;
}


char LICENSE[] SEC("license") = "GPL";