#pragma once

#include "ui/widgets/WidgetTile.h"
#include <functional>
#include <vector>

class IconTile;
class TextTile;
class RingTile;

struct ControllerStatus {
	int playerNumber = 1;    // 1 based. 0 or less hides the number.
	int batteryPercent = -1; // 0 to 100. Negative means no battery (wired), which hides the battery.
	bool charging = false; 
};

// One controller: its icon, the player number it is assigned to, and its battery.
// When several controllers are supported, this is the part that gets repeated for each one.
class ControllerWidget : public WidgetTile {
public:
	static constexpr int batteryBars = 3;

	ControllerWidget(float x, float y, float width, float height,
	                 SDL_GPUGraphicsPipeline* rectanglePipeline, SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler);

	// Where the widget gets its data on every refresh (call this first in a chain)
	ControllerWidget* setStatusSource(std::function<std::vector<ControllerStatus>()> source);

	// False until the first refresh, and whenever no controller is connected
	bool isConnected() const { return !m_shown.empty(); }

protected:
	void refresh(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) override;
	void layoutChildren() override;

private:
	// Everything drawn for one controller
	struct Slot {
		RingTile* ring = nullptr;
		IconTile* controllerIcon = nullptr;
		IconTile* batteryOutline = nullptr;
		IconTile* batteryBars[batteryBars] = {};
		TextTile* playerNumber = nullptr;
		int shownNumber = 0;   // what the number's texture currently says
		IconTile* chargingBolt = nullptr;
	};

	IconTile* makeIcon(const char* svg, float w, float h);
	void addSlot(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd);

	std::function<std::vector<ControllerStatus>()> m_source;

	SDL_GPUGraphicsPipeline* m_textPipeline;
	SDL_GPUSampler* m_sampler;
	float m_slotWidth;   // the width the widget was created with. It grows by this much per controller.

	std::vector<Slot> m_slots;
	IconTile* m_noneIcon = nullptr;

	std::vector<ControllerStatus> m_shown;
};
