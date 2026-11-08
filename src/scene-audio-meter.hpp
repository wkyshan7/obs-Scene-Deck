#pragma once
#include <obs.hpp>
#include <QWidget>
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <string>

class QTimer;

// Shows the greatest post-fader peak among visible audio inputs in this scene,
// recursively including nested scenes. This is not the final Program mix.
class SceneAudioMeter final : public QWidget {
  Q_OBJECT
public:
  explicit SceneAudioMeter(obs_source_t *scene, QWidget *parent = nullptr);
  ~SceneAudioMeter() override;
  float channelDb(int channel) const;
  size_t monitoredSourceCount() const { return monitors_.size(); }

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  struct Monitor {
    OBSWeakSource source;
    obs_volmeter_t *meter = nullptr;
    std::mutex mutex;
    std::array<float, 2> peak{-100.0f, -100.0f};
    int channels = 2;
    ~Monitor();
  };
  void refreshSources();
  void updateLevels();
  void stop();
  static void levels(void *data, const float *, const float *peak, const float *);
  OBSWeakSource scene_;
  QTimer *refresh_ = nullptr;
  QTimer *repaint_ = nullptr;
  std::map<std::string, std::unique_ptr<Monitor>> monitors_;
  std::array<float, 2> displayed_{-100.0f, -100.0f};
};
