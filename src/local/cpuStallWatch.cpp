#include "local/cpuStallWatch.h"

#include "common/stringUtils.h"
#include "local/device-fault.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {

void Append(std::string& text, const char* format, ...) {
	char    line[1024];
	va_list args;
	va_start(args, format);
	const int length = std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	if (length > 0) text.append(line, std::min(static_cast<size_t>(length), sizeof(line) - 1));
}

} // namespace

namespace CpuStallWatch {

uint64_t ThresholdMs() {
	static const uint64_t threshold = []() -> uint64_t {
		const char* enable = std::getenv("KYTY_CPU_STALL_WATCH");
		if (enable == nullptr || *enable == '\0' || std::strtoul(enable, nullptr, 10) == 0) return 0;
		const char* ms = std::getenv("KYTY_CPU_STALL_MS");
		const auto  value = ms != nullptr && *ms != '\0' ? std::strtoull(ms, nullptr, 10) : 0;
		return value != 0 ? value : 2000;
	}();
	return threshold;
}

void NoteFrame(double frame_ms, const int64_t* waits, uint64_t count) {
	if (ThresholdMs() == 0 || frame_ms < static_cast<double>(ThresholdMs())) return;
	static std::atomic<bool> fired {false};
	if (fired.exchange(true, std::memory_order_acq_rel)) return;

	std::string text;
	Append(text, "CPU stall: frame %.1f ms (threshold %llu ms)\n", frame_ms,
	       static_cast<unsigned long long>(ThresholdMs()));
	static constexpr const char* names[] = {"gpu", "readback", "download", "compile", "record"};
	for (uint64_t i = 0; i < count && i < std::size(names); ++i)
		Append(text, "  wait %-9s : %.1f ms\n", names[i], static_cast<double>(waits[i]) / 1e6);

	// The last submissions and the pipelines/shader hashes they bound (breadcrumbs, always kept).
	DeviceFault::AppendSubmissions(text);

	std::string path = "/tmp/opencode/cpu-stall-";
	path += std::to_string(static_cast<unsigned long long>(getpid()));
	path += ".txt";
	if (FILE* file = std::fopen(path.c_str(), "w")) {
		std::fwrite(text.data(), 1, text.size(), file);
		std::fflush(file);
		std::fclose(file);
		std::printf("CPU stall watch: wrote %s\n", path.c_str());
		std::fflush(stdout);
	}
}

} // namespace CpuStallWatch
