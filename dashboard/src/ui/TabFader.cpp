#include "ui/TabFader.h"
#include <algorithm>

namespace {

constexpr float fadeOutSeconds = 0.1f;  // old tab clears out quickly
constexpr float fadeInSeconds  = 0.3f;

float moveToward(float current, float target, float maximumChange) {
	if (current < target) return std::min(current + maximumChange, target);
	return std::max(current - maximumChange, target);
}

} // namespace

bool TabFader::setActive(Tab tab) {
	if (tab == m_active) return false;
	m_active = tab;
	return true;
}

void TabFader::update(float deltaSeconds) {
	const bool recent = (m_active == Tab::RecentlyPlayed);
	float& outgoing = recent ? m_collectionOpacity : m_recentlyPlayedOpacity;
	float& incoming = recent ? m_recentlyPlayedOpacity : m_collectionOpacity;

	outgoing = moveToward(outgoing, 0.0f, deltaSeconds / fadeOutSeconds);

	const bool outgoingGone = (outgoing <= 0.0f);
	incoming = moveToward(incoming, outgoingGone ? 1.0f : 0.0f,
	                      deltaSeconds / (outgoingGone ? fadeInSeconds : fadeOutSeconds));
}

float TabFader::opacity(Tab tab) const {
	return tab == Tab::RecentlyPlayed ? m_recentlyPlayedOpacity : m_collectionOpacity;
}