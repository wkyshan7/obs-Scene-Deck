#include "native-display.hpp"
#include <QWindow>

#if defined(__linux__) || defined(__FreeBSD__)
#include <obs-nix-platform.h>
#endif

bool sceneDeckNativeWindow(QWindow *window, gs_window &target)
{
  if (!window) return false;
#if defined(_WIN32)
  target.hwnd = reinterpret_cast<void *>(window->winId());
  return target.hwnd != nullptr;
#elif defined(__APPLE__)
  target.view = reinterpret_cast<id>(window->winId());
  return target.view != nullptr;
#elif defined(__linux__) || defined(__FreeBSD__)
  // Match OBSQTDisplay and share the host's existing display connection.
  switch (obs_get_nix_platform()) {
  case OBS_NIX_PLATFORM_X11_EGL:
    target.id = static_cast<uint32_t>(window->winId());
    target.display = obs_get_nix_platform_display();
    return target.id != 0 && target.display != nullptr;
  case OBS_NIX_PLATFORM_WAYLAND:
    target.display = reinterpret_cast<void *>(window->winId());
    return target.display != nullptr;
  default: return false;
  }
#else
  return false;
#endif
}
