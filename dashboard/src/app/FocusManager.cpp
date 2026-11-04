#include "app/FocusManager.h"
#include <algorithm>
#include <cmath>

void FocusManager::addTile(Row row, Tile* tile) {
	if (tile && tile->isInteractable()) {
		m_tileRows[row].items.push_back(tile);
	}
}

void FocusManager::setComponent(Row row, FocusableRow* component) { m_components[row] = component; }
void FocusManager::setRowEnabled(Row row, bool enabled) { m_enabled[row] = enabled; }
void FocusManager::setLeaveTarget(Row from, Tile* tile) { m_leaveTarget[from] = tile; }

size_t FocusManager::rowSize(Row row) const {
	if (!m_enabled[row]) return 0;
	return m_components[row] ? m_components[row]->size() : m_tileRows[row].items.size();
}

void FocusManager::navigate(NavInput nav) {
	FocusableRow* component = m_components[m_row];

	switch (nav) {
		case NavInput::Left:  moveHorizontal(-1); break;
		case NavInput::Right: moveHorizontal(1);  break;
		case NavInput::Up:    moveVertical(-1);   break;
		case NavInput::Down:  moveVertical(1);    break;
		case NavInput::JumpFirst: if (component) component->jumpToFirst(); break;
		case NavInput::JumpLast:  if (component) component->jumpToLast();  break;
		default: break;
	}
}

void FocusManager::moveHorizontal(int delta) {
	// Component rows scroll, so they handle it themselves
	if (FocusableRow* component = m_components[m_row]) {
		component->moveHorizontal(delta);
		return;
	}

	TileRow& group = m_tileRows[m_row];
	if (group.items.empty()) return;

	const size_t last = group.items.size() - 1;
	if (delta < 0 && group.column > 0) --group.column;
	else if (delta > 0 && group.column < last) ++group.column;
	else return; // already at the end of the row

	refresh();
}

void FocusManager::moveVertical(int delta) {
	// A component row moves (and scrolls) through its own rows first.
	// Focus only leaves it when there is no row left in that direction.
	FocusableRow* component = m_components[m_row];
	if (component && component->moveVertical(delta)) return;

	// Step to the next row in that direction that has something focusable
	int target = static_cast<int>(m_row) + delta;
	while (target >= 0 && target < RowCount && rowSize(static_cast<Row>(target)) == 0) {
		target += delta;
	}
	if (target < 0 || target >= RowCount) return;

	// Measure where we are before changing rows
	const float fromX = anchorX();
	const Row previousRow = m_row;
	m_row = static_cast<Row>(target);

	// Component rows keep their remembered selection. A row of tiles picks one now.
	if (!m_components[m_row]) {
		TileRow& group = m_tileRows[m_row];

		Tile* leaveTarget = m_leaveTarget[previousRow];
		const auto landing = std::find(group.items.begin(), group.items.end(), leaveTarget);

		size_t best = 0;
		if (leaveTarget && landing != group.items.end()) {
			best = static_cast<size_t>(landing - group.items.begin());
		}
		else {
			float bestDistance = std::abs(group.items[0]->getCenterX() - fromX);
			for (size_t i = 1; i < group.items.size(); ++i) {
				const float distance = std::abs(group.items[i]->getCenterX() - fromX);
				if (distance < bestDistance) {
					bestDistance = distance;
					best = i;
				}
			}
		}
		group.column = best;
	}

	refresh();
}

// The x position to compare against when entering another row
float FocusManager::anchorX() const {
	if (m_components[m_row]) return m_components[m_row]->anchorX();

	const TileRow& group = m_tileRows[m_row];
	return group.items[group.column]->getCenterX();
}

void FocusManager::focusRow(Row row, bool animate) {
	if (rowSize(row) == 0) return;

	m_row = row;
	refresh(animate);
}

void FocusManager::focusTile(Tile* tile) {
	for (int row = 0; row < RowCount; ++row) {
		TileRow& group = m_tileRows[row];
		const auto it = std::find(group.items.begin(), group.items.end(), tile);
		if (it == group.items.end()) continue;

		group.column = static_cast<size_t>(it - group.items.begin());
		m_row = static_cast<Row>(row);
		refresh();
		return;
	}
}

Tile* FocusManager::focusedTile() const {
	if (m_components[m_row]) return nullptr;

	const TileRow& group = m_tileRows[m_row];
	return group.items.empty() ? nullptr : group.items[group.column];
}

void FocusManager::refresh(bool animate) {
	for (int row = 0; row < RowCount; ++row) {
		const TileRow& group = m_tileRows[row];
		for (size_t i = 0; i < group.items.size(); ++i) {
			group.items[i]->setFocused(row == m_row && i == group.column);
		}
	}

	// The callback can switch tabs, so it runs before the component rows are laid out
	if (m_onChanged) m_onChanged();

	// A component row's selected item only gets its border while the row has focus
	for (int row = 0; row < RowCount; ++row) {
		if (m_components[row]) m_components[row]->setFocused(row == m_row, animate);
	}
}