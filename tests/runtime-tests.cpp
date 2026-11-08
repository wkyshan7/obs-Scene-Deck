// Integration tests use the real OBS graphics/source/hotkey runtime. Only the
// frontend is simulated, so no user's OBS profile or scene collection is opened.
#include "scene-deck.hpp"
#include "scene-preview.hpp"
#include "scene-audio-meter.hpp"
#include "hotkey-edit.hpp"
#include <obs-module.h>
#include <util/config-file.h>
#include <util/platform.h>
#include <graphics/vec4.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFrame>
#include <QInputDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QPalette>
#include <QPushButton>
#include <QTimer>
#include <QSpinBox>
#include <QThread>
#include <QTabWidget>
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
std::vector<OBSScene> scenes;
obs_source_t *program = nullptr;
obs_source_t *preview = nullptr;
bool studio = true;
SceneDeck *window = nullptr;
config_t *userConfig = nullptr;
OBSData saved;
int assertions = 0;
std::atomic<int> rendered{0};
std::map<std::string, std::string> translations;

void check(bool condition, const char *message)
{
  ++assertions;
  if (!condition) { std::cerr << "FAILED: " << message << std::endl; std::exit(1); }
}
void pump(int milliseconds = 60)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QApplication::processEvents();
    QThread::msleep(2);
  }
}
void key(int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool repeat = false)
{
  int native = code;
  if (code == Qt::Key_Return || code == Qt::Key_Enter) native = 0x0d;
  else if (code == Qt::Key_Space) native = 0x20;
  else if (code >= Qt::Key_F1 && code <= Qt::Key_F24) native = 0x70 + code - Qt::Key_F1;
  QKeyEvent press(QEvent::KeyPress, code, modifiers, 0, uint32_t(native), 0, QString(), repeat);
  QApplication::sendEvent(window, &press);
  QKeyEvent release(QEvent::KeyRelease, code, modifiers, 0, uint32_t(native), 0);
  QApplication::sendEvent(window, &release);
  pump();
}
obs_hotkey_id namedHotkey(const std::string &name)
{
  struct State { const std::string &name; obs_hotkey_id id = OBS_INVALID_HOTKEY_ID; } state{name};
  obs_enum_hotkeys([](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) {
    auto &state = *static_cast<State *>(data);
    if (state.name == obs_hotkey_get_name(hotkey)) state.id = id;
    return true;
  }, &state);
  return state.id;
}
obs_hotkey_id sceneHotkey(obs_source_t *source)
{
  struct State { std::string name; obs_hotkey_id id = OBS_INVALID_HOTKEY_ID; } state{
    std::string("SceneDeck.Scene.") + obs_source_get_uuid(source)};
  obs_enum_hotkeys([](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) {
    auto &state = *static_cast<State *>(data);
    if (state.name == obs_hotkey_get_name(hotkey)) state.id = id;
    return true;
  }, &state);
  return state.id;
}
ScenePreview *scenePreview(obs_source_t *scene)
{
  return window->findChild<ScenePreview *>(QStringLiteral("preview.%1").arg(obs_source_get_uuid(scene)));
}
void openContextAction(obs_source_t *scene, const QString &actionName)
{
  QTimer::singleShot(30, [actionName] {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    check(menu != nullptr, "right-clicking the native picture opens a context menu");
    auto *action = menu->findChild<QAction *>(actionName);
    check(action != nullptr, "picture context menu exposes the requested action");
    menu->setActiveAction(action);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &enter);
  });
  auto *picture = scenePreview(scene);
  check(picture != nullptr, "visible scene has a native picture");
  const QPoint point = picture->rect().center();
  QContextMenuEvent event(QContextMenuEvent::Mouse, point, picture->mapToGlobal(point));
  QApplication::sendEvent(picture, &event);
}
void editShortcut(obs_source_t *scene, int nativeKey, bool save)
{
  QTimer::singleShot(100, [nativeKey, save] {
    auto *dialog = QApplication::activeModalWidget();
    check(dialog != nullptr, "shortcut editor is modal");
    auto *capture = dynamic_cast<HotkeyCapture *>(dialog->findChild<QLineEdit *>());
    check(capture != nullptr, "shortcut capture field exists");
    const auto modifiers = Qt::ControlModifier | Qt::AltModifier;
    QKeyEvent event(QEvent::KeyPress, nativeKey, modifiers, 0, nativeKey, 0);
    QApplication::sendEvent(capture, &event);
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    check(buttons != nullptr, "shortcut editor has confirmation controls");
    buttons->button(save ? QDialogButtonBox::Save : QDialogButtonBox::Cancel)->click();
  });
  openContextAction(scene, QStringLiteral("editShortcut"));
  pump(180);
}
void editPanelShortcut(const char *editor, int code, bool save = true)
{
  QTimer::singleShot(60, [editor, code, save] {
    auto *settings = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    check(settings != nullptr, "panel shortcut is editable from Settings");
    settings->findChild<QTabWidget *>()->setCurrentIndex(2);
    QTimer::singleShot(40, [code, save] {
      auto *dialog = QApplication::activeModalWidget();
      auto *capture = dynamic_cast<HotkeyCapture *>(dialog->findChild<QLineEdit *>());
      check(capture != nullptr, "panel action has a key capture editor");
      auto *buttons = dialog->findChild<QDialogButtonBox *>();
      if (code == 0) buttons->button(QDialogButtonBox::Reset)->click();
      else {
        QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier, 0, uint32_t(0x70 + code - Qt::Key_F1), 0);
        QApplication::sendEvent(capture, &event);
      }
      buttons->button(save ? QDialogButtonBox::Save : QDialogButtonBox::Cancel)->click();
    });
    settings->findChild<QPushButton *>(QString::fromUtf8(editor))->click();
    settings->accept();
  });
  window->findChild<QPushButton *>("openSettings")->click();
  window->setFocus();
  pump();
}
const char *testName(void *) { return "Scene Deck test color"; }
void *createColor(obs_data_t *, obs_source_t *) { return new int(0); }
void destroyColor(void *value) { delete static_cast<int *>(value); }
uint32_t sourceWidth(void *) { return 640; }
uint32_t sourceHeight(void *) { return 360; }
void renderColor(void *, gs_effect_t *)
{
  ++rendered;
  auto *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
  vec4 color;
  vec4_set(&color, 0.1f, 0.55f, 0.85f, 1.0f);
  gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &color);
  while (gs_effect_loop(effect, "Solid")) gs_draw_sprite(nullptr, 0, 640, 360);
}
void sendAudio(obs_source_t *source, float amplitude)
{
  std::array<float, 1024> samples;
  samples.fill(amplitude);
  obs_source_audio audio = {};
  audio.data[0] = reinterpret_cast<const uint8_t *>(samples.data());
  audio.data[1] = reinterpret_cast<const uint8_t *>(samples.data());
  audio.frames = uint32_t(samples.size());
  audio.speakers = SPEAKERS_STEREO;
  audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
  audio.samples_per_sec = 48000;
  audio.timestamp = os_gettime_ns();
  obs_source_output_audio(source, &audio);
}
} // namespace

extern "C" const char *obs_module_text(const char *key)
{
  auto it = translations.find(key);
  return it == translations.end() ? key : it->second.c_str();
}

extern "C" void obs_frontend_get_scenes(obs_frontend_source_list *list)
{
  for (const auto &scene : scenes) {
    obs_source_t *source = obs_source_get_ref(obs_scene_get_source(scene));
    da_push_back(list->sources, &source);
  }
}
extern "C" obs_source_t *obs_frontend_get_current_scene() { return obs_source_get_ref(program); }
extern "C" void obs_frontend_set_current_scene(obs_source_t *source)
{
  program = source;
  window->frontendEvent(OBS_FRONTEND_EVENT_SCENE_CHANGED);
}
extern "C" bool obs_frontend_preview_program_mode_active() { return studio; }
extern "C" void obs_frontend_set_preview_program_mode(bool enabled) { studio = enabled; }
extern "C" obs_source_t *obs_frontend_get_current_preview_scene() { return studio ? obs_source_get_ref(preview) : nullptr; }
extern "C" void obs_frontend_set_current_preview_scene(obs_source_t *source)
{
  preview = source;
  window->frontendEvent(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
}
extern "C" void obs_frontend_preview_program_trigger_transition()
{
  std::swap(program, preview);
  window->frontendEvent(OBS_FRONTEND_EVENT_TRANSITION_STOPPED);
}
extern "C" obs_source_t *obs_frontend_get_current_transition() { return nullptr; }
extern "C" config_t *obs_frontend_get_user_config() { return userConfig; }
extern "C" void obs_frontend_save() { if (window) window->saveLoad(saved, true); }

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setStyle(QStringLiteral("Fusion"));
  QPalette palette;
  palette.setColor(QPalette::Window, QColor(36, 38, 43));
  palette.setColor(QPalette::WindowText, QColor(232, 233, 236));
  palette.setColor(QPalette::Base, QColor(24, 26, 30));
  palette.setColor(QPalette::Text, QColor(232, 233, 236));
  palette.setColor(QPalette::Button, QColor(52, 55, 63));
  palette.setColor(QPalette::ButtonText, QColor(232, 233, 236));
  palette.setColor(QPalette::Highlight, QColor(62, 112, 190));
  app.setPalette(palette);
  const QStringList args = app.arguments();
  const bool graphics = args.contains(QStringLiteral("--render"));
  const QString obsRoot = args.value(args.indexOf(QStringLiteral("--obs")) + 1, QStringLiteral("C:/Program Files/obs-studio"));
  QSettings locale(QStringLiteral(SCENE_DECK_SOURCE_DIR "/data/locale/zh-CN.ini"), QSettings::IniFormat);
  for (const auto &key : locale.allKeys()) translations[key.toStdString()] = locale.value(key).toString().toStdString();
  check(obs_startup("zh-CN", nullptr, nullptr), "libobs starts");
  obs_hotkey_enable_callback_rerouting(true);
  check(config_open_string(&userConfig, "") == CONFIG_SUCCESS, "isolated frontend config created");
  obs_audio_info audio = {};
  audio.samples_per_sec = 48000;
  audio.speakers = SPEAKERS_STEREO;
  check(obs_reset_audio(&audio), "audio task queue initialized for lifecycle verification");
  config_set_string(userConfig, "General", "HotkeyFocusType", "NeverDisableHotkeys");
  saved = OBSDataAutoRelease(obs_data_create());

  if (graphics) {
    const QByteArray data = (obsRoot + QStringLiteral("/data/libobs/")).toUtf8();
    obs_add_data_path(data.constData());
    obs_video_info info = {};
    const QByteArray module = (obsRoot + QStringLiteral("/bin/64bit/libobs-d3d11.dll")).toUtf8();
    info.graphics_module = module.constData();
    info.fps_num = 30; info.fps_den = 1;
    info.base_width = 1280; info.base_height = 720;
    info.output_width = 1280; info.output_height = 720;
    info.output_format = VIDEO_FORMAT_NV12;
    info.colorspace = VIDEO_CS_709;
    info.range = VIDEO_RANGE_PARTIAL;
    info.scale_type = OBS_SCALE_BICUBIC;
    check(obs_reset_video(&info) == OBS_VIDEO_SUCCESS, "real D3D11 graphics initializes");
  }
  obs_source_info colorInfo = {};
  colorInfo.id = "scene_deck_test_color";
  colorInfo.type = OBS_SOURCE_TYPE_INPUT;
  colorInfo.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
  colorInfo.get_name = testName; colorInfo.create = createColor; colorInfo.destroy = destroyColor;
  colorInfo.get_width = sourceWidth; colorInfo.get_height = sourceHeight; colorInfo.video_render = renderColor;
  obs_register_source(&colorInfo);
  OBSSourceAutoRelease color = obs_source_create("scene_deck_test_color", "Test color", nullptr, nullptr);
  check(bool(color), "test source created");
  for (int i = 0; i < 8; ++i) {
    OBSSceneAutoRelease scene = obs_scene_create(QStringLiteral("机位 %1").arg(i + 1).toUtf8().constData());
    obs_scene_add(scene, color);
    scenes.emplace_back(scene);
  }
  obs_source_info audioInfo = {};
  audioInfo.id = "scene_deck_test_audio";
  audioInfo.type = OBS_SOURCE_TYPE_INPUT;
  audioInfo.output_flags = OBS_SOURCE_AUDIO;
  audioInfo.get_name = testName;
  audioInfo.create = createColor;
  audioInfo.destroy = destroyColor;
  obs_register_source(&audioInfo);
  OBSSourceAutoRelease testAudio = obs_source_create("scene_deck_test_audio", "Meter test audio", nullptr, nullptr);
  OBSSceneAutoRelease nested = obs_scene_create_private("Nested meter scene");
  obs_scene_add(nested, testAudio);
  obs_scene_add(scenes[0], obs_scene_get_source(nested));
  obs_scene_add(scenes[0], testAudio);
  program = obs_scene_get_source(scenes[0]);
  preview = obs_scene_get_source(scenes[1]);
  auto *deck = new SceneDeck;
  window = deck;
  deck->resize(1000, 700);
  deck->saveLoad(saved, false);
  deck->frontendEvent(OBS_FRONTEND_EVENT_FINISHED_LOADING);
  if (!graphics) deck->findChild<QCheckBox *>("livePreviews")->setChecked(false);
  deck->show();
  deck->activateWindow();
  deck->setFocus();
  pump(300);
  check(deck->findChildren<ScenePreview *>().size() == 6, "only six visible scene previews are created");
  check(QApplication::focusWidget() == deck, "panel has keyboard focus");
  check(!deck->findChild<QSpinBox *>("gridColumns")->isVisible() &&
        !deck->findChild<QComboBox *>("switchMode")->isVisible(), "layout and switching controls are hidden in Settings");
  deck->saveLoad(saved, true);
  OBSDataAutoRelease initialConfig = obs_data_get_obj(saved, "obs-scene-deck");
  OBSDataArrayAutoRelease initialFocus = obs_data_get_array(initialConfig, "focusBindings");
  check(obs_data_array_count(initialFocus) == 1, "fresh collection has the default focus shortcut");
  auto *firstPicture = scenePreview(obs_scene_get_source(scenes[0]));
  check(firstPicture->captionName() == QString::fromUtf8("机位 1") && firstPicture->captionNumber() == 1,
        "picture carries its scene name and current-page number captions");
  auto *firstCard = firstPicture->parentWidget()->parentWidget()->parentWidget();
  check(firstCard->findChildren<QPushButton *>().isEmpty(), "cards contain no title or shortcut buttons");
  check(firstCard->findChildren<QLabel *>().isEmpty(), "cards contain no Program/Preview status text");
  check(firstPicture->height() >= firstCard->height() - 16, "live picture fills the card apart from thin borders");
  auto *programRing = firstCard->findChild<QFrame *>("SceneProgramFrame");
  auto *previewRing = firstCard->findChild<QFrame *>("ScenePreviewFrame");
  check(programRing->property("program").toBool() && programRing->styleSheet().contains("#ef4444"),
        "Program is shown with a red frame around the picture");
  check(scenePreview(obs_scene_get_source(scenes[1]))->parentWidget()->property("preview").toBool(),
        "Preview is shown on its own scene frame");
  preview = program;
  deck->frontendEvent(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
  check(programRing->property("program").toBool() && previewRing->property("preview").toBool() &&
        previewRing->styleSheet().contains("#22c55e"), "coincident Program and Preview keep both red and green frames");
  preview = obs_scene_get_source(scenes[1]);
  deck->frontendEvent(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
  auto *firstMeter = firstPicture->parentWidget()->findChild<SceneAudioMeter *>();
  check(firstMeter != nullptr && firstMeter->isVisible(), "audio levels appear beside the picture by default");
  check(firstMeter->monitoredSourceCount() == 1, "nested audio inputs are found and shared inputs deduplicated");
  check(scenePreview(obs_scene_get_source(scenes[1]))->parentWidget()->findChild<SceneAudioMeter *>()->monitoredSourceCount() == 0,
        "scene without audio does not display another scene's audio level");
  for (int i = 0; i < 6; ++i) { sendAudio(testAudio, 0.25f); pump(25); }
  check(firstMeter->channelDb(0) > -20.0f && firstMeter->channelDb(0) < -6.0f,
        "real OBS audio callbacks drive the visible dB meter");
  obs_source_set_muted(testAudio, true);
  for (int i = 0; i < 16; ++i) { sendAudio(testAudio, 0.9f); pump(25); }
  check(firstMeter->channelDb(0) < -25.0f, "muted audio does not drive the meter");
  obs_source_set_muted(testAudio, false);
  const auto beforeSettingsProgram = program;
  QTimer::singleShot(80, [deck] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    check(dialog && dialog->objectName() == QStringLiteral("deckSettings"), "Settings button opens the separate settings window");
    check(deck->findChild<QSpinBox *>("gridColumns")->isVisible(), "layout controls are available in the settings window");
    deck->findChild<QCheckBox *>("showAudioMeters")->setChecked(false);
    check(!deck->findChild<SceneAudioMeter *>()->isVisible() && deck->findChild<SceneAudioMeter *>()->monitoredSourceCount() == 0,
          "turning meters off releases audio callbacks");
    deck->findChild<QComboBox *>("audioMeterSide")->setCurrentIndex(1);
    deck->findChild<QSpinBox *>("audioMeterWidth")->setValue(20);
    deck->findChild<QCheckBox *>("showAudioMeters")->setChecked(true);
    QTimer::singleShot(40, [] {
      auto *edit = QApplication::activeModalWidget();
      auto *capture = dynamic_cast<HotkeyCapture *>(edit ? edit->findChild<QLineEdit *>() : nullptr);
      check(capture != nullptr, "scene shortcut can be edited directly from Settings");
      QKeyEvent event(QEvent::KeyPress, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier, 0, 0x4a, 0);
      QApplication::sendEvent(capture, &event);
      edit->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    deck->findChild<QPushButton *>("editSceneShortcut")->click();
    dialog->accept();
  });
  deck->findChild<QPushButton *>("openSettings")->click();
  pump();
  check(program == beforeSettingsProgram, "editing Settings does not switch Program");
  check(!describeHotkey(sceneHotkey(obs_scene_get_source(scenes[0]))).isEmpty(), "Settings shortcut edit is saved to the scene action");
  check(firstMeter->width() == 20 && firstMeter->x() < firstPicture->x(), "meter width and left-side position apply immediately");
  deck->setFocus();
  key(Qt::Key_6);
  key(Qt::Key_Right);
  check(deck->findChildren<ScenePreview *>().size() == 2 && scenePreview(obs_scene_get_source(scenes[6])) != nullptr,
        "right arrow at page end automatically reveals the next page");
  check(scenePreview(obs_scene_get_source(scenes[6]))->captionNumber() == 1, "new page uses matching local numeric captions");
  key(Qt::Key_Left);
  check(deck->findChildren<ScenePreview *>().size() == 6 && scenePreview(obs_scene_get_source(scenes[5])) != nullptr,
        "left arrow at page start automatically returns to the previous page");
  key(Qt::Key_3);
  check(program == obs_scene_get_source(scenes[0]), "selecting does not change Program in safe mode");
  check(preview == obs_scene_get_source(scenes[1]), "number selects without bypassing Enter");
  key(Qt::Key_Return);
  check(preview == obs_scene_get_source(scenes[2]), "Enter sends selected scene to Preview");
  check(program == obs_scene_get_source(scenes[0]), "Preview operation preserves Program");
  key(Qt::Key_Space);
  check(program == obs_scene_get_source(scenes[2]), "Space takes current Preview");
  editShortcut(obs_scene_get_source(scenes[0]), 0x4b, true);
  check(program == obs_scene_get_source(scenes[2]), "right-click shortcut editing preserves Program");
  check(preview == obs_scene_get_source(scenes[0]), "right-click shortcut editing preserves Preview");
  const QString savedShortcut = describeHotkey(sceneHotkey(obs_scene_get_source(scenes[0])));
  check(!savedShortcut.isEmpty(), "shortcut editor saves a native key combination");
  editShortcut(obs_scene_get_source(scenes[0]), 0x4c, false);
  check(describeHotkey(sceneHotkey(obs_scene_get_source(scenes[0]))) == savedShortcut,
        "cancelling shortcut edit preserves previous binding");

  QTimer::singleShot(100, [] {
    auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
    check(dialog != nullptr, "page-number editor is available from the picture context menu");
    dialog->setIntValue(2);
    dialog->accept();
  });
  openContextAction(obs_scene_get_source(scenes[0]), QStringLiteral("editPosition"));
  pump(180);
  deck->setFocus();
  key(Qt::Key_1);
  key(Qt::Key_Return);
  check(preview == obs_scene_get_source(scenes[1]), "edited page number changes the numeric scene slot");
  check(describeHotkey(sceneHotkey(obs_scene_get_source(scenes[0]))) == savedShortcut,
        "renumbering leaves the fixed scene shortcut attached to its UUID");
  // Restore the initial order through the same user-facing operation.
  QTimer::singleShot(100, [] {
    auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
    check(dialog != nullptr, "page-number editor can restore an earlier position");
    dialog->setIntValue(1);
    dialog->accept();
  });
  openContextAction(obs_scene_get_source(scenes[0]), QStringLiteral("editPosition"));
  pump(180);

  const auto boundScene = obs_scene_get_source(scenes[4]);
  const auto fixed = sceneHotkey(boundScene);
  check(fixed != OBS_INVALID_HOTKEY_ID, "each scene has an OBS-registered action");
  obs_key_combination_t combo = {INTERACT_CONTROL_KEY | INTERACT_ALT_KEY, OBS_KEY_1};
  obs_hotkey_load_bindings(fixed, &combo, 1);
  check(bindingConflicts(combo, OBS_INVALID_HOTKEY_ID).size() == 1, "conflict detector sees registered OBS bindings");
  check(bindingConflicts(combo, fixed).empty(), "editing own binding does not report itself");
  obs_hotkey_trigger_routed_callback(fixed, true);
  pump();
  check(preview == boundScene, "fixed hotkey routes to Preview in safe mode");
  check(program == obs_scene_get_source(scenes[2]), "fixed hotkey preserves Program in safe mode");
  obs_source_set_name(boundScene, "Renamed camera");
  pump(150);
  check(!describeHotkey(sceneHotkey(boundScene)).isEmpty(), "renaming a scene preserves its fixed binding");

  studio = false;
  deck->setFocus();
  key(Qt::Key_1);
  const auto beforeBlocked = program;
  key(Qt::Key_Return);
  check(program == beforeBlocked, "safe mode never falls back to Program outside Studio Mode");
  QTimer::singleShot(60, [deck] {
    auto *dialog = QApplication::activeModalWidget();
    check(dialog != nullptr, "switching Settings opens while Studio Mode is disabled");
    dialog->findChild<QTabWidget *>()->setCurrentIndex(1);
    deck->findChild<QPushButton *>("enableStudio")->click();
    qobject_cast<QDialog *>(dialog)->accept();
  });
  deck->findChild<QPushButton *>("openSettings")->click();
  check(studio, "Studio Mode can be enabled from the settings window");

  key(Qt::Key_PageDown);
  check(deck->findChildren<ScenePreview *>().size() == 2, "last page contains two previews");
  auto *mode = deck->findChild<QComboBox *>("switchMode");
  mode->setCurrentIndex(1);
  deck->setFocus();
  key(Qt::Key_2);
  check(program == obs_scene_get_source(scenes[7]), "direct mode switches scene from numeric slot");
  key(Qt::Key_9);
  check(program == obs_scene_get_source(scenes[7]), "empty numeric slot cannot switch another scene");

  auto *search = deck->findChild<QLineEdit *>("sceneSearch");
  search->setFocus();
  search->setText(QStringLiteral("不存在"));
  pump();
  check(deck->findChildren<ScenePreview *>().empty(), "search with no matches releases previews");
  search->clear();
  pump();
  deck->findChild<QSpinBox *>("gridColumns")->setValue(2);
  deck->saveLoad(saved, true);
  OBSDataAutoRelease config = obs_data_get_obj(saved, "obs-scene-deck");
  check(obs_data_get_int(config, "columns") == 2, "layout is saved inside scene collection data");
  check(obs_data_get_bool(config, "direct"), "switch mode persisted");
  check(obs_data_get_bool(config, "audioMeters") && obs_data_get_int(config, "audioWidth") == 20 &&
        obs_data_get_int(config, "audioSide") == 1, "audio display preferences persist with the scene collection");
  OBSDataAutoRelease snapshot = obs_data_create_from_json(obs_data_get_json(saved));
  deck->findChild<QCheckBox *>("showAudioMeters")->setChecked(false);
  deck->findChild<QComboBox *>("audioMeterSide")->setCurrentIndex(0);
  deck->findChild<QSpinBox *>("audioMeterWidth")->setValue(10);
  deck->findChild<QSpinBox *>("gridColumns")->setValue(4);
  deck->saveLoad(snapshot, false);
  pump();
  check(deck->findChild<QSpinBox *>("gridColumns")->value() == 2, "saved layout is restored");
  check(deck->findChild<QCheckBox *>("showAudioMeters")->isChecked() &&
        deck->findChild<QComboBox *>("audioMeterSide")->currentIndex() == 1 &&
        deck->findChild<QSpinBox *>("audioMeterWidth")->value() == 20, "saved audio display preferences are restored");

  const auto previewAction = namedHotkey("SceneDeck.SetPreview");
  const auto activateAction = namedHotkey("SceneDeck.ActivateSelected");
  const auto takeAction = namedHotkey("SceneDeck.TakePreview");
  check(previewAction != OBS_INVALID_HOTKEY_ID && activateAction != OBS_INVALID_HOTKEY_ID &&
        takeAction != OBS_INVALID_HOTKEY_ID, "all panel actions are registered with OBS");
  editPanelShortcut("editPreviewShortcut", Qt::Key_F6);
  editPanelShortcut("editActivateShortcut", Qt::Key_F7);
  editPanelShortcut("editTakeShortcut", Qt::Key_F8);
  const auto previewBinding = describeHotkey(previewAction);
  check(!previewBinding.isEmpty(), "Preview accepts an unmodified function key");
  editPanelShortcut("editPreviewShortcut", Qt::Key_F9, false);
  check(describeHotkey(previewAction) == previewBinding, "cancelling a panel binding preserves its previous key");
  mode->setCurrentIndex(0);
  deck->setFocus();
  key(Qt::Key_PageUp);
  key(Qt::Key_1);
  preview = obs_scene_get_source(scenes[1]);
  const auto programBeforeAction = program;
  key(Qt::Key_Return);
  key(Qt::Key_Space);
  check(preview == obs_scene_get_source(scenes[1]) && program == programBeforeAction,
        "rebinding actions removes the old Enter and Space behavior");
  key(Qt::Key_F6);
  check(preview == obs_scene_get_source(scenes[0]) && program == programBeforeAction,
        "custom Preview key changes only Preview");
  key(Qt::Key_2);
  key(Qt::Key_F7);
  check(preview == obs_scene_get_source(scenes[1]) && program == programBeforeAction,
        "custom Activate key follows safe mode");
  key(Qt::Key_F8);
  check(program == obs_scene_get_source(scenes[1]), "custom Take key transitions the actual Preview");
  const auto afterTake = program;
  key(Qt::Key_F8, Qt::NoModifier, true);
  check(program == afterTake, "holding the Take key does not repeat transitions");
  mode->setCurrentIndex(1);
  key(Qt::Key_Right);
  key(Qt::Key_F6);
  check(preview == obs_scene_get_source(scenes[2]) && program == afterTake,
        "explicit Preview key remains preview-only in direct mode");
  key(Qt::Key_F7);
  check(program == obs_scene_get_source(scenes[2]), "custom Activate key follows direct mode");
  search->setFocus();
  preview = obs_scene_get_source(scenes[0]);
  const auto previewBeforeTyping = preview;
  obs_hotkey_trigger_routed_callback(previewAction, true);
  pump();
  check(preview == previewBeforeTyping, "registered panel actions do nothing while an input has focus");
  deck->saveLoad(saved, true);
  OBSDataAutoRelease actionSnapshot = obs_data_create_from_json(obs_data_get_json(saved));
  editPanelShortcut("editActivateShortcut", 0);
  check(describeHotkey(activateAction).isEmpty(), "panel action binding can be cleared");
  deck->saveLoad(saved, true);
  OBSDataAutoRelease clearedSnapshot = obs_data_create_from_json(obs_data_get_json(saved));
  deck->saveLoad(actionSnapshot, false);
  check(!describeHotkey(activateAction).isEmpty(), "custom action binding is restored with the collection");
  deck->saveLoad(clearedSnapshot, false);
  check(describeHotkey(activateAction).isEmpty(), "cleared action stays disabled after reloading");
  check(describeHotkey(previewAction) == previewBinding && !describeHotkey(takeAction).isEmpty(),
        "other action bindings survive save and reload");

  if (!graphics) deck->findChild<QCheckBox *>("livePreviews")->setChecked(false);

  if (graphics) {
    deck->findChild<QCheckBox *>("livePreviews")->setChecked(true);
    pump(500);
    check(rendered.load() > 0, "visible cards render real OBS source frames");
    check(obs_source_showing(color), "visible previews maintain showing references");
    deck->hide();
    pump();
    check(!obs_source_showing(color), "hiding panel releases showing references");
    const int afterHide = rendered.load();
    pump(150);
    check(rendered.load() == afterHide, "hidden panel stops rendering");
    deck->show();
    pump(300);
    check(rendered.load() > afterHide, "showing panel resumes rendering");
  }

  deck->setFocus();
  obs_hotkey_trigger_routed_callback(sceneHotkey(boundScene), true);
  obs_hotkey_trigger_routed_callback(previewAction, true);
  const auto previewBeforeChange = preview;
  deck->frontendEvent(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING);
  pump();
  check(preview == previewBeforeChange, "queued hotkey from previous collection is discarded");
  check(deck->findChildren<ScenePreview *>().empty(), "collection change releases all preview widgets");
  if (graphics) check(!obs_source_showing(color), "collection change releases showing references");
  for (const auto &scene : scenes) obs_source_remove(obs_scene_get_source(scene));
  scenes.clear();
  nested = nullptr;
  while (obs_wait_for_destroy_queue()) {}
  program = preview = nullptr;
  OBSDataAutoRelease emptyCollection = obs_data_create();
  deck->saveLoad(emptyCollection, false);
  deck->frontendEvent(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED);
  pump();
  check(deck->findChildren<ScenePreview *>().empty(), "empty new collection has no stale previews");
  check(deck->findChild<QComboBox *>("switchMode")->currentIndex() == 0, "new collection defaults to safe mode");
  deck->shutdown();
  delete deck;
  window = nullptr;
  saved = nullptr;
  obs_source_remove(color);
  color = nullptr;
  obs_source_remove(testAudio);
  testAudio = nullptr;
  while (obs_wait_for_destroy_queue()) {}
  config_close(userConfig);
  obs_shutdown();
  std::cout << "Passed " << assertions << " runtime assertions" << std::endl;
}
