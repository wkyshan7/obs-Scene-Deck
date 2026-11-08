#pragma once
#include <obs-frontend-api.h>
#include <util/config-file.h>

#if LIBOBS_API_MAJOR_VER < 30
#error Scene Deck requires OBS Studio 30 or newer.
#endif

inline config_t *sceneDeckUserConfig()
{
#if LIBOBS_API_MAJOR_VER >= 31
  return obs_frontend_get_user_config();
#else
  return obs_frontend_get_global_config();
#endif
}
