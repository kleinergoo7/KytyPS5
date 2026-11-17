#include "graphics/presentation/debugPanel.h"

#include "common/debugCounters.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace Libs::Graphics {

namespace DC = Common::DebugCounters;

// Written by the window thread (F2), read by the present thread — must be atomic.
static std::atomic<bool> s_enabled {false};

static std::chrono::steady_clock::time_point s_last = std::chrono::steady_clock::now();
static bool s_have_prev = false;
static std::array<uint64_t, static_cast<size_t>(DC::Counter::Count)> s_prev {};

static double s_draws_ps = 0.0;
static double s_dispatches_ps = 0.0;
static double s_submits_ps = 0.0;
static double s_gpu_waits_ps = 0.0;
static double s_gpu_wait_ms_ps = 0.0;

void DebugPanel::SetEnabled(bool enabled) {
	s_enabled.store(enabled, std::memory_order_release);
	DC::SetEnabled(enabled);
}

bool DebugPanel::Enabled() noexcept {
	return s_enabled.load(std::memory_order_acquire);
}

void DebugPanel::Toggle() noexcept {
	bool expected = s_enabled.load(std::memory_order_relaxed);
	while (!s_enabled.compare_exchange_weak(expected, !expected, std::memory_order_acq_rel,
	                                        std::memory_order_relaxed)) {}
	DC::SetEnabled(!expected);
}

void DebugPanel::Draw(GraphicContext& /*graphics*/, const ImVec2& extent) {
	if (!s_enabled.load(std::memory_order_acquire)) {
		return;
	}
	using DCtr = DC::Counter;
	const auto now = std::chrono::steady_clock::now();
	const double dt = std::chrono::duration<double>(now - s_last).count();
	const auto cur_draws = DC::Get(DCtr::Draws);
	const auto cur_dispatches = DC::Get(DCtr::Dispatches);
	const auto cur_submits = DC::Get(DCtr::QueueSubmits);
	const auto cur_gpu_waits = DC::Get(DCtr::GpuWaits);
	const auto cur_gpu_wait_ns = DC::Get(DCtr::GpuWaitNs);

	if (!s_have_prev || dt >= 1.0) {
		if (s_have_prev && dt > 0.0) {
			s_draws_ps = static_cast<double>(cur_draws - s_prev[static_cast<size_t>(DCtr::Draws)]) / dt;
			s_dispatches_ps = static_cast<double>(cur_dispatches - s_prev[static_cast<size_t>(DCtr::Dispatches)]) / dt;
			s_submits_ps = static_cast<double>(cur_submits - s_prev[static_cast<size_t>(DCtr::QueueSubmits)]) / dt;
			s_gpu_waits_ps = static_cast<double>(cur_gpu_waits - s_prev[static_cast<size_t>(DCtr::GpuWaits)]) / dt;
			s_gpu_wait_ms_ps = static_cast<double>(cur_gpu_wait_ns - s_prev[static_cast<size_t>(DCtr::GpuWaitNs)]) / 1e6 / dt;
		}
		s_prev[static_cast<size_t>(DCtr::Draws)] = cur_draws;
		s_prev[static_cast<size_t>(DCtr::Dispatches)] = cur_dispatches;
		s_prev[static_cast<size_t>(DCtr::QueueSubmits)] = cur_submits;
		s_prev[static_cast<size_t>(DCtr::GpuWaits)] = cur_gpu_waits;
		s_prev[static_cast<size_t>(DCtr::GpuWaitNs)] = cur_gpu_wait_ns;
		s_last = now;
		s_have_prev = true;
	}

	ImGui::SetNextWindowPos({extent.x - 16.0f, 16.0f}, ImGuiCond_Always, {1.0f, 0.0f});
	ImGui::SetNextWindowBgAlpha(0.45f);
	constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
	                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
	ImGui::Begin("##DebugPanel", nullptr, flags);
	ImGui::Text("Draws/s %.0f", s_draws_ps);
	ImGui::Text("Dispatches/s %.0f", s_dispatches_ps);
	ImGui::Text("Submits/s %.0f", s_submits_ps);
	ImGui::Text("GPU waits/s %.0f", s_gpu_waits_ps);
	ImGui::Text("GPU wait ms %.1f", s_gpu_wait_ms_ps);
	ImGui::Text("Shaders compiled %llu", static_cast<unsigned long long>(DC::Get(DCtr::ShadersCompiled)));
	ImGui::Text("Compile peak ms %.1f", static_cast<double>(DC::Get(DC::Peak::ShaderCompileNs)) / 1e6);
	ImGui::Text("Flips %llu", static_cast<unsigned long long>(DC::Get(DCtr::FlipsRequested)));
	ImGui::TextColored({1.0f, 0.6f, 0.2f, 1.0f}, "Late vblanks %llu", static_cast<unsigned long long>(DC::Get(DCtr::LateVblanks)));
	ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Dropped presents %llu", static_cast<unsigned long long>(DC::Get(DCtr::DroppedPresents)));
	ImGui::End();
}

} // namespace Libs::Graphics