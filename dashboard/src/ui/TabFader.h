#pragma once

enum class Tab { RecentlyPlayed, Collection };

// Which tab is active, and how visible each tab's content is while switching.
// The old tab clears out first, then the new one fades in, so the two never overlap.
class TabFader {
public:
	Tab active() const { return m_active; }
	bool isActive(Tab tab) const { return m_active == tab; }

	// Returns false if that tab was already active
	bool setActive(Tab tab);

	void update(float deltaSeconds);

	// 0 to 1
	float opacity(Tab tab) const;

private:
	Tab m_active = Tab::RecentlyPlayed;
	float m_recentlyPlayedOpacity = 1.0f;
	float m_collectionOpacity = 0.0f;
};