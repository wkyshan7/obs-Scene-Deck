#pragma once
#include <obs.hpp>
#include <QWidget>
#include <functional>
#include <mutex>
#include <vector>

// This widget owns its display and showing reference together. The render
// callback accesses no Qt objects and its source remains alive until destroy().
class ScenePreview final : public QWidget {
  Q_OBJECT
public:
  explicit ScenePreview(obs_source_t *source, QWidget *parent = nullptr);
  ~ScenePreview() override;
  void setPreviewEnabled(bool enabled);
  void stop();
  void setCaption(const QString &name, int number);
  QString captionName() const { return captionName_; }
  int captionNumber() const { return captionNumber_; }
  std::function<void()> clicked;
  std::function<void()> doubleClicked;
  std::function<void()> failed;
  QPaintEngine *paintEngine() const override;

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void paintEvent(QPaintEvent *) override;
  bool event(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
  void start();
  void updateCaption();
  void drawCaption(uint32_t width, uint32_t height, int x, int y);
  static void draw(void *data, uint32_t width, uint32_t height);
  OBSWeakSource weakSource_;
  OBSSource source_;
  obs_display_t *display_ = nullptr;
  bool enabled_ = true;
  bool showing_ = false;
  QString captionName_;
  int captionNumber_ = 0;
  std::mutex captionMutex_;
  std::vector<uint8_t> captionPixels_;
  uint32_t captionWidth_ = 0, captionHeight_ = 0;
  uint64_t captionRevision_ = 0, uploadedRevision_ = 0;
  gs_texture_t *captionTexture_ = nullptr;
};
