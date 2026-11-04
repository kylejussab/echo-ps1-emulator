#pragma once

// Positions, sizes and fonts shared by more than one part of the dashboard.
// Everything is in design units (see Renderer::designWidth / designHeight).
namespace layout {

// Top-left corner of the featured tile. The rest of the row follows from here.
inline constexpr float rowStartX = 120.0f;
inline constexpr float rowStartY = 260.0f;

// Content is cut off above this line, so a scrolled grid row slides under the navigation
inline constexpr float contentClipTop = rowStartY - 24.0f;

inline constexpr float tileCornerRadius = 16.0f;
inline constexpr float selectedBorderWidth = 2.0f;

inline constexpr const char* fontItalic   = "fonts/GoogleSans-Italic.ttf";
inline constexpr const char* fontRegular  = "fonts/GoogleSans-Regular.ttf";
inline constexpr const char* fontSemiBold = "fonts/GoogleSans-SemiBold.ttf";

} // namespace layout