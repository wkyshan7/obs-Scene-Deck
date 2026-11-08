#pragma once
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <QWidget>
#include <memory>
#include <array>
#include <vector>

class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QDialog;
class QHBoxLayout;
class QTabWidget;
class ScenePreview;
class SceneAudioMeter;
class ScopedHotkeyPause;

class SceneDeck final : public QWidget {
  Q_OBJECT
public:
  explicit SceneDeck(QWidget *parent = nullptr);
  ~SceneDeck() override;
  void frontendEvent(obs_frontend_event event);
  void saveLoad(obs_data_t *data, bool saving);
  void shutdown();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  bool event(QEvent *event) override;

private:
  enum PanelAction { SetPreview, ActivateSelected, TakePreview, ActionCount };
  struct Entry {
    SceneDeck *owner = nullptr;
    QString uuid, name;
    OBSWeakSource source;
    obs_hotkey_id hotkey = OBS_INVALID_HOTKEY_ID;
  };
  struct Card {
    QString uuid;
    QFrame *frame = nullptr;
    QFrame *programFrame = nullptr;
    QFrame *previewFrame = nullptr;
    ScenePreview *preview = nullptr;
    SceneAudioMeter *meter = nullptr;
    QHBoxLayout *pictureLayout = nullptr;
    int slot = 0;
  };
  void scheduleRefresh();
  void refreshScenes();
  void clearScenes();
  void clearCards();
  void rebuildCards();
  void updateStates();
  void updateSettings();
  void snapshotBindings();
  void requestSave();
  void select(int index);
  void changePage(int delta);
  void activate(const QString &uuid);
  void setPreview(const QString &uuid);
  void runPanelAction(PanelAction action);
  void loadPanelBindings();
  int matchingPanelAction(obs_key_combination_t combination) const;
  void take();
  void editBinding(const QString &uuid);
  void batchBindings();
  void showSettings();
  void refreshSettingsScenes();
  void applyAudioSettings();
  void editHotkey(obs_hotkey_id id, const QString &name);
  bool canEdit() const;
  void showSceneMenu(const QString &uuid, const QPoint &globalPosition);
  void editPosition(const QString &uuid);
  void saveOrder();
  void showHelp();
  void moveScene(const QString &uuid, int delta);
  void editGroup(const QString &uuid);
  void updateGroups();
  void handleHotkey(const QString &uuid);
  bool actionsBlocked() const;
  Entry *findEntry(const QString &uuid) const;
  int selectedIndex() const;
  QString sceneGroup(const QString &uuid) const;
  void status(const QString &message);
  static void hotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed);
  static void focusHotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed);
  static void panelHotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed);
  static void sourceRenamed(void *data, calldata_t *);

  std::vector<std::unique_ptr<Entry>> entries_;
  std::vector<Entry *> filtered_;
  std::vector<Card> cards_;
  OBSData settings_;
  QWidget *gridWidget_ = nullptr;
  QGridLayout *grid_ = nullptr;
  QLineEdit *search_ = nullptr;
  QComboBox *groups_ = nullptr;
  QComboBox *mode_ = nullptr;
  QSpinBox *columns_ = nullptr;
  QSpinBox *rows_ = nullptr;
  QCheckBox *live_ = nullptr;
  QCheckBox *audioMeters_ = nullptr;
  QComboBox *audioSide_ = nullptr;
  QSpinBox *audioWidth_ = nullptr;
  QDialog *settingsDialog_ = nullptr;
  QComboBox *shortcutScene_ = nullptr;
  QLabel *sceneBinding_ = nullptr;
  QLabel *focusBinding_ = nullptr;
  std::array<QLabel *, ActionCount> actionBindings_{};
  QLabel *pageLabel_ = nullptr;
  QLabel *status_ = nullptr;
  QPushButton *previous_ = nullptr;
  QPushButton *next_ = nullptr;
  QPushButton *take_ = nullptr;
  QPushButton *studio_ = nullptr;
  QTimer *stateTimer_ = nullptr;
  QString selectedUuid_;
  int page_ = 0;
  bool refreshPending_ = false;
  bool changingCollection_ = true;
  bool shuttingDown_ = false;
  bool editing_ = false;
  bool loading_ = false;
  bool transitionRunning_ = false;
  unsigned generation_ = 0;
  obs_hotkey_id focusHotkey_ = OBS_INVALID_HOTKEY_ID;
  std::array<obs_hotkey_id, ActionCount> actionHotkeys_{
    OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID};
  std::unique_ptr<ScopedHotkeyPause> navigationPause_;
};
