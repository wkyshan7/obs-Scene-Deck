#pragma once
#include <obs.h>
#include <QDialog>
#include <QLineEdit>
#include <QStringList>

class ScopedHotkeyPause final : public QObject {
public:
  explicit ScopedHotkeyPause(bool navigationOnly = false);
  ~ScopedHotkeyPause() override;
private:
  bool navigationOnly_;
};

bool hotkeysAllowedByFocusPolicy();
obs_key_combination_t translateKey(const QKeyEvent *event);

class HotkeyCapture final : public QLineEdit {
public:
  explicit HotkeyCapture(QWidget *parent = nullptr);
  obs_key_combination_t combination() const { return combination_; }
  bool changed() const { return changed_; }
  void clearBinding();

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;

private:
  obs_key_combination_t combination_{};
  bool changed_ = false;
};

// Converts via the native key code, preserving keypad and platform mappings.
QString describeBinding(obs_key_combination_t combination);
QString describeHotkey(obs_hotkey_id id);
QStringList bindingConflicts(obs_key_combination_t combination, obs_hotkey_id except);
