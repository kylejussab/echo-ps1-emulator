#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include "input/ControllerInput.h"
#include "ui/FocusableRow.h"
#include "ui/Tile.h"

// Decides what has focus, and moves it around in response to the d-pad.
//
// Rows are stacked top to bottom. A row is either a list of tiles (the manager remembers which one
// was last focused), or a FocusableRow component that keeps its own selection.
class FocusManager {
public:
	enum Row { RowNav, RowCollection, RowGames, RowWidgets, RowCount };

	// A row of plain tiles, left to right. Tiles that aren't interactable are skipped.
	void addTile(Row row, Tile* tile);

	// A row that keeps its own selection and scrolling. Not owned by the manager.
	void setComponent(Row row, FocusableRow* component);

	// Disabled rows are skipped, for example because their tab isn't showing.
	// Everything starts enabled except the collection, which belongs to the second tab.
	void setRowEnabled(Row row, bool enabled);

	// Moving out of `from` into a row of tiles lands on `tile`, instead of the one closest in x
	void setLeaveTarget(Row from, Tile* tile);

	// Called after focus changes, once the tiles have been updated
	void setOnChanged(std::function<void()> callback) { m_onChanged = std::move(callback); }

	void navigate(NavInput nav);
	void focusRow(Row row, bool animate = true);
	void focusTile(Tile* tile);

	// Pushes the current focus to every tile and row again
	void refresh(bool animate = true);

	Row currentRow() const { return m_row; }

	// The focused tile, or null while focus is inside a component row
	Tile* focusedTile() const;

private:
	struct TileRow {
		std::vector<Tile*> items; // left to right
		size_t column = 0;        // remembered position
	};

	size_t rowSize(Row row) const;
	void moveHorizontal(int delta);
	void moveVertical(int delta);
	float anchorX() const;

	TileRow m_tileRows[RowCount];
	FocusableRow* m_components[RowCount] = {};
	bool m_enabled[RowCount] = { true, false, true, true }; // Nav, Collection, Games, Widgets
	Tile* m_leaveTarget[RowCount] = {};
	Row m_row = RowGames;
	std::function<void()> m_onChanged;
};