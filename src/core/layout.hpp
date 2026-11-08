#pragma once

#include <algorithm>
#include <cstddef>

namespace deck {
struct Rect {
  int x = 0, y = 0, width = 0, height = 0;
};

inline Rect fit(int sourceWidth, int sourceHeight, int width, int height)
{
  if (sourceWidth <= 0 || sourceHeight <= 0 || width <= 0 || height <= 0)
    return {};
  const double scale = std::min(double(width) / sourceWidth, double(height) / sourceHeight);
  const int w = std::max(1, int(sourceWidth * scale));
  const int h = std::max(1, int(sourceHeight * scale));
  return {(width - w) / 2, (height - h) / 2, w, h};
}

inline int pageCount(int count, int pageSize)
{
  return count <= 0 || pageSize <= 0 ? 1 : 1 + (count - 1) / pageSize;
}

inline int clampPage(int page, int count, int pageSize)
{
  return std::clamp(page, 0, pageCount(count, pageSize) - 1);
}

inline int slotIndex(int page, int pageSize, int slot, int count)
{
  if (page < 0 || pageSize <= 0 || slot < 0 || slot >= pageSize || count <= 0)
    return -1;
  const auto index = static_cast<long long>(page) * pageSize + slot;
  return index < count ? static_cast<int>(index) : -1;
}

// Left/right follow scene order across rows and pages. Up/down retain their
// column where possible. The caller reveals the page containing the result.
inline int moveSelection(int current, int count, int columns, int dx, int dy)
{
  if (count <= 0 || columns <= 0)
    return -1;
  current = std::clamp(current, 0, count - 1);
  if (dx < 0)
    return std::max(0, current - 1);
  if (dx > 0)
    return std::min(count - 1, current + 1);
  if (dy < 0)
    return current < columns ? current : current - columns;
  if (dy > 0) {
    if (current / columns == (count - 1) / columns)
      return current;
    return std::min(current + columns, count - 1);
  }
  return current;
}
} // namespace deck
