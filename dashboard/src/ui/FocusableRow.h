#pragma once

// A row of content that keeps its own selection, like the games row or the collection grid.
// The FocusManager moves focus into, out of and within it.
class FocusableRow {
public:
	virtual ~FocusableRow() = default;

	// How many things can be selected. 0 means focus skips this row.
	virtual size_t size() const = 0;

	virtual void moveHorizontal(int delta) = 0;

	// Returns true if the selection moved inside the row. False lets focus leave the row.
	virtual bool moveVertical(int delta) = 0;

	virtual void jumpToFirst() = 0;
	virtual void jumpToLast() = 0;

	// The x position focus compares against when it moves to another row
	virtual float anchorX() const = 0;

	// Whether the row has input focus. Only then does its selection show a border.
	virtual void setFocused(bool focused, bool animate) = 0;
};