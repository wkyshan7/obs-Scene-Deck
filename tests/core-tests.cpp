#include "core/layout.hpp"
#include <cstdlib>
#include <iostream>

static int checks = 0;
static void check(bool condition, const char *message)
{
  ++checks;
  if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}

int main()
{
  check(deck::pageCount(0, 6) == 1, "empty list has an empty page");
  check(deck::pageCount(7, 6) == 2, "partial last page");
  check(deck::slotIndex(1, 6, 1, 7) == -1, "empty slot is not an existing card");
  check(deck::slotIndex(0, 6, -1, 7) == -1, "negative slot rejected");
  check(deck::slotIndex(0, 6, 6, 7) == -1, "slot outside page rejected");
  check(deck::slotIndex(1000000000, 24, 1, 7) == -1, "page index cannot overflow");
  check(deck::clampPage(5, 0, 6) == 0, "scene deletion clamps current page");
  check(deck::moveSelection(2, 7, 3, 1, 0) == 3, "right follows the next row");
  check(deck::moveSelection(3, 7, 3, -1, 0) == 2, "left returns to the preceding row");
  check(deck::moveSelection(5, 8, 3, 1, 0) == 6, "right crosses a six-card page boundary");
  check(deck::moveSelection(6, 8, 3, -1, 0) == 5, "left crosses back to the previous page");
  check(deck::moveSelection(5, 7, 3, 0, 1) == 6, "down reaches short final row");
  check(deck::moveSelection(6, 7, 3, 0, 1) == 6, "down stops at bottom");
  check(deck::moveSelection(0, 0, 3, 1, 0) == -1, "empty list has no selection");
  for (int count = 1; count <= 80; ++count) {
    for (int columns = 1; columns <= 6; ++columns) {
      for (int current = 0; current < count; ++current) {
        const int directions[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (const auto &direction : directions) {
          const int next = deck::moveSelection(current, count, columns, direction[0], direction[1]);
          check(next >= 0 && next < count, "navigation stays within scene list");
          if (direction[0]) check(next == std::clamp(current + direction[0], 0, count - 1), "horizontal navigation follows scene order");
        }
      }
    }
  }
  const auto widescreen = deck::fit(1920, 1080, 400, 300);
  check(widescreen.width == 400 && widescreen.height == 225 && widescreen.y == 37, "16:9 letterbox");
  const auto portrait = deck::fit(1080, 1920, 400, 300);
  check(portrait.width == 168 && portrait.height == 300 && portrait.x == 116, "portrait pillarbox");
  check(deck::fit(0, 1080, 400, 300).width == 0, "unknown source dimensions are skipped");
  std::cout << "Passed " << checks << " checks\n";
}
