#include "scene-preview.hpp"
#include "core/layout.hpp"
#include "native-display.hpp"

#include <QHideEvent>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWindow>
#include <QImage>
#include <QPainter>
#include <QFontMetrics>
#include <algorithm>


ScenePreview::ScenePreview(obs_source_t *source, QWidget *parent)
  : QWidget(parent), weakSource_(OBSGetWeakRef(source))
{
  setAttribute(Qt::WA_PaintOnScreen);
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAttribute(Qt::WA_DontCreateNativeAncestors);
  setAttribute(Qt::WA_NativeWindow);
  setFocusPolicy(Qt::NoFocus);
  setMinimumSize(96, 54);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  if (windowHandle()) {
    connect(windowHandle(), &QWindow::visibleChanged, this, [this](bool visible) {
      if (visible)
        start();
      else
        stop();
    });
    connect(windowHandle(), &QWindow::screenChanged, this, [this] {
      updateCaption();
      if (display_) {
        const QSize pixels = size() * devicePixelRatioF();
        obs_display_resize(display_, pixels.width(), pixels.height());
      }
    });
    windowHandle()->installEventFilter(this);
  }
}

ScenePreview::~ScenePreview() { stop(); }

void ScenePreview::setPreviewEnabled(bool enabled)
{
  enabled_ = enabled;
  if (enabled)
    start();
  else
    stop();
}

void ScenePreview::start()
{
  if (display_ || !enabled_ || !isVisible() || !windowHandle() || !windowHandle()->isExposed())
    return;
  source_ = OBSGetStrongRef(weakSource_);
  if (!source_ || obs_source_removed(source_)) {
    source_ = nullptr;
    return;
  }
  const QSize pixels = size() * devicePixelRatioF();
  gs_init_data info = {};
  info.cx = std::max(1, pixels.width());
  info.cy = std::max(1, pixels.height());
  info.format = GS_BGRA;
  info.zsformat = GS_ZS_NONE;
  if (!sceneDeckNativeWindow(windowHandle(), info.window)) {
    source_ = nullptr;
    if (failed) failed();
    return;
  }
  display_ = obs_display_create(&info, 0xff161616);
  if (!display_) {
    source_ = nullptr;
    if (failed)
      failed();
    return;
  }
  obs_source_inc_showing(source_);
  showing_ = true;
  updateCaption();
  obs_display_add_draw_callback(display_, draw, this);
}

void ScenePreview::stop()
{
  // Destroy synchronizes with render callbacks before source_ is released.
  if (display_) {
    obs_display_remove_draw_callback(display_, draw, this);
    obs_display_destroy(display_);
    display_ = nullptr;
  }
  if (showing_) {
    obs_source_dec_showing(source_);
    showing_ = false;
  }
  if (captionTexture_) {
    obs_enter_graphics();
    gs_texture_destroy(captionTexture_);
    obs_leave_graphics();
    captionTexture_ = nullptr;
    uploadedRevision_ = 0;
  }
  source_ = nullptr;
}

void ScenePreview::setCaption(const QString &name, int number)
{
  if (captionName_ == name && captionNumber_ == number) return;
  captionName_ = name;
  captionNumber_ = number;
  updateCaption();
}

void ScenePreview::updateCaption()
{
  if (captionName_.isEmpty() || captionNumber_ <= 0) return;
  OBSSource source = OBSGetStrongRef(weakSource_);
  if (!source) return;
  const qreal ratio = devicePixelRatioF();
  const auto area = deck::fit(int(obs_source_get_width(source)), int(obs_source_get_height(source)),
    int(width() * ratio), int(height() * ratio));
  if (area.width < 16 || area.height < 16) return;
  QFont font = this->font();
  font.setPixelSize(std::max(10, int(12 * ratio)));
  font.setBold(true);
  const QFontMetrics metrics(font);
  const int padding = std::max(3, int(4 * ratio));
  const int captionHeight = std::min(area.height, metrics.height() + padding * 2);
  QImage bitmap(area.width, captionHeight, QImage::Format_RGBA8888_Premultiplied);
  bitmap.fill(Qt::transparent);
  QPainter painter(&bitmap);
  painter.setFont(font);
  painter.setRenderHint(QPainter::TextAntialiasing);
  const QString number = QString::number(captionNumber_);
  const int numberWidth = metrics.horizontalAdvance(number) + padding * 2;
  const int maximumName = std::max(0, area.width - numberWidth - padding * 3);
  const QString name = metrics.elidedText(captionName_, Qt::ElideRight, maximumName);
  painter.fillRect(QRect(0, 0, std::min(area.width, metrics.horizontalAdvance(name) + padding * 2), captionHeight), QColor(12, 14, 18, 190));
  painter.fillRect(QRect(area.width - numberWidth, 0, numberWidth, captionHeight), QColor(12, 14, 18, 210));
  painter.setPen(Qt::white);
  painter.drawText(QRect(padding, 0, maximumName, captionHeight), Qt::AlignLeft | Qt::AlignVCenter, name);
  painter.drawText(QRect(area.width - numberWidth, 0, numberWidth, captionHeight), Qt::AlignCenter, number);
  painter.end();
  std::lock_guard<std::mutex> lock(captionMutex_);
  captionWidth_ = uint32_t(bitmap.width());
  captionHeight_ = uint32_t(bitmap.height());
  captionPixels_.assign(bitmap.constBits(), bitmap.constBits() + bitmap.sizeInBytes());
  ++captionRevision_;
}

void ScenePreview::drawCaption(uint32_t width, uint32_t height, int x, int y)
{
  // Text is rasterized on the UI thread. The graphics thread only consumes a
  // locked byte buffer, uploads a texture and draws it over the native picture.
  std::lock_guard<std::mutex> lock(captionMutex_);
  if (uploadedRevision_ != captionRevision_ && !captionPixels_.empty()) {
    if (captionTexture_) gs_texture_destroy(captionTexture_);
    const uint8_t *pixels = captionPixels_.data();
    captionTexture_ = gs_texture_create(captionWidth_, captionHeight_, GS_RGBA, 1, &pixels, 0);
    uploadedRevision_ = captionRevision_;
  }
  if (!captionTexture_) return;
  gs_set_viewport(0, 0, int(width), int(height));
  gs_ortho(0.0f, float(width), 0.0f, float(height), -100.0f, 100.0f);
  gs_matrix_identity();
  gs_matrix_translate3f(float(x), float(y), 0.0f);
  gs_blend_state_push();
  gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
  gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
  gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), captionTexture_);
  while (gs_effect_loop(effect, "Draw")) gs_draw_sprite(captionTexture_, 0, captionWidth_, captionHeight_);
  gs_blend_state_pop();
}

void ScenePreview::draw(void *data, uint32_t width, uint32_t height)
{
  auto *self = static_cast<ScenePreview *>(data);
  obs_source_t *source = self->source_;
  if (!source || obs_source_removed(source))
    return;
  const auto sw = obs_source_get_width(source);
  const auto sh = obs_source_get_height(source);
  const deck::Rect rect = deck::fit(int(sw), int(sh), int(width), int(height));
  if (rect.width == 0 || rect.height == 0)
    return;
  gs_viewport_push();
  gs_projection_push();
  gs_matrix_push();
  gs_matrix_identity();
  gs_set_viewport(rect.x, rect.y, rect.width, rect.height);
  gs_ortho(0.0f, float(sw), 0.0f, float(sh), -100.0f, 100.0f);
  obs_source_video_render(source);
  self->drawCaption(width, height, rect.x, rect.y);
  gs_matrix_pop();
  gs_projection_pop();
  gs_viewport_pop();
}

void ScenePreview::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  QTimer::singleShot(0, this, [this] { start(); });
}

void ScenePreview::hideEvent(QHideEvent *event)
{
  stop();
  QWidget::hideEvent(event);
}

void ScenePreview::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  updateCaption();
  if (display_) {
    const QSize pixels = size() * devicePixelRatioF();
    obs_display_resize(display_, std::max(1, pixels.width()), std::max(1, pixels.height()));
  }
}

bool ScenePreview::event(QEvent *event)
{
  if (event->type() == QEvent::PlatformSurface &&
      static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() ==
        QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
    stop();
  return QWidget::event(event);
}

bool ScenePreview::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::PlatformSurface &&
      static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() ==
        QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
    stop();
  if (event->type() == QEvent::Expose)
    QTimer::singleShot(0, this, [this] { start(); });
  return QWidget::eventFilter(watched, event);
}

void ScenePreview::paintEvent(QPaintEvent *) { start(); }
QPaintEngine *ScenePreview::paintEngine() const { return nullptr; }

void ScenePreview::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton && clicked)
    clicked();
  QWidget::mousePressEvent(event);
}

void ScenePreview::mouseDoubleClickEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton && doubleClicked)
    doubleClicked();
  QWidget::mouseDoubleClickEvent(event);
}
