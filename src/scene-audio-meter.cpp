#include "scene-audio-meter.hpp"
#include "text.hpp"
#include <obs-audio-controls.h>
#include <QHideEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QShowEvent>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <set>

namespace {
struct AudioInputs {
  std::set<obs_source_t *> visited;
  std::map<std::string, OBSSource> sources;
};
void collectInputs(obs_source_t *source, AudioInputs &inputs)
{
  if (!source || obs_source_removed(source) || !inputs.visited.insert(source).second) return;
  obs_scene_t *scene = obs_scene_from_source(source);
  if (!scene) scene = obs_group_from_source(source);
  if (scene) {
    obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
      if (obs_sceneitem_visible(item)) collectInputs(obs_sceneitem_get_source(item), *static_cast<AudioInputs *>(data));
      return true;
    }, &inputs);
  } else if (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) {
    inputs.sources.emplace(obs_source_get_uuid(source), OBSSource(source));
  }
}
} // namespace

SceneAudioMeter::Monitor::~Monitor()
{
  // Keep a live source alive during detach; libobs also handles sources that
  // have already emitted their destroy signal. Callback removal synchronizes.
  OBSSource keepAlive = OBSGetStrongRef(source);
  if (meter) {
    obs_volmeter_remove_callback(meter, SceneAudioMeter::levels, this);
    obs_volmeter_destroy(meter);
  }
}

SceneAudioMeter::SceneAudioMeter(obs_source_t *scene, QWidget *parent)
  : QWidget(parent), scene_(OBSGetWeakRef(scene))
{
  setObjectName(QStringLiteral("sceneAudioMeter"));
  setFocusPolicy(Qt::NoFocus);
  setFixedWidth(14);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  setToolTip(text("AudioMeterHint"));
  setAccessibleName(text("AudioMeter"));
  refresh_ = new QTimer(this);
  refresh_->setInterval(500);
  connect(refresh_, &QTimer::timeout, this, [this] { refreshSources(); });
  repaint_ = new QTimer(this);
  repaint_->setInterval(40);
  connect(repaint_, &QTimer::timeout, this, [this] { updateLevels(); });
}

SceneAudioMeter::~SceneAudioMeter() { stop(); }

void SceneAudioMeter::refreshSources()
{
  if (!isVisible()) return;
  AudioInputs inputs;
  OBSSource scene = OBSGetStrongRef(scene_);
  collectInputs(scene, inputs);
  for (auto it = monitors_.begin(); it != monitors_.end();) {
    if (!inputs.sources.count(it->first)) it = monitors_.erase(it);
    else ++it;
  }
  for (const auto &[uuid, source] : inputs.sources) {
    if (monitors_.count(uuid)) continue;
    auto monitor = std::make_unique<Monitor>();
    monitor->source = OBSGetWeakRef(source);
    monitor->meter = obs_volmeter_create(OBS_FADER_LOG);
    if (!monitor->meter) continue;
    if (!obs_volmeter_attach_source(monitor->meter, source)) continue;
    const int channels = obs_volmeter_get_nr_channels(monitor->meter);
    monitor->channels = channels > 0 ? channels : 2;
    obs_volmeter_add_callback(monitor->meter, levels, monitor.get());
    monitors_.emplace(uuid, std::move(monitor));
  }
}

void SceneAudioMeter::levels(void *data, const float *, const float *peak, const float *)
{
  auto &monitor = *static_cast<Monitor *>(data);
  std::lock_guard<std::mutex> lock(monitor.mutex);
  for (int channel = 0; channel < 2; ++channel) {
    const float value = peak[monitor.channels == 1 ? 0 : channel];
    if (std::isfinite(value)) monitor.peak[channel] = std::max(monitor.peak[channel], value);
  }
}

void SceneAudioMeter::updateLevels()
{
  std::array<float, 2> target{-100.0f, -100.0f};
  for (const auto &[uuid, monitor] : monitors_) {
    OBSSource source = OBSGetStrongRef(monitor->source);
    std::lock_guard<std::mutex> lock(monitor->mutex);
    if (source && !obs_source_removed(source) && !obs_source_muted(source)) {
      for (int channel = 0; channel < 2; ++channel) target[channel] = std::max(target[channel], monitor->peak[channel]);
    }
    monitor->peak = {-100.0f, -100.0f};
  }
  for (int channel = 0; channel < 2; ++channel)
    displayed_[channel] = std::clamp(std::max(target[channel], displayed_[channel] - 2.4f), -100.0f, 0.0f);
  update();
}

void SceneAudioMeter::stop()
{
  refresh_->stop();
  repaint_->stop();
  monitors_.clear();
  displayed_ = {-100.0f, -100.0f};
}

float SceneAudioMeter::channelDb(int channel) const
{
  return channel >= 0 && channel < 2 ? displayed_[channel] : -100.0f;
}

void SceneAudioMeter::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  refreshSources();
  refresh_->start();
  repaint_->start();
}
void SceneAudioMeter::hideEvent(QHideEvent *event)
{
  stop();
  QWidget::hideEvent(event);
}

void SceneAudioMeter::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.fillRect(rect(), QColor(QStringLiteral("#111318")));
  const QRect area = rect().adjusted(1, 2, -1, -2);
  const int barWidth = std::max(1, (area.width() - 2) / 2);
  QLinearGradient gradient(0, area.bottom(), 0, area.top());
  gradient.setColorAt(0, QColor(QStringLiteral("#22c55e")));
  gradient.setColorAt(0.7, QColor(QStringLiteral("#eab308")));
  gradient.setColorAt(0.9, QColor(QStringLiteral("#ef4444")));
  gradient.setColorAt(1, QColor(QStringLiteral("#ef4444")));
  for (int channel = 0; channel < 2; ++channel) {
    QRect bar(area.left() + channel * (barWidth + 2), area.top(), barWidth, area.height());
    const int filled = int(std::clamp((displayed_[channel] + 60.0f) / 60.0f, 0.0f, 1.0f) * area.height());
    painter.fillRect(bar, QColor(QStringLiteral("#26312c")));
    bar.setTop(bar.bottom() - filled + 1);
    if (filled > 0) painter.fillRect(bar, gradient);
  }
}
