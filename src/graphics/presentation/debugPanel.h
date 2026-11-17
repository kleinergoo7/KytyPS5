#ifndef EMULATOR_SRC_GRAPHICS_PRESENTATION_DEBUGPANEL_H_
#define EMULATOR_SRC_GRAPHICS_PRESENTATION_DEBUGPANEL_H_

#include "graphics/host_gpu/graphicContext.h"

#include "imgui.h"

namespace Libs::Graphics {

class DebugPanel {
public:
	static void SetEnabled(bool enabled);
	static bool Enabled() noexcept;
	static void Toggle() noexcept;
	static void Draw(GraphicContext& graphics, const ImVec2& extent);
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_PRESENTATION_DEBUGPANEL_H_