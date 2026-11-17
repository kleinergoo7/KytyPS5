#ifndef EMULATOR_SRC_LOCAL_CPUSTALLWATCH_H_
#define EMULATOR_SRC_LOCAL_CPUSTALLWATCH_H_
// Diagnostic: on the first frame slower than a threshold, write one report of what the render
// thread was waiting on. Default-off (KYTY_CPU_STALL_WATCH=1); no cost when disabled.
// Targets CPU-side stalls (e.g. the shader-compile stall at Astro Bot's splash->game transition)
// that the GPU-timeline stall detector in masterSemaphore does not cover.
#include <cstdint>

namespace CpuStallWatch {

// Returns the threshold in ms, or 0 when disabled. KYTY_CPU_STALL_WATCH=1 enables;
// KYTY_CPU_STALL_MS=<n> overrides the default 2000 ms threshold.
[[nodiscard]] uint64_t ThresholdMs();

// Called once per frame with its wall time (ms) and the five wait deltas (ns) in the order
// LiveCensus::Wait (gpu, readback, download, compile, record). Writes one report on the first
// frame over the threshold. No-op when disabled or after it has fired.
void NoteFrame(double frame_ms, const int64_t* wait_deltas_ns, uint64_t wait_count);

} // namespace CpuStallWatch

#endif // EMULATOR_SRC_LOCAL_CPUSTALLWATCH_H_
