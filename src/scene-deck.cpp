#include "scene-deck.hpp"
#include "core/layout.hpp"
#include "hotkey-edit.hpp"
#include "scene-preview.hpp"
#include "scene-audio-meter.hpp"
#include "text.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr auto ConfigKey = "obs-scene-deck";
constexpr const char *ActionNames[] = {"SceneDeck.SetPreview", "SceneDeck.ActivateSelected", "SceneDeck.TakePreview"};
constexpr const char *ActionLabels[] = {"SetPreviewAction", "ActivateAction", "TakeAction"};
constexpr const char *ActionKeys[] = {"previewBindings", "activateBindings", "takeBindings"};
constexpr const char *ActionEditors[] = {"editPreviewShortcut", "editActivateShortcut", "editTakeShortcut"};
QPushButton *button(const QString &label, QWidget *parent)
{
  auto *result = new QPushButton(label, parent);
  result->setFocusPolicy(Qt::NoFocus);
  return result;
}
QString sourceUuid(obs_source_t *source)
{
  return source ? QString::fromUtf8(obs_source_get_uuid(source)) : QString();
}
} // namespace

SceneDeck::SceneDeck(QWidget *parent) : QWidget(parent)
{
  settings_ = OBSDataAutoRelease(obs_data_create());
  setObjectName(QStringLiteral("SceneDeck"));
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(320, 300);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(4);

  auto *filterRow = new QHBoxLayout;
  search_ = new QLineEdit(this);
  search_->setObjectName(QStringLiteral("sceneSearch"));
  search_->setPlaceholderText(text("SearchScenes"));
  groups_ = new QComboBox(this);
  groups_->setObjectName(QStringLiteral("sceneGroups"));
  groups_->addItem(text("AllGroups"), QString());
  groups_->setFocusPolicy(Qt::NoFocus);
  auto *settingsButton = button(text("Settings"), this);
  settingsButton->setObjectName(QStringLiteral("openSettings"));
  filterRow->addWidget(search_, 1);
  filterRow->addWidget(groups_);
  filterRow->addWidget(settingsButton);
  layout->addLayout(filterRow);

  settingsDialog_ = new QDialog(this);
  settingsDialog_->setObjectName(QStringLiteral("deckSettings"));
  settingsDialog_->setWindowTitle(text("SettingsTitle"));
  settingsDialog_->resize(560, 470);
  auto *settingsLayout = new QVBoxLayout(settingsDialog_);
  auto *tabs = new QTabWidget(settingsDialog_);
  settingsLayout->addWidget(tabs);
  auto *displayPage = new QWidget(tabs);
  auto *displayForm = new QFormLayout(displayPage);
  auto *switchPage = new QWidget(tabs);
  auto *switchForm = new QFormLayout(switchPage);
  auto *shortcutPage = new QWidget(tabs);
  auto *shortcutForm = new QFormLayout(shortcutPage);
  auto *audioPage = new QWidget(tabs);
  auto *audioForm = new QFormLayout(audioPage);
  tabs->addTab(displayPage, text("DisplaySettings"));
  tabs->addTab(switchPage, text("SwitchSettings"));
  tabs->addTab(shortcutPage, text("ShortcutSettings"));
  tabs->addTab(audioPage, text("AudioSettings"));
  mode_ = new QComboBox(switchPage);
  mode_->setObjectName(QStringLiteral("switchMode"));
  mode_->addItem(text("SafeMode"));
  mode_->addItem(text("DirectMode"));
  columns_ = new QSpinBox(displayPage);
  columns_->setObjectName(QStringLiteral("gridColumns"));
  columns_->setRange(1, 6);
  columns_->setValue(3);
  rows_ = new QSpinBox(displayPage);
  rows_->setObjectName(QStringLiteral("gridRows"));
  rows_->setRange(1, 4);
  rows_->setValue(2);
  live_ = new QCheckBox(text("LivePreviews"), displayPage);
  live_->setObjectName(QStringLiteral("livePreviews"));
  live_->setChecked(true);
  displayForm->addRow(text("Columns"), columns_);
  displayForm->addRow(text("Rows"), rows_);
  displayForm->addRow(live_);
  auto *legend = new QLabel(text("FrameLegend"), displayPage);
  legend->setWordWrap(true);
  displayForm->addRow(legend);
  switchForm->addRow(text("SwitchMode"), mode_);
  studio_ = new QPushButton(text("EnableStudio"), switchPage);
  studio_->setObjectName(QStringLiteral("enableStudio"));
  switchForm->addRow(studio_);
  auto *switchHint = new QLabel(text("SwitchHint"), switchPage);
  switchHint->setWordWrap(true);
  switchForm->addRow(switchHint);

  auto *focusRow = new QWidget(shortcutPage);
  auto *focusLayout = new QHBoxLayout(focusRow);
  focusLayout->setContentsMargins(0, 0, 0, 0);
  focusBinding_ = new QLabel(focusRow);
  auto *editFocus = new QPushButton(text("Edit"), focusRow);
  editFocus->setObjectName(QStringLiteral("editFocusShortcut"));
  focusLayout->addWidget(focusBinding_, 1);
  focusLayout->addWidget(editFocus);
  shortcutForm->addRow(text("FocusPanel"), focusRow);
  for (int index = 0; index < ActionCount; ++index) {
    auto *row = new QWidget(shortcutPage);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    actionBindings_[index] = new QLabel(row);
    auto *edit = new QPushButton(text("Edit"), row);
    edit->setObjectName(QString::fromUtf8(ActionEditors[index]));
    rowLayout->addWidget(actionBindings_[index], 1);
    rowLayout->addWidget(edit);
    shortcutForm->addRow(text(ActionLabels[index]), row);
    connect(edit, &QPushButton::clicked, this, [this, index] {
      editHotkey(actionHotkeys_[index], text(ActionLabels[index]));
    });
  }
  shortcutScene_ = new QComboBox(shortcutPage);
  shortcutScene_->setObjectName(QStringLiteral("shortcutScene"));
  shortcutForm->addRow(text("Scene"), shortcutScene_);
  auto *bindingRow = new QWidget(shortcutPage);
  auto *bindingLayout = new QHBoxLayout(bindingRow);
  bindingLayout->setContentsMargins(0, 0, 0, 0);
  sceneBinding_ = new QLabel(bindingRow);
  auto *editScene = new QPushButton(text("Edit"), bindingRow);
  editScene->setObjectName(QStringLiteral("editSceneShortcut"));
  bindingLayout->addWidget(sceneBinding_, 1);
  bindingLayout->addWidget(editScene);
  shortcutForm->addRow(text("ShortcutSettings"), bindingRow);
  auto *batch = new QPushButton(text("BatchBindings"), shortcutPage);
  batch->setObjectName(QStringLiteral("batchShortcuts"));
  shortcutForm->addRow(batch);
  auto *keyboardHelp = new QPushButton(text("Help"), shortcutPage);
  shortcutForm->addRow(keyboardHelp);

  audioMeters_ = new QCheckBox(text("ShowAudioMeters"), audioPage);
  audioMeters_->setObjectName(QStringLiteral("showAudioMeters"));
  audioMeters_->setChecked(true);
  audioSide_ = new QComboBox(audioPage);
  audioSide_->setObjectName(QStringLiteral("audioMeterSide"));
  audioSide_->addItem(text("RightSide"));
  audioSide_->addItem(text("LeftSide"));
  audioWidth_ = new QSpinBox(audioPage);
  audioWidth_->setObjectName(QStringLiteral("audioMeterWidth"));
  audioWidth_->setRange(8, 28);
  audioWidth_->setValue(14);
  audioWidth_->setSuffix(QStringLiteral(" px"));
  audioForm->addRow(audioMeters_);
  audioForm->addRow(text("MeterSide"), audioSide_);
  audioForm->addRow(text("MeterWidth"), audioWidth_);
  auto *audioHint = new QLabel(text("AudioMeterHint"), audioPage);
  audioHint->setWordWrap(true);
  audioForm->addRow(audioHint);
  auto *autoSave = new QLabel(text("SettingsAutoSave"), settingsDialog_);
  autoSave->setWordWrap(true);
  settingsLayout->addWidget(autoSave);
  auto *closeSettings = new QDialogButtonBox(QDialogButtonBox::Close, settingsDialog_);
  settingsLayout->addWidget(closeSettings);
  connect(closeSettings, &QDialogButtonBox::rejected, settingsDialog_, &QDialog::reject);
  connect(settingsButton, &QPushButton::clicked, this, [this] { showSettings(); });
  connect(editFocus, &QPushButton::clicked, this, [this] { editHotkey(focusHotkey_, text("FocusPanel")); });
  connect(editScene, &QPushButton::clicked, this, [this] { editBinding(shortcutScene_->currentData().toString()); });
  connect(shortcutScene_, &QComboBox::currentIndexChanged, this, [this] { updateStates(); });
  connect(batch, &QPushButton::clicked, this, [this] { batchBindings(); });
  connect(keyboardHelp, &QPushButton::clicked, this, [this] { showHelp(); });
  connect(audioMeters_, &QCheckBox::toggled, this, [this] { applyAudioSettings(); updateSettings(); requestSave(); });
  connect(audioSide_, &QComboBox::currentIndexChanged, this, [this] { applyAudioSettings(); updateSettings(); requestSave(); });
  connect(audioWidth_, &QSpinBox::valueChanged, this, [this] { applyAudioSettings(); updateSettings(); requestSave(); });

  gridWidget_ = new QWidget(this);
  gridWidget_->setFocusPolicy(Qt::NoFocus);
  grid_ = new QGridLayout(gridWidget_);
  grid_->setContentsMargins(0, 0, 0, 0);
  grid_->setSpacing(4);
  gridWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(gridWidget_, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
    showSceneMenu(QString(), gridWidget_->mapToGlobal(position));
  });
  layout->addWidget(gridWidget_, 1);

  auto *navigation = new QHBoxLayout;
  previous_ = button(text("PreviousPage"), this);
  next_ = button(text("NextPage"), this);
  pageLabel_ = new QLabel(this);
  pageLabel_->setAlignment(Qt::AlignCenter);
  navigation->addWidget(previous_);
  navigation->addWidget(pageLabel_, 1);
  navigation->addWidget(next_);
  take_ = button(text("Take"), this);
  navigation->addWidget(take_);
  layout->addLayout(navigation);
  status_ = new QLabel(text("Loading"), this);
  status_->setWordWrap(true);
  layout->addWidget(status_);

  connect(search_, &QLineEdit::textChanged, this, [this] { page_ = 0; rebuildCards(); });
  connect(groups_, &QComboBox::currentIndexChanged, this, [this] {
    page_ = 0; updateSettings(); rebuildCards(); requestSave();
  });
  connect(columns_, &QSpinBox::valueChanged, this, [this] {
    page_ = 0; updateSettings(); rebuildCards(); requestSave();
  });
  connect(rows_, &QSpinBox::valueChanged, this, [this] {
    page_ = 0; updateSettings(); rebuildCards(); requestSave();
  });
  connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
    updateSettings(); updateStates(); requestSave();
  });
  connect(live_, &QCheckBox::toggled, this, [this](bool enabled) {
    for (auto &card : cards_) card.preview->setPreviewEnabled(enabled);
    updateSettings(); requestSave();
  });
  connect(previous_, &QPushButton::clicked, this, [this] { changePage(-1); setFocus(); });
  connect(next_, &QPushButton::clicked, this, [this] { changePage(1); setFocus(); });
  connect(take_, &QPushButton::clicked, this, [this] { take(); setFocus(); });
  connect(studio_, &QPushButton::clicked, this, [this] {
    if (canEdit()) obs_frontend_set_preview_program_mode(true);
    updateStates();
  });
  stateTimer_ = new QTimer(this);
  stateTimer_->setInterval(500);
  connect(stateTimer_, &QTimer::timeout, this, [this] { updateStates(); });
  qApp->installEventFilter(this);
  signal_handler_connect(obs_get_signal_handler(), "source_rename", sourceRenamed, this);
  focusHotkey_ = obs_hotkey_register_frontend("SceneDeck.Focus", text("FocusHotkey").toUtf8().constData(), focusHotkeyCallback, this);
  obs_key_combination_t focusBinding = {INTERACT_CONTROL_KEY | INTERACT_ALT_KEY, OBS_KEY_D};
  if (bindingConflicts(focusBinding, focusHotkey_).isEmpty())
    obs_hotkey_load_bindings(focusHotkey_, &focusBinding, 1);
  for (int index = 0; index < ActionCount; ++index)
    actionHotkeys_[index] = obs_hotkey_register_frontend(ActionNames[index], text(ActionLabels[index]).toUtf8().constData(), panelHotkeyCallback, this);
  loadPanelBindings();
}

SceneDeck::~SceneDeck()
{
  shutdown();
}

void SceneDeck::shutdown()
{
  if (shuttingDown_) return;
  shuttingDown_ = true;
  ++generation_;
  stateTimer_->stop();
  navigationPause_.reset();
  qApp->removeEventFilter(this);
  signal_handler_disconnect(obs_get_signal_handler(), "source_rename", sourceRenamed, this);
  if (focusHotkey_ != OBS_INVALID_HOTKEY_ID) {
    obs_hotkey_unregister(focusHotkey_);
    focusHotkey_ = OBS_INVALID_HOTKEY_ID;
  }
  for (auto &id : actionHotkeys_) {
    if (id != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(id);
    id = OBS_INVALID_HOTKEY_ID;
  }
  clearScenes();
}

void SceneDeck::sourceRenamed(void *data, calldata_t *)
{
  auto *deck = static_cast<SceneDeck *>(data);
  QMetaObject::invokeMethod(deck, [deck] { deck->scheduleRefresh(); }, Qt::QueuedConnection);
}

void SceneDeck::scheduleRefresh()
{
  if (shuttingDown_ || changingCollection_ || refreshPending_) return;
  refreshPending_ = true;
  QTimer::singleShot(0, this, [this] {
    refreshPending_ = false;
    if (editing_) {
      // Keep card widgets and hotkey contexts stable for modal editors.
      QTimer::singleShot(100, this, [this] { scheduleRefresh(); });
      return;
    }
    if (!shuttingDown_ && !changingCollection_) refreshScenes();
  });
}

void SceneDeck::frontendEvent(obs_frontend_event event)
{
  switch (event) {
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
    changingCollection_ = true;
    ++generation_;
    snapshotBindings();
    clearScenes();
    status(text("Loading"));
    break;
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
    clearScenes();
    break;
  case OBS_FRONTEND_EVENT_FINISHED_LOADING:
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
    changingCollection_ = false;
    transitionRunning_ = false;
    scheduleRefresh();
    break;
  case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
    scheduleRefresh();
    break;
  case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
    transitionRunning_ = false;
    updateStates();
    break;
  case OBS_FRONTEND_EVENT_SCENE_CHANGED:
  case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
  case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
  case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
    updateStates();
    break;
  case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
  case OBS_FRONTEND_EVENT_EXIT:
    shutdown();
    break;
  default: break;
  }
}

void SceneDeck::saveLoad(obs_data_t *collectionData, bool saving)
{
  if (shuttingDown_) return;
  if (saving) {
    snapshotBindings();
    updateSettings();
    obs_data_set_obj(collectionData, ConfigKey, settings_);
    return;
  }
  QScopedValueRollback<bool> loading(loading_, true);
  ++generation_;
  clearScenes();
  settings_ = OBSDataAutoRelease(obs_data_get_obj(collectionData, ConfigKey));
  if (!settings_) settings_ = OBSDataAutoRelease(obs_data_create());
  loadPanelBindings();
  if (obs_data_has_user_value(settings_, "focusBindings")) {
    OBSDataArrayAutoRelease array = obs_data_get_array(settings_, "focusBindings");
    obs_hotkey_load(focusHotkey_, array);
  } else {
    obs_key_combination_t binding = {INTERACT_CONTROL_KEY | INTERACT_ALT_KEY, OBS_KEY_D};
    if (bindingConflicts(binding, focusHotkey_).isEmpty()) obs_hotkey_load_bindings(focusHotkey_, &binding, 1);
    else obs_hotkey_load_bindings(focusHotkey_, nullptr, 0);
  }
  obs_data_set_default_int(settings_, "columns", 3);
  obs_data_set_default_int(settings_, "rows", 2);
  obs_data_set_default_bool(settings_, "live", true);
  obs_data_set_default_bool(settings_, "audioMeters", true);
  obs_data_set_default_int(settings_, "audioWidth", 14);
  const QSignalBlocker a(columns_), b(rows_), c(mode_), d(live_), e(groups_), f(search_);
  const QSignalBlocker g(audioMeters_), h(audioSide_), i(audioWidth_);
  columns_->setValue(int(obs_data_get_int(settings_, "columns")));
  rows_->setValue(int(obs_data_get_int(settings_, "rows")));
  mode_->setCurrentIndex(obs_data_get_bool(settings_, "direct") ? 1 : 0);
  live_->setChecked(obs_data_get_bool(settings_, "live"));
  audioMeters_->setChecked(obs_data_get_bool(settings_, "audioMeters"));
  audioSide_->setCurrentIndex(obs_data_get_int(settings_, "audioSide") == 1 ? 1 : 0);
  audioWidth_->setValue(int(obs_data_get_int(settings_, "audioWidth")));
  search_->clear();
  page_ = 0;
  selectedUuid_ = QString::fromUtf8(obs_data_get_string(settings_, "selected"));
  applyAudioSettings();
  if (!changingCollection_) scheduleRefresh();
}

void SceneDeck::updateSettings()
{
  if (loading_ || !settings_) return;
  obs_data_set_int(settings_, "version", 3);
  obs_data_set_int(settings_, "columns", columns_->value());
  obs_data_set_int(settings_, "rows", rows_->value());
  obs_data_set_bool(settings_, "direct", mode_->currentIndex() == 1);
  obs_data_set_bool(settings_, "live", live_->isChecked());
  obs_data_set_bool(settings_, "audioMeters", audioMeters_->isChecked());
  obs_data_set_int(settings_, "audioSide", audioSide_->currentIndex());
  obs_data_set_int(settings_, "audioWidth", audioWidth_->value());
  obs_data_set_string(settings_, "selected", selectedUuid_.toUtf8().constData());
  obs_data_set_string(settings_, "group", groups_->currentData().toString().toUtf8().constData());
}

void SceneDeck::snapshotBindings()
{
  if (!settings_) return;
  for (int index = 0; index < ActionCount; ++index) {
    if (actionHotkeys_[index] == OBS_INVALID_HOTKEY_ID) continue;
    OBSDataArrayAutoRelease bindings = obs_hotkey_save(actionHotkeys_[index]);
    obs_data_set_array(settings_, ActionKeys[index], bindings);
  }
  if (focusHotkey_ != OBS_INVALID_HOTKEY_ID) {
    OBSDataArrayAutoRelease focusBindings = obs_hotkey_save(focusHotkey_);
    obs_data_set_array(settings_, "focusBindings", focusBindings);
  }
  if (entries_.empty()) return;
  OBSDataAutoRelease bindings = obs_data_get_obj(settings_, "bindings");
  if (!bindings) bindings = obs_data_create();
  for (const auto &entry : entries_) {
    OBSDataArrayAutoRelease array = obs_hotkey_save(entry->hotkey);
    obs_data_set_array(bindings, entry->uuid.toUtf8().constData(), array);
  }
  obs_data_set_obj(settings_, "bindings", bindings);
}

void SceneDeck::requestSave()
{
  if (!loading_ && !changingCollection_ && !shuttingDown_) obs_frontend_save();
}

void SceneDeck::clearCards()
{
  cards_.clear();
  while (auto *item = grid_->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  for (int i = 0; i < 6; ++i) grid_->setColumnStretch(i, 0);
  for (int i = 0; i < 4; ++i) grid_->setRowStretch(i, 0);
}

void SceneDeck::clearScenes()
{
  clearCards();
  filtered_.clear();
  for (auto &entry : entries_) {
    if (entry->hotkey != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(entry->hotkey);
  }
  entries_.clear();
}

void SceneDeck::refreshScenes()
{
  ++generation_;
  snapshotBindings();
  clearScenes();
  obs_frontend_source_list sources = {};
  obs_frontend_get_scenes(&sources);
  OBSDataAutoRelease bindings = obs_data_get_obj(settings_, "bindings");
  for (size_t i = 0; i < sources.sources.num; ++i) {
    auto *source = sources.sources.array[i];
    auto entry = std::make_unique<Entry>();
    entry->owner = this;
    entry->uuid = sourceUuid(source);
    entry->name = QString::fromUtf8(obs_source_get_name(source));
    entry->source = OBSGetWeakRef(source);
    const QByteArray key = QStringLiteral("SceneDeck.Scene.%1").arg(entry->uuid).toUtf8();
    const QByteArray description = text("SceneHotkey").arg(entry->name).toUtf8();
    entry->hotkey = obs_hotkey_register_frontend(key.constData(), description.constData(), hotkeyCallback, entry.get());
    if (bindings) {
      OBSDataArrayAutoRelease array = obs_data_get_array(bindings, entry->uuid.toUtf8().constData());
      if (array) obs_hotkey_load(entry->hotkey, array);
    }
    entries_.push_back(std::move(entry));
  }
  obs_frontend_source_list_free(&sources);
  OBSDataArrayAutoRelease order = obs_data_get_array(settings_, "order");
  if (order) {
    QStringList uuids;
    for (size_t i = 0; i < obs_data_array_count(order); ++i) {
      OBSDataAutoRelease item = obs_data_array_item(order, i);
      uuids.append(QString::fromUtf8(obs_data_get_string(item, "uuid")));
    }
    std::stable_sort(entries_.begin(), entries_.end(), [&uuids](const auto &a, const auto &b) {
      int ai = uuids.indexOf(a->uuid), bi = uuids.indexOf(b->uuid);
      if (ai < 0) ai = int(uuids.size());
      if (bi < 0) bi = int(uuids.size());
      return ai < bi;
    });
  }
  updateGroups();
  refreshSettingsScenes();
  rebuildCards();
  status(text("Ready"));
}

QString SceneDeck::sceneGroup(const QString &uuid) const
{
  OBSDataAutoRelease groups = obs_data_get_obj(settings_, "groups");
  return groups ? QString::fromUtf8(obs_data_get_string(groups, uuid.toUtf8().constData())) : QString();
}

void SceneDeck::updateGroups()
{
  const QString selected = QString::fromUtf8(obs_data_get_string(settings_, "group"));
  const QSignalBlocker block(groups_);
  groups_->clear();
  groups_->addItem(text("AllGroups"), QString());
  QStringList groups;
  for (const auto &entry : entries_) {
    const QString group = sceneGroup(entry->uuid);
    if (!group.isEmpty() && !groups.contains(group)) groups.append(group);
  }
  groups.sort();
  for (const auto &group : groups) groups_->addItem(group, group);
  const int index = groups_->findData(selected);
  groups_->setCurrentIndex(index < 0 ? 0 : index);
}

void SceneDeck::rebuildCards()
{
  clearCards();
  filtered_.clear();
  if (changingCollection_ || shuttingDown_) return;
  const QString query = search_->text().trimmed();
  const QString group = groups_->currentData().toString();
  for (auto &entry : entries_) {
    if (entry->name.contains(query, Qt::CaseInsensitive) && (group.isEmpty() || sceneGroup(entry->uuid) == group))
      filtered_.push_back(entry.get());
  }
  const int pageSize = columns_->value() * rows_->value();
  page_ = deck::clampPage(page_, int(filtered_.size()), pageSize);
  if (selectedIndex() < 0 && !filtered_.empty()) selectedUuid_ = filtered_[page_ * pageSize]->uuid;
  for (int slot = 0; slot < pageSize; ++slot) {
    const int index = deck::slotIndex(page_, pageSize, slot, int(filtered_.size()));
    if (index < 0) break;
    Entry *entry = filtered_[index];
    OBSSource source = OBSGetStrongRef(entry->source);
    if (!source) continue;
    Card card;
    card.uuid = entry->uuid;
    card.frame = new QFrame(gridWidget_);
    card.frame->setObjectName(QStringLiteral("SceneCard"));
    card.frame->setProperty("sceneUuid", entry->uuid);
    card.slot = slot;
    auto *contents = new QVBoxLayout(card.frame);
    contents->setContentsMargins(0, 0, 0, 0);
    contents->setSpacing(0);
    // Both frames reserve their border width, so state changes never resize
    // native displays. A scene in both Program and Preview shows both rings.
    card.programFrame = new QFrame(card.frame);
    card.programFrame->setObjectName(QStringLiteral("SceneProgramFrame"));
    auto *programContents = new QVBoxLayout(card.programFrame);
    programContents->setContentsMargins(0, 0, 0, 0);
    programContents->setSpacing(0);
    card.previewFrame = new QFrame(card.programFrame);
    card.previewFrame->setObjectName(QStringLiteral("ScenePreviewFrame"));
    auto *previewContents = new QHBoxLayout(card.previewFrame);
    card.pictureLayout = previewContents;
    previewContents->setContentsMargins(0, 0, 0, 0);
    previewContents->setSpacing(2);
    card.preview = new ScenePreview(source, card.previewFrame);
    card.preview->setObjectName(QStringLiteral("preview.%1").arg(entry->uuid));
    card.preview->setCaption(entry->name, slot + 1);
    card.preview->setPreviewEnabled(live_->isChecked());
    const QString uuid = entry->uuid;
    card.preview->clicked = [this, uuid] {
      for (int i = 0; i < int(filtered_.size()); ++i) if (filtered_[i]->uuid == uuid) select(i);
      setFocus(); activate(uuid);
    };
    card.preview->doubleClicked = [this, uuid] {
      // Double click has exactly the same routing as a single click; safe mode
      // never escalates a double click to Program.
      activate(uuid);
    };
    card.preview->failed = [this] { status(text("PreviewFailed")); };
    previewContents->addWidget(card.preview, 1);
    card.meter = new SceneAudioMeter(source, card.previewFrame);
    previewContents->addWidget(card.meter);
    card.meter->setFixedWidth(audioWidth_->value());
    card.meter->setVisible(audioMeters_->isChecked());
    previewContents->setDirection(audioSide_->currentIndex() == 1 ? QBoxLayout::RightToLeft : QBoxLayout::LeftToRight);
    programContents->addWidget(card.previewFrame);
    contents->addWidget(card.programFrame);
    // The video has its own native window, so attach the menu directly to it
    // as well as the frame edges; right-clicking must never activate a scene.
    for (QWidget *surface : {static_cast<QWidget *>(card.frame), static_cast<QWidget *>(card.programFrame),
                             static_cast<QWidget *>(card.previewFrame), static_cast<QWidget *>(card.preview)}) {
      surface->setContextMenuPolicy(Qt::CustomContextMenu);
      connect(surface, &QWidget::customContextMenuRequested, this, [this, uuid, surface](const QPoint &position) {
        showSceneMenu(uuid, surface->mapToGlobal(position));
      });
    }
    grid_->addWidget(card.frame, slot / columns_->value(), slot % columns_->value());
    cards_.push_back(card);
  }
  for (int i = 0; i < columns_->value(); ++i) grid_->setColumnStretch(i, 1);
  for (int i = 0; i < rows_->value(); ++i) grid_->setRowStretch(i, 1);
  if (filtered_.empty()) {
    auto *empty = new QLabel(text("NoScenes"), gridWidget_);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    grid_->addWidget(empty, 0, 0);
  }
  pageLabel_->setText(text("PageCount").arg(page_ + 1).arg(deck::pageCount(int(filtered_.size()), pageSize)).arg(filtered_.size()));
  previous_->setEnabled(page_ > 0);
  next_->setEnabled(page_ + 1 < deck::pageCount(int(filtered_.size()), pageSize));
  updateStates();
}

void SceneDeck::updateStates()
{
  if (shuttingDown_ || changingCollection_) return;
  OBSSourceAutoRelease program = obs_frontend_get_current_scene();
  OBSSourceAutoRelease preview = obs_frontend_get_current_preview_scene();
  const QString programUuid = sourceUuid(program), previewUuid = sourceUuid(preview);
  const bool studio = obs_frontend_preview_program_mode_active();
  // A stopped/cut transition may not emit an event after a rapid UI action.
  OBSSourceAutoRelease transition = obs_frontend_get_current_transition();
  const float progress = transition ? obs_transition_get_time(transition) : 1.0f;
  if (transitionRunning_ && (progress <= 0.0f || progress >= 1.0f)) transitionRunning_ = false;
  studio_->setVisible(!studio);
  take_->setEnabled(studio && !transitionRunning_);
  focusBinding_->setText(describeHotkey(focusHotkey_));
  for (int index = 0; index < ActionCount; ++index) {
    const QString binding = describeHotkey(actionHotkeys_[index]);
    actionBindings_[index]->setText(binding.isEmpty() ? text("Unbound") : binding);
  }
  const QString takeBinding = describeHotkey(actionHotkeys_[TakePreview]);
  take_->setText(takeBinding.isEmpty() ? text("TakeButton") : text("TakeButton") + QStringLiteral(" · ") + takeBinding);
  if (auto *entry = findEntry(shortcutScene_->currentData().toString()))
    sceneBinding_->setText(describeHotkey(entry->hotkey).isEmpty() ? text("Unbound") : describeHotkey(entry->hotkey));
  else sceneBinding_->setText(text("Unbound"));
  for (auto &card : cards_) {
    const bool isProgram = card.uuid == programUuid;
    const bool isPreview = card.uuid == previewUuid;
    const bool isSelected = card.uuid == selectedUuid_;
    card.frame->setProperty("keyboardSelected", isSelected);
    card.programFrame->setProperty("program", isProgram);
    card.previewFrame->setProperty("preview", isPreview);
    card.frame->setStyleSheet(QStringLiteral("QFrame#SceneCard { border: 1px solid %1; }")
      .arg(isSelected ? QStringLiteral("#eab308") : QStringLiteral("transparent")));
    card.programFrame->setStyleSheet(QStringLiteral("QFrame#SceneProgramFrame { border: 3px solid %1; }")
      .arg(isProgram ? QStringLiteral("#ef4444") : QStringLiteral("transparent")));
    card.previewFrame->setStyleSheet(QStringLiteral("QFrame#ScenePreviewFrame { border: 3px solid %1; }")
      .arg(isPreview ? QStringLiteral("#22c55e") : QStringLiteral("transparent")));
    if (auto *entry = findEntry(card.uuid)) {
      card.preview->setCaption(entry->name, card.slot + 1);
      const QString binding = describeHotkey(entry->hotkey);
      QStringList info{entry->name, text("PagePosition").arg(card.slot + 1)};
      if (card.slot < 9) info.append(text("NumberKey").arg(card.slot + 1));
      if (!binding.isEmpty()) info.append(text("ShortcutValue").arg(binding));
      info.append(text("RightClickEdit"));
      card.preview->setToolTip(info.join(QLatin1Char('\n')));
      card.preview->setAccessibleName(entry->name);
      OBSSource source = OBSGetStrongRef(entry->source);
      if (source && QString::fromUtf8(obs_source_get_name(source)) != entry->name) scheduleRefresh();
    }
  }
}

SceneDeck::Entry *SceneDeck::findEntry(const QString &uuid) const
{
  for (const auto &entry : entries_) if (entry->uuid == uuid) return entry.get();
  return nullptr;
}

int SceneDeck::selectedIndex() const
{
  for (int i = 0; i < int(filtered_.size()); ++i) if (filtered_[i]->uuid == selectedUuid_) return i;
  return -1;
}

void SceneDeck::select(int index)
{
  if (index < 0 || index >= int(filtered_.size())) return;
  selectedUuid_ = filtered_[index]->uuid;
  const int page = index / (columns_->value() * rows_->value());
  if (page != page_) { page_ = page; rebuildCards(); }
  else updateStates();
  updateSettings();
}

void SceneDeck::changePage(int delta)
{
  const int pageSize = columns_->value() * rows_->value();
  page_ = deck::clampPage(page_ + delta, int(filtered_.size()), pageSize);
  const int first = deck::slotIndex(page_, pageSize, 0, int(filtered_.size()));
  if (first >= 0) selectedUuid_ = filtered_[first]->uuid;
  rebuildCards();
}

bool SceneDeck::actionsBlocked() const
{
  return shuttingDown_ || changingCollection_ || editing_ || QApplication::activeModalWidget() || QApplication::activePopupWidget();
}

void SceneDeck::activate(const QString &uuid)
{
  if (actionsBlocked()) return;
  if (mode_->currentIndex() == 0) { setPreview(uuid); return; }
  Entry *entry = findEntry(uuid);
  if (!entry) return;
  OBSSource source = OBSGetStrongRef(entry->source);
  if (!source || obs_source_removed(source)) return;
  obs_frontend_set_current_scene(source);
  status(text("SwitchedTo").arg(entry->name));
  updateStates();
}

void SceneDeck::setPreview(const QString &uuid)
{
  if (actionsBlocked()) return;
  if (!obs_frontend_preview_program_mode_active()) { status(text("StudioRequired")); return; }
  auto *entry = findEntry(uuid);
  if (!entry) return;
  OBSSource source = OBSGetStrongRef(entry->source);
  if (!source || obs_source_removed(source)) return;
  obs_frontend_set_current_preview_scene(source);
  status(text("Previewing").arg(entry->name));
  updateStates();
}

void SceneDeck::loadPanelBindings()
{
  for (int index = 0; index < ActionCount; ++index) {
    if (obs_data_has_user_value(settings_, ActionKeys[index])) {
      OBSDataArrayAutoRelease bindings = obs_data_get_array(settings_, ActionKeys[index]);
      obs_hotkey_load(actionHotkeys_[index], bindings);
    } else if (index == ActivateSelected || index == TakePreview) {
      obs_key_combination_t key = {0, index == ActivateSelected ? OBS_KEY_RETURN : OBS_KEY_SPACE};
      obs_hotkey_load_bindings(actionHotkeys_[index], &key, 1);
    } else obs_hotkey_load_bindings(actionHotkeys_[index], nullptr, 0);
  }
}

int SceneDeck::matchingPanelAction(obs_key_combination_t combination) const
{
  struct Match { const SceneDeck *self; obs_key_combination_t key; int result = -1; } match{this, combination};
  obs_enum_hotkey_bindings([](void *data, size_t, obs_hotkey_binding_t *binding) {
    auto &match = *static_cast<Match *>(data);
    const auto current = obs_hotkey_binding_get_key_combination(binding);
    if (current.key != match.key.key || current.modifiers != match.key.modifiers || current.key == OBS_KEY_NONE) return true;
    for (int index = 0; index < ActionCount; ++index)
      if (obs_hotkey_binding_get_hotkey_id(binding) == match.self->actionHotkeys_[index]) { match.result = index; return false; }
    return true;
  }, &match);
  return match.result;
}

void SceneDeck::runPanelAction(PanelAction action)
{
  if (actionsBlocked() || QApplication::focusWidget() != this) return;
  if (action == SetPreview) setPreview(selectedUuid_);
  else if (action == ActivateSelected) activate(selectedUuid_);
  else if (action == TakePreview) take();
}

void SceneDeck::panelHotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
  if (!pressed) return;
  auto *self = static_cast<SceneDeck *>(data);
  QPointer<SceneDeck> owner(self);
  const auto generation = self->generation_;
  QMetaObject::invokeMethod(self, [owner, id, generation] {
    if (!owner || owner->generation_ != generation) return;
    for (int index = 0; index < ActionCount; ++index)
      if (owner->actionHotkeys_[index] == id) owner->runPanelAction(PanelAction(index));
  }, Qt::QueuedConnection);
}

void SceneDeck::take()
{
  if (actionsBlocked()) return;
  if (!obs_frontend_preview_program_mode_active()) { status(text("StudioRequired")); return; }
  if (transitionRunning_) return;
  transitionRunning_ = true;
  take_->setEnabled(false);
  obs_frontend_preview_program_trigger_transition();
  status(text("TransitionTriggered"));
}

void SceneDeck::hotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
  if (!pressed) return;
  auto *entry = static_cast<Entry *>(data);
  QPointer<SceneDeck> owner(entry->owner);
  const QString uuid = entry->uuid;
  const unsigned generation = entry->owner->generation_;
  QMetaObject::invokeMethod(entry->owner, [owner, uuid, generation] {
    if (owner && owner->generation_ == generation) owner->handleHotkey(uuid);
  }, Qt::QueuedConnection);
}

void SceneDeck::focusHotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
  if (!pressed) return;
  auto *self = static_cast<SceneDeck *>(data);
  QPointer<SceneDeck> owner(self);
  QMetaObject::invokeMethod(self, [owner] {
    if (!owner || owner->actionsBlocked()) return;
    for (QWidget *parent = owner->parentWidget(); parent; parent = parent->parentWidget()) {
      if (auto *dock = qobject_cast<QDockWidget *>(parent)) { dock->show(); dock->raise(); break; }
    }
    owner->show();
    owner->window()->raise();
    owner->window()->activateWindow();
    owner->setFocus(Qt::ShortcutFocusReason);
  }, Qt::QueuedConnection);
}

void SceneDeck::handleHotkey(const QString &uuid)
{
  if (actionsBlocked()) return;
  // Editing a name/search field must not switch a scene via a plain letter key.
  auto *focus = QApplication::focusWidget();
  if (focus && (qobject_cast<QLineEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus))) return;
  selectedUuid_ = uuid;
  activate(uuid);
}

bool SceneDeck::eventFilter(QObject *watched, QEvent *event)
{
  auto *focus = QApplication::focusWidget();
  const bool deckFocus = focus == this;
  if (!deckFocus || actionsBlocked()) return QWidget::eventFilter(watched, event);
  if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease &&
      event->type() != QEvent::ShortcutOverride) return QWidget::eventFilter(watched, event);
  auto *key = static_cast<QKeyEvent *>(event);
  const int code = key->key();
  const int action = matchingPanelAction(translateKey(key));
  if (action >= 0) {
    event->accept();
    if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) runPanelAction(PanelAction(action));
    return true;
  }
  const bool plain = (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier | Qt::ShiftModifier)) == 0;
  const bool handled = plain && ((code >= Qt::Key_1 && code <= Qt::Key_9) || code == Qt::Key_Left ||
    code == Qt::Key_Right || code == Qt::Key_Up || code == Qt::Key_Down ||
    code == Qt::Key_PageUp || code == Qt::Key_PageDown || code == Qt::Key_Escape);
  if (!handled) {
    if (code == Qt::Key_Tab || code == Qt::Key_Backtab || event->type() == QEvent::ShortcutOverride)
      return QWidget::eventFilter(watched, event);
    // While focused, physical-key polling is paused. Forward non-navigation
    // keys exactly once through OBS so existing fixed shortcuts still work.
    if (!key->isAutoRepeat() && hotkeysAllowedByFocusPolicy()) {
      const auto combo = translateKey(key);
      if (combo.key != OBS_KEY_NONE)
        obs_hotkey_inject_event(combo, event->type() == QEvent::KeyPress);
    }
    return true;
  }
  if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
  if (event->type() == QEvent::KeyRelease || key->isAutoRepeat()) return true;
  const int pageSize = columns_->value() * rows_->value();
  if (code >= Qt::Key_1 && code <= Qt::Key_9) {
    select(deck::slotIndex(page_, pageSize, code - Qt::Key_1, int(filtered_.size())));
    if (mode_->currentIndex() == 1 && code - Qt::Key_1 < int(cards_.size())) activate(selectedUuid_);
  } else if (code == Qt::Key_PageUp || code == Qt::Key_PageDown) {
    changePage(code == Qt::Key_PageUp ? -1 : 1);
  } else if (code == Qt::Key_Escape) {
    clearFocus();
  } else {
    select(deck::moveSelection(selectedIndex(), int(filtered_.size()), columns_->value(),
      code == Qt::Key_Left ? -1 : code == Qt::Key_Right ? 1 : 0,
      code == Qt::Key_Up ? -1 : code == Qt::Key_Down ? 1 : 0));
  }
  return true;
}

void SceneDeck::editBinding(const QString &uuid)
{
  Entry *entry = findEntry(uuid);
  if (!entry || !canEdit()) return;
  editHotkey(entry->hotkey, entry->name);
}

bool SceneDeck::canEdit() const
{
  auto *modal = QApplication::activeModalWidget();
  return !shuttingDown_ && !changingCollection_ && !editing_ &&
    (!modal || modal == settingsDialog_) && !QApplication::activePopupWidget();
}

void SceneDeck::editHotkey(obs_hotkey_id hotkey, const QString &name)
{
  if (!canEdit() || hotkey == OBS_INVALID_HOTKEY_ID) return;
  QScopedValueRollback<bool> editing(editing_, true);
  ScopedHotkeyPause pause;
  const unsigned generation = generation_;
  QDialog dialog(settingsDialog_->isVisible() ? static_cast<QWidget *>(settingsDialog_) : this);
  dialog.setWindowTitle(text("ShortcutFor").arg(name));
  auto *layout = new QVBoxLayout(&dialog);
  const bool panelAction = std::find(actionHotkeys_.begin(), actionHotkeys_.end(), hotkey) != actionHotkeys_.end();
  auto *hint = new QLabel(text(panelAction ? "PanelShortcutHint" : "ShortcutHint"), &dialog);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  auto *capture = new HotkeyCapture(&dialog);
  capture->setText(describeHotkey(hotkey));
  layout->addWidget(capture);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, &dialog);
  layout->addWidget(buttons);
  connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, capture, &HotkeyCapture::clearBinding);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this] {
    if (generation_ != generation) { dialog.reject(); return; }
    if (!capture->changed()) { dialog.accept(); return; }
    const auto combo = capture->combination();
    // Plain navigation keys are reserved within the panel. Require modifiers
    // for fixed bindings so a number key cannot select and switch differently.
    const bool reserved = (combo.key >= OBS_KEY_1 && combo.key <= OBS_KEY_9) || combo.key == OBS_KEY_LEFT ||
      combo.key == OBS_KEY_RIGHT || combo.key == OBS_KEY_UP || combo.key == OBS_KEY_DOWN ||
      combo.key == OBS_KEY_PAGEUP || combo.key == OBS_KEY_PAGEDOWN || combo.key == OBS_KEY_ESCAPE || combo.key == OBS_KEY_TAB;
    if (panelAction && combo.modifiers == 0 && reserved) {
      QMessageBox::information(&dialog, text("ShortcutConflict"), text("NavigationKeyReserved"));
      return;
    }
    if (!panelAction && combo.key != OBS_KEY_NONE && combo.modifiers == 0) {
      QMessageBox::information(&dialog, text("ShortcutConflict"), text("ModifierRequired"));
      return;
    }
    const QStringList conflicts = bindingConflicts(combo, hotkey);
    if (combo.key != OBS_KEY_NONE && !conflicts.isEmpty()) {
      QMessageBox::information(&dialog, text("ShortcutConflict"), text("ConflictingBindings").arg(conflicts.join(QStringLiteral("\n"))));
      return;
    }
    if (combo.key == OBS_KEY_NONE) obs_hotkey_load_bindings(hotkey, nullptr, 0);
    else { auto binding = combo; obs_hotkey_load_bindings(hotkey, &binding, 1); }
    snapshotBindings(); requestSave(); updateStates(); dialog.accept();
  });
  capture->setFocus();
  dialog.exec();
  if (settingsDialog_->isVisible()) shortcutScene_->setFocus();
  else setFocus();
}

void SceneDeck::batchBindings()
{
  if (!canEdit() || filtered_.empty()) return;
  QScopedValueRollback<bool> editing(editing_, true);
  ScopedHotkeyPause pause;
  const unsigned generation = generation_;
  const auto answer = QMessageBox::question(settingsDialog_->isVisible() ? static_cast<QWidget *>(settingsDialog_) : this,
    text("BatchBindings"), text("BatchHint"), QMessageBox::Yes | QMessageBox::No);
  if (answer != QMessageBox::Yes || changingCollection_ || shuttingDown_ || generation != generation_) return;
  const int pageSize = columns_->value() * rows_->value();
  struct Binding { Entry *entry; obs_key_combination_t combo; };
  std::vector<Binding> pending;
  for (int slot = 0; slot < std::min(pageSize, 9); ++slot) {
    const int index = deck::slotIndex(page_, pageSize, slot, int(filtered_.size()));
    if (index < 0) break;
    auto *entry = filtered_[index];
    obs_key_combination_t combo = {INTERACT_CONTROL_KEY | INTERACT_ALT_KEY,
      obs_key_from_name(QStringLiteral("OBS_KEY_%1").arg(slot + 1).toUtf8().constData())};
    const auto conflicts = bindingConflicts(combo, entry->hotkey);
    if (!conflicts.isEmpty()) {
      QMessageBox::information(this, text("ShortcutConflict"), text("ConflictingBindings").arg(conflicts.join(QStringLiteral("\n"))));
      return;
    }
    pending.push_back({entry, combo});
  }
  for (auto &binding : pending) obs_hotkey_load_bindings(binding.entry->hotkey, &binding.combo, 1);
  snapshotBindings(); requestSave(); updateStates();
  status(text("BatchComplete"));
}

void SceneDeck::showSceneMenu(const QString &uuid, const QPoint &globalPosition)
{
  if (actionsBlocked()) return;
  const unsigned generation = generation_;
  QMenu menu(this);
  QAction *shortcut = nullptr, *position = nullptr, *group = nullptr, *earlier = nullptr, *later = nullptr;
  if (auto *entry = findEntry(uuid)) {
    menu.addAction(entry->name)->setEnabled(false);
    for (const auto &card : cards_) {
      if (card.uuid == uuid) {
        menu.addAction(text("PagePosition").arg(card.slot + 1))->setEnabled(false);
        if (card.slot < 9) menu.addAction(text("NumberKey").arg(card.slot + 1))->setEnabled(false);
        break;
      }
    }
    menu.addSeparator();
    const QString binding = describeHotkey(entry->hotkey);
    shortcut = menu.addAction(binding.isEmpty() ? text("SetShortcut") : text("EditShortcutValue").arg(binding));
    shortcut->setObjectName(QStringLiteral("editShortcut"));
    position = menu.addAction(text("SetPosition"));
    position->setObjectName(QStringLiteral("editPosition"));
    group = menu.addAction(text("SetGroup"));
    earlier = menu.addAction(text("MoveEarlier"));
    later = menu.addAction(text("MoveLater"));
    menu.addSeparator();
  }
  auto *settings = menu.addAction(text("Settings"));
  auto *help = menu.addAction(text("Help"));
  QAction *chosen = menu.exec(globalPosition);
  // Defer changes until the native context-menu event has returned. This also
  // prevents a card reorder from deleting the event receiver on its own stack.
  if (!chosen || generation != generation_) return;
  if (chosen == shortcut) QTimer::singleShot(0, this, [this, uuid] { editBinding(uuid); });
  else if (chosen == position) QTimer::singleShot(0, this, [this, uuid] { editPosition(uuid); });
  else if (chosen == group) QTimer::singleShot(0, this, [this, uuid] { editGroup(uuid); });
  else if (chosen == earlier) QTimer::singleShot(0, this, [this, uuid] { moveScene(uuid, -1); });
  else if (chosen == later) QTimer::singleShot(0, this, [this, uuid] { moveScene(uuid, 1); });
  else if (chosen == settings) QTimer::singleShot(0, this, [this] { showSettings(); });
  else if (chosen == help) QTimer::singleShot(0, this, [this] { showHelp(); });
}

void SceneDeck::editPosition(const QString &uuid)
{
  if (actionsBlocked()) return;
  int index = -1;
  for (int i = 0; i < int(filtered_.size()); ++i) if (filtered_[i]->uuid == uuid) index = i;
  const int pageSize = columns_->value() * rows_->value();
  const int first = page_ * pageSize;
  const int count = std::min(pageSize, int(filtered_.size()) - first);
  if (index < first || index >= first + count) return;
  QScopedValueRollback<bool> editing(editing_, true);
  ScopedHotkeyPause pause;
  const unsigned generation = generation_;
  bool accepted = false;
  const int slot = QInputDialog::getInt(this, text("SetPosition"), text("PositionHint"),
    index - first + 1, 1, count, 1, &accepted);
  if (!accepted || generation != generation_ || changingCollection_ || shuttingDown_) return;
  const QString targetUuid = filtered_[first + slot - 1]->uuid;
  auto a = std::find_if(entries_.begin(), entries_.end(), [&uuid](const auto &entry) { return entry->uuid == uuid; });
  auto b = std::find_if(entries_.begin(), entries_.end(), [&targetUuid](const auto &entry) { return entry->uuid == targetUuid; });
  if (a == entries_.end() || b == entries_.end() || a == b) return;
  std::iter_swap(a, b);
  saveOrder();
  rebuildCards();
  requestSave();
}

void SceneDeck::saveOrder()
{
  OBSDataArrayAutoRelease order = obs_data_array_create();
  for (const auto &entry : entries_) {
    OBSDataAutoRelease item = obs_data_create();
    obs_data_set_string(item, "uuid", entry->uuid.toUtf8().constData());
    obs_data_array_push_back(order, item);
  }
  obs_data_set_array(settings_, "order", order);
}

void SceneDeck::moveScene(const QString &uuid, int delta)
{
  auto it = std::find_if(entries_.begin(), entries_.end(), [&uuid](const auto &entry) { return entry->uuid == uuid; });
  if (it == entries_.end()) return;
  const int index = int(std::distance(entries_.begin(), it));
  const int target = index + delta;
  if (target < 0 || target >= int(entries_.size())) return;
  std::swap(entries_[index], entries_[target]);
  saveOrder();
  rebuildCards(); requestSave();
}

void SceneDeck::editGroup(const QString &uuid)
{
  auto *entry = findEntry(uuid);
  if (!entry) return;
  QScopedValueRollback<bool> editing(editing_, true);
  ScopedHotkeyPause pause;
  const unsigned generation = generation_;
  bool ok = false;
  const QString group = QInputDialog::getText(this, text("SetGroup"), text("GroupHint"), QLineEdit::Normal, sceneGroup(uuid), &ok).trimmed();
  if (!ok || generation_ != generation || !findEntry(uuid)) return;
  OBSDataAutoRelease groups = obs_data_get_obj(settings_, "groups");
  if (!groups) groups = obs_data_create();
  obs_data_set_string(groups, uuid.toUtf8().constData(), group.toUtf8().constData());
  obs_data_set_obj(settings_, "groups", groups);
  updateGroups(); rebuildCards(); requestSave();
}

void SceneDeck::showHelp()
{
  QScopedValueRollback<bool> editing(editing_, true);
  QMessageBox::information(settingsDialog_->isVisible() ? static_cast<QWidget *>(settingsDialog_) : this,
    text("Help"), text("HelpText"));
}

void SceneDeck::showSettings()
{
  if (actionsBlocked()) return;
  ScopedHotkeyPause pause;
  refreshSettingsScenes();
  updateStates();
  settingsDialog_->exec();
  window()->activateWindow();
  setFocus();
}

void SceneDeck::refreshSettingsScenes()
{
  const QSignalBlocker block(shortcutScene_);
  const QString selected = shortcutScene_->currentData().toString();
  shortcutScene_->clear();
  for (const auto &entry : entries_) shortcutScene_->addItem(entry->name, entry->uuid);
  const int previous = shortcutScene_->findData(selected);
  if (previous >= 0) shortcutScene_->setCurrentIndex(previous);
}

void SceneDeck::applyAudioSettings()
{
  audioSide_->setEnabled(audioMeters_->isChecked());
  audioWidth_->setEnabled(audioMeters_->isChecked());
  for (auto &card : cards_) {
    card.meter->setFixedWidth(audioWidth_->value());
    card.meter->setVisible(audioMeters_->isChecked());
    card.pictureLayout->setDirection(audioSide_->currentIndex() == 1 ? QBoxLayout::RightToLeft : QBoxLayout::LeftToRight);
  }
}

void SceneDeck::status(const QString &message) { status_->setText(message); }

void SceneDeck::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  stateTimer_->start();
  if (!changingCollection_) scheduleRefresh();
}

void SceneDeck::hideEvent(QHideEvent *event)
{
  stateTimer_->stop();
  QWidget::hideEvent(event);
}

bool SceneDeck::event(QEvent *event)
{
  if (event->type() == QEvent::FocusIn && !shuttingDown_)
    navigationPause_ = std::make_unique<ScopedHotkeyPause>(true);
  if (event->type() == QEvent::FocusOut) navigationPause_.reset();
  if (event->type() == QEvent::Type(QEvent::User + QEvent::Close)) {
    for (auto &card : cards_) card.preview->stop();
  }
  return QWidget::event(event);
}
