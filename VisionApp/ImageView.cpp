#include "ImageView.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

namespace vision::app {

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent),
      pixmapItem_(scene_.addPixmap(QPixmap())),
      roleItem_(scene_.addSimpleText(QString())),
      resultItem_(scene_.addSimpleText(QString()))
{
    setScene(&scene_);
    setBackgroundBrush(QColor(31, 38, 46));
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    QFont roleFont;
    roleFont.setPointSize(12);
    roleFont.setBold(true);
    roleItem_->setFont(roleFont);
    roleItem_->setBrush(Qt::white);
    roleItem_->setFlag(QGraphicsItem::ItemIgnoresTransformations);

    QFont resultFont;
    resultFont.setPointSize(13);
    resultFont.setBold(true);
    resultItem_->setFont(resultFont);
    resultItem_->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    clearImage();
}

void ImageView::setImage(const QImage& image)
{
    pixmapItem_->setPixmap(QPixmap::fromImage(image));
    scene_.setSceneRect(pixmapItem_->boundingRect());
    fitOnResize_ = true;
    fitImage();
    updateOverlayPosition();
}

void ImageView::setOverlay(
    const QString& role,
    const QString& result,
    const QColor& color)
{
    roleItem_->setText(role);
    resultItem_->setText(result);
    resultItem_->setBrush(color);
    updateOverlayPosition();
}

void ImageView::clearImage(const QString& placeholder)
{
    QImage image(960, 640, QImage::Format_Grayscale8);
    image.fill(35);
    setImage(image);
    setOverlay(QString(), placeholder, QColor(170, 180, 190));
}

void ImageView::fitImage()
{
    if (!pixmapItem_->pixmap().isNull()) {
        fitInView(pixmapItem_, Qt::KeepAspectRatio);
    }
    fitOnResize_ = true;
}

void ImageView::wheelEvent(QWheelEvent* event)
{
    const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale(factor, factor);
    fitOnResize_ = false;
    event->accept();
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    fitImage();
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (fitOnResize_) {
        fitImage();
    }
    updateOverlayPosition();
}

void ImageView::updateOverlayPosition()
{
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    roleItem_->setPos(visible.left() + 14.0, visible.top() + 12.0);
    resultItem_->setPos(visible.left() + 14.0, visible.top() + 42.0);
}

} // namespace vision::app
