#include "ui/widgets/ControllerWidget.h"
#include "ui/IconTile.h"
#include "ui/TextTile.h"
#include <algorithm>
#include <string>
#include "ui/RingTile.h"

namespace {

constexpr float ringDiameter  = 116.0f;
constexpr float ringThickness = 8.0f;
constexpr float ringOffsetX   = 22.0f;   // (160 - 116) / 2
constexpr float ringOffsetY   = 22.0f;

constexpr SDL_FColor ringTrackColor    = { 0.63f, 0.63f, 0.63f, 1.0f };
constexpr SDL_FColor ringColorGood = { 0.30f, 0.80f, 0.40f, 1.0f };  // green
constexpr SDL_FColor ringColorMid  = { 0.97f, 0.77f, 0.27f, 1.0f };  // the orange from the mockup
constexpr SDL_FColor ringColorLow  = { 0.93f, 0.28f, 0.25f, 1.0f };  // red

constexpr int ringGoodFromPercent = 67;   // this and above is green
constexpr int ringMidFromPercent  = 34;   // this and above is orange, below it is red


constexpr float normalPollSeconds = 2.0f;
constexpr float fastPollSeconds   = 0.25f;

constexpr const char* controllerSvg  = "icons/controller.svg";
constexpr const char* batterySvg     = "icons/battery.svg";
constexpr const char* batteryBarSvg  = "icons/battery bar.svg";
constexpr const char* playerFont     = "fonts/GoogleSans-SemiBold.ttf";
constexpr const char* noneSvg = "icons/none.svg";
constexpr float noneIconSize  = 54.0f;
constexpr float noneIconOffsetX = 53.0f;
constexpr float noneIconOffsetY = 53.3f;


constexpr int playerFontSize = 12;

constexpr float controllerIconWidth  = 30.0f;
constexpr float controllerIconHeight = 22.67f;

// These match icons/battery.svg (40 x 20) and icons/battery_bar.svg (7 x 10).
// Bar positions are measured from the battery's top left corner. Change them if you swap the artwork.
constexpr float batteryWidth  = 16.0f;
constexpr float batteryHeight = 10.18f;
constexpr float barWidth      = 2.91f;
constexpr float barHeight     = 5.82f;
constexpr float barFirstX     = 3.4f;
constexpr float barPitch      = 3.8f;
constexpr float barY          = 2.25f;

constexpr const char* boltSvg = "icons/bolt.svg";
constexpr float boltWidth   = 5.0f;
constexpr float boltHeight  = 8.0f;
constexpr float boltOffsetX = 6.0f;
constexpr float boltOffsetY = 1.0f;

// Top left of each element, measured from the widget's top left corner (design units)
constexpr float playerNumberOffsetX   = 78.0f;
constexpr float playerNumberOffsetY   = 48.0f;
constexpr float batteryOffsetX        = 72.0f;
constexpr float batteryOffsetY        = 97.0f;
constexpr float controllerIconOffsetX = 65.0f;
constexpr float controllerIconOffsetY = 67.0f;


constexpr SDL_FColor transparent = { 0.0f, 0.0f, 0.0f, 0.0f };
constexpr SDL_FColor white       = { 1.0f, 1.0f, 1.0f, 1.0f };

int barsForPercent(int percent) {
	if (percent >= 67) return 3;
	if (percent >= 34) return 2;
	if (percent >= 10) return 1;
	return 0;
}

SDL_FColor ringColorForPercent(int percent) {
	if (percent >= ringGoodFromPercent) return ringColorGood;
	if (percent >= ringMidFromPercent)  return ringColorMid;
	return ringColorLow;
}

} // namespace


ControllerWidget::ControllerWidget(float x_, float y_, float width_, float height_, SDL_GPUGraphicsPipeline* rectanglePipeline, SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler) : WidgetTile(x_, y_, width_, height_, rectanglePipeline), m_textPipeline(textPipeline), m_sampler(sampler), m_slotWidth(width_){
	setInteractable(false);

	// Only shows with zero controllers, when Dashboard has the widget hidden anyway
	m_noneIcon = makeIcon(noneSvg, noneIconSize, noneIconSize);
}


// Child tiles default to an opaque grey background, so every one of them is made transparent
IconTile* ControllerWidget::makeIcon(const char* svg, float w, float h) {
	IconTile* icon = addChild<IconTile>(0.0f, 0.0f, m_textPipeline, m_sampler);
	icon->setSvg(svg)->setIconSize(w, h)->setIconColor(white);
	icon->setBackgroundColor(transparent)->setInteractable(false);
	return icon;
}


// Creates the children for one more controller. Called from refresh(), so it can build what it makes.
void ControllerWidget::addSlot(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {
	Slot slot;

	// Added back to front, so the ring sits behind the rest
	slot.ring = addChild<RingTile>(0.0f, 0.0f, m_textPipeline, m_sampler);
	slot.ring->setRingSize(ringDiameter, ringThickness)->setTrackColor(ringTrackColor)->setProgressColor(ringColorGood);
	slot.ring->setInteractable(false);

	slot.controllerIcon = makeIcon(controllerSvg, controllerIconWidth, controllerIconHeight);
	slot.batteryOutline = makeIcon(batterySvg, batteryWidth, batteryHeight);
	for (IconTile*& bar : slot.batteryBars) bar = makeIcon(batteryBarSvg, barWidth, barHeight);

	slot.chargingBolt = makeIcon(boltSvg, boltWidth, boltHeight)->setIconColor(ringColorGood);

	slot.playerNumber = addChild<TextTile>(0.0f, 0.0f, m_textPipeline, m_sampler);
	slot.playerNumber->setFont(playerFont, playerFontSize);
	slot.playerNumber->setBackgroundColor(transparent)->setInteractable(false);

	// The icons never change, so they are built once here. The ring and number build when they get their values.
	slot.controllerIcon->build(gpuDevice, uploadCmd);
	slot.batteryOutline->build(gpuDevice, uploadCmd);
	for (IconTile* bar : slot.batteryBars) bar->build(gpuDevice, uploadCmd);
	slot.chargingBolt->build(gpuDevice, uploadCmd);

	m_slots.push_back(slot);
}


ControllerWidget* ControllerWidget::setStatusSource(std::function<std::vector<ControllerStatus>()> source) {
	m_source = std::move(source);
	requestUpdate();
	return this;
}


void ControllerWidget::refresh(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {
	const std::vector<ControllerStatus> statuses = m_source ? m_source() : std::vector<ControllerStatus>{};
	const int count = static_cast<int>(statuses.size());

	// One slot's width per controller. With none, a single slot's width is kept.
	setSize(m_slotWidth * static_cast<float>(std::max(count, 1)), height);

	while (m_slots.size() < statuses.size()) addSlot(gpuDevice, uploadCmd);

	bool waitingForBattery = false;

	for (size_t i = 0; i < m_slots.size(); ++i) {
		Slot& slot = m_slots[i];

		// Slots past the connected count are hidden, not destroyed, so they are ready if a controller returns
		if (i >= statuses.size()) {
			slot.ring->setVisible(false);
			slot.controllerIcon->setVisible(false);
			slot.batteryOutline->setVisible(false);
			for (IconTile* bar : slot.batteryBars) bar->setVisible(false);
			slot.chargingBolt->setVisible(false);
			slot.playerNumber->setVisible(false);
			continue;
		}

		const ControllerStatus& status = statuses[i];
		slot.controllerIcon->setVisible(true);

		// The number is a texture, so it is only re-rasterized when it changes
		if (slot.shownNumber != status.playerNumber) {
			slot.playerNumber->setText(std::to_string(status.playerNumber))->build(gpuDevice, uploadCmd);
			slot.shownNumber = status.playerNumber;
		}
		slot.playerNumber->setVisible(true);

		const bool hasPercent = status.batteryPercent >= 0;
		const bool hasBattery = hasPercent || status.charging;

		// While charging the bolt replaces the bars
		const int bars = (hasPercent && !status.charging) ? barsForPercent(status.batteryPercent) : 0;

		slot.batteryOutline->setVisible(hasBattery);
		slot.chargingBolt->setVisible(status.charging);
		for (int b = 0; b < batteryBars; ++b) slot.batteryBars[b]->setVisible(b >= batteryBars - bars);

		slot.ring->setVisible(hasPercent);
		if (hasPercent) {
			slot.ring->setProgress(status.batteryPercent / 100.0f)
			         ->setProgressColor(ringColorForPercent(status.batteryPercent))
			         ->build(gpuDevice, uploadCmd);
		}
		else {
			waitingForBattery = true;
		}
	}

	m_noneIcon->setVisible(count == 0);
	m_shown = statuses;

	setUpdateInterval(waitingForBattery ? fastPollSeconds : normalPollSeconds);
}


void ControllerWidget::layoutChildren() {
	m_noneIcon->setPosition(x + noneIconOffsetX, y + noneIconOffsetY);

	// Each slot is the same layout shifted right by one slot width
	for (size_t i = 0; i < m_slots.size(); ++i) {
		Slot& slot = m_slots[i];
		const float left = x + static_cast<float>(i) * m_slotWidth;

		slot.ring->setPosition(left + ringOffsetX, y + ringOffsetY);
		slot.playerNumber->setPosition(left + playerNumberOffsetX, y + playerNumberOffsetY);

		const float batteryX = left + batteryOffsetX;
		const float batteryY = y + batteryOffsetY;
		slot.batteryOutline->setPosition(batteryX, batteryY);
		for (int b = 0; b < batteryBars; ++b) {
			slot.batteryBars[b]->setPosition(batteryX + barFirstX + b * barPitch, batteryY + barY);
		}
		slot.chargingBolt->setPosition(batteryX + boltOffsetX, batteryY + boltOffsetY);

		slot.controllerIcon->setPosition(left + controllerIconOffsetX, y + controllerIconOffsetY);
	}
}
