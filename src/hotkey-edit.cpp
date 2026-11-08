#include "hotkey-edit.hpp"
#include "text.hpp"
#include "obs-compat.hpp"
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/dstr.hpp>
#include <QApplication>
#include <QKeyEvent>
#include <QStringList>

namespace {
int modalPauses = 0;
int navigationPauses = 0;
void applyHotkeyPolicy()
{
  const bool active = QGuiApplication::applicationState() == Qt::ApplicationActive;
  obs_hotkey_enable_background_press(hotkeysAllowedByFocusPolicy() &&
                                    modalPauses == 0 && !(active && navigationPauses > 0));
}
} // namespace

bool hotkeysAllowedByFocusPolicy()
{
  const char *policy = config_get_string(sceneDeckUserConfig(), "General", "HotkeyFocusType");
  const bool active = QGuiApplication::applicationState() == Qt::ApplicationActive;
  const QString setting = QString::fromUtf8(policy ? policy : "NeverDisableHotkeys");
  return !((active && setting == QStringLiteral("DisableHotkeysInFocus")) ||
                         (!active && setting == QStringLiteral("DisableHotkeysOutOfFocus")));
}

ScopedHotkeyPause::ScopedHotkeyPause(bool navigationOnly) : navigationOnly_(navigationOnly)
{
  if (navigationOnly_) ++navigationPauses;
  else ++modalPauses;
  applyHotkeyPolicy();
  connect(qApp, &QGuiApplication::applicationStateChanged, this, [] { applyHotkeyPolicy(); });
}

ScopedHotkeyPause::~ScopedHotkeyPause()
{
  disconnect(qApp, nullptr, this, nullptr);
  if (navigationOnly_) --navigationPauses;
  else --modalPauses;
  applyHotkeyPolicy();
}

obs_key_combination_t translateKey(const QKeyEvent *event)
{
  obs_key_combination_t combination = {};
  if (event->modifiers() & Qt::ShiftModifier) combination.modifiers |= INTERACT_SHIFT_KEY;
  if (event->modifiers() & Qt::AltModifier) combination.modifiers |= INTERACT_ALT_KEY;
#ifdef __APPLE__
  // Qt maps Command to ControlModifier and physical Control to MetaModifier.
  if (event->modifiers() & Qt::ControlModifier) combination.modifiers |= INTERACT_COMMAND_KEY;
  if (event->modifiers() & Qt::MetaModifier) combination.modifiers |= INTERACT_CONTROL_KEY;
#else
  if (event->modifiers() & Qt::ControlModifier) combination.modifiers |= INTERACT_CONTROL_KEY;
  if (event->modifiers() & Qt::MetaModifier) combination.modifiers |= INTERACT_COMMAND_KEY;
#endif
  combination.key = obs_key_from_virtual_key(int(event->nativeVirtualKey()));
  return combination;
}

QString describeBinding(obs_key_combination_t combination)
{
  DStr description;
  obs_key_combination_to_str(combination, description);
  return QString::fromUtf8(description->array ? description->array : "");
}

QString describeHotkey(obs_hotkey_id id)
{
  struct State { obs_hotkey_id id; QStringList descriptions; } state{id, {}};
  obs_enum_hotkey_bindings([](void *data, size_t, obs_hotkey_binding_t *binding) {
    auto &state = *static_cast<State *>(data);
    if (obs_hotkey_binding_get_hotkey_id(binding) == state.id)
      state.descriptions.append(describeBinding(obs_hotkey_binding_get_key_combination(binding)));
    return true;
  }, &state);
  return state.descriptions.join(QStringLiteral(" / "));
}

QStringList bindingConflicts(obs_key_combination_t combination, obs_hotkey_id except)
{
  struct State { obs_key_combination_t key; obs_hotkey_id except; QStringList conflicts; } state{combination, except, {}};
  obs_enum_hotkey_bindings([](void *data, size_t, obs_hotkey_binding_t *binding) {
    auto &state = *static_cast<State *>(data);
    const auto current = obs_hotkey_binding_get_key_combination(binding);
    if (current.key == state.key.key && current.modifiers == state.key.modifiers &&
        obs_hotkey_binding_get_hotkey_id(binding) != state.except) {
      state.conflicts.append(QString::fromUtf8(
        obs_hotkey_get_description(obs_hotkey_binding_get_hotkey(binding))));
    }
    return true;
  }, &state);
  state.conflicts.removeDuplicates();
  return state.conflicts;
}

HotkeyCapture::HotkeyCapture(QWidget *parent) : QLineEdit(parent)
{
  setReadOnly(true);
  setPlaceholderText(::text("PressShortcut"));
  setFocusPolicy(Qt::StrongFocus);
}

void HotkeyCapture::clearBinding()
{
  combination_ = {};
  changed_ = true;
  setText(::text("Unbound"));
}

void HotkeyCapture::keyPressEvent(QKeyEvent *event)
{
  event->accept();
  if (event->isAutoRepeat())
    return;
  switch (event->key()) {
  case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt: case Qt::Key_Meta:
    return;
  case Qt::Key_Backspace: case Qt::Key_Delete:
    clearBinding();
    return;
  default: break;
  }
  combination_ = translateKey(event);
  if (combination_.key == OBS_KEY_NONE) {
    setText(::text("UnsupportedKey"));
    changed_ = false;
    return;
  }
  changed_ = true;
  setText(describeBinding(combination_));
}

void HotkeyCapture::keyReleaseEvent(QKeyEvent *event) { event->accept(); }
