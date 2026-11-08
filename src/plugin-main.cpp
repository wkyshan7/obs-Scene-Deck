#include "scene-deck.hpp"
#include "text.hpp"
#include "obs-compat.hpp"
#include <obs-module.h>
#include <QPointer>
#include <QThread>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-scene-deck", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
  return "Live scene cards and keyboard switching for OBS Studio";
}
MODULE_EXPORT const char *obs_module_name(void) { return "Scene Deck"; }

namespace {
QPointer<SceneDeck> dock;
constexpr auto DockId = "obs-scene-deck.dock";

void frontendEvent(obs_frontend_event event, void *)
{
  if (!dock) return;
  if (QThread::currentThread() == dock->thread()) dock->frontendEvent(event);
  else QMetaObject::invokeMethod(dock, [event] { if (dock) dock->frontendEvent(event); }, Qt::BlockingQueuedConnection);
}

void saveLoad(obs_data_t *data, bool saving, void *)
{
  if (dock) dock->saveLoad(data, saving);
}
} // namespace

bool obs_module_load(void)
{
  auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
  if (!mainWindow) return false;
  dock = new SceneDeck(mainWindow);
  if (!obs_frontend_add_dock_by_id(DockId, text("DockTitle").toUtf8().constData(), dock)) {
    delete dock;
    dock = nullptr;
    return false;
  }
  obs_frontend_add_event_callback(frontendEvent, nullptr);
  obs_frontend_add_save_callback(saveLoad, nullptr);
  blog(LOG_INFO, "[Scene Deck] Loaded %s", SCENE_DECK_VERSION);
  return true;
}

void obs_module_unload(void)
{
  obs_frontend_remove_event_callback(frontendEvent, nullptr);
  obs_frontend_remove_save_callback(saveLoad, nullptr);
  if (dock) {
    dock->shutdown();
    obs_frontend_remove_dock(DockId);
    dock = nullptr;
  }
}
