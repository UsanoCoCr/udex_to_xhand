# ADR-033: M5c latency stats — `std::vector<double>` + sort for exact p95, not streaming / approx

Date: 2026-05-18
Status: Accepted
Milestone: M5c

## Context

Plan §3.2 wires per-frame latency
(`frame.recv_ts → just after send_command return` — PC2-internal only, not
glove-to-XHand wall clock; UDCAP JSON has no sender timestamp, see plan §2.2)
so that M5c §6.8 produces a quantitative latency summary at exit.

Acceptance bar: SPEC §9 phase 3.9 documents the target as `<50ms`. Plan §6.8
tightens this to `avg < 50ms AND p95 < 50ms`.

How to compute `p95`?

1. **Reservoir / fixed-capacity vector + sort at end** — exact percentile,
   O(n) memory, O(n log n) end-of-run cost.
2. **Streaming quantile estimator** (P², Greenwald-Khanna, t-digest) — O(1)
   or O(log n) memory, approximate percentile.
3. **Histogram with fixed buckets (HdrHistogram-style)** — O(buckets) memory,
   quantization error proportional to bucket width.

Session-size envelope:

- M5c §6.8 target session = 100 Hz × 30 s = ≤ 3 000 ticks.
- Actually 2026-05-18 produced **n = 1 773** samples in 31 s (only valid UDP
  frames are sampled; UDCAP @ ~60 Hz, PC2 polls @ 100 Hz, so ≈ 60% of ticks
  see a new frame).
- 1 773 × `sizeof(double)` ≈ 14 KB. Even an hour-long run would be ~1.7 MB.

## Decision

**Option 1 — `std::vector<double>` + `std::sort` at exit.**

Implementation lives in `main.cpp`'s anonymous namespace:

```cpp
struct LatencyStats {
    std::vector<double> samples_ms;
    void add(double ms);
    bool empty() const;
    void summary(std::ostream& os) const;  // sorts a copy; prints min/avg/p50/p95/max/n
};
```

`summary()` copies `samples_ms`, sorts, computes percentiles via
`s[std::min(n-1, size_t(q*(n-1)))]`, emits one line:
`latency_ms{n=… min=… avg=… p50=… p95=… max=…}`.

Printed once at exit, immediately before the existing `"exited after …"` tail
in main.cpp. Gated on `!latency_stats.empty()` so mock / receiver-only modes
(which don't engage `driver` and therefore never sample) stay quiet.

## Consequences

**Positives**
- Exact percentiles. No estimator-error term to argue about during review.
- Trivial to read / debug — `std::sort` + indexed lookup. Zero new library
  deps. Survives `-Wall -Wextra -Wpedantic` without diagnostics.
- 2026-05-18 M5c §6.8 measured
  `latency_ms{n=1773 min=9.52464 avg=9.59516 p50=9.5711 p95=9.61949 max=10.675}`
  in 31 s. Both `avg` and `p95` are **≈ 19% of the SPEC §9 50 ms ceiling**
  (DoD §7 D9 met with massive headroom). Distribution is remarkably tight
  (max − min ≈ 1.15 ms) → the C++ runtime + RS485 path is not introducing
  jitter at this scale, and the bottleneck is presumably the inherent
  `send_command` round-trip on RS485 @ 3 Mbps.
- The struct is small enough to live in `main.cpp`'s anonymous namespace
  without a separate `latency_stats.{hpp,cpp}` pair → matches CLAUDE.md
  "no abstractions beyond what the task requires".

**Negatives / risks**
- Unbounded vector growth for `--duration 0` (no time cap). At 100 Hz poll /
  ~60 Hz UDCAP, an hour = ~216 k samples ≈ 1.7 MB. Still trivial for PC2 RAM,
  but a multi-hour M8 stress test would push this past comfort. Plan §3.2
  explicitly defers the streaming/ring-buffer rewrite to M6+.
- Summary is computed only at exit. SIGKILL or hard crashes lose it. M5c §6.6
  graceful-shutdown path runs `summary()` before exit; SIGKILL bypass is
  acceptable.
- Percentile formula degenerates for very small `n` (single-digit). At
  `n = 1 773` it's irrelevant; flagged here so reviewers don't try to read
  `p95` from a 5-second debug run.

## Alternatives

1. **P² algorithm** (Jain–Chlamtac 1985) — O(1) state per quantile, no
   sample storage. Exact for normal distributions, ~1% relative error
   otherwise. Reasonable for an M6+ rewrite, but misses M5c's
   "complexity should be transparent during review" criterion. Adds ~120 LOC.
2. **HdrHistogram-style fixed-bucket histogram** — exact within bucket, O(B)
   memory. Industry standard for latency tracking but adds a header library
   (or 200+ LOC if reimplemented). Overkill for n ≤ 30 k.
3. **Defer measurement to an external profiler** (perf record, eBPF) — gives
   more detail (per-syscall, jitter sources) but requires PC2-side tooling +
   sudo + a decode pipeline outside this binary. Misses M5c's "did the C++
   rewrite preserve M4-era responsiveness?" question, which needs a number
   the operator can see at exit.
4. **Use a hypothetical `frame.udcap_ts`** — would yield wall-clock latency
   including UDP transit. UDCAP JSON has no sender timestamp (plan §2.2 +
   §3.1 note). Pursue only if/when UDCAP exposes one; not on M5c critical
   path.
