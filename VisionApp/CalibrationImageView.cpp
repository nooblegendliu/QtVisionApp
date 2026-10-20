#include "CalibrationImageView.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsSimpleTextItem>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vision::app {

CalibrationImageView::CalibrationImageView(QWidget* parent)
    : QGraphicsView(parent),
      pixmapItem_(scene_.addPixmap(QPixmap())),
      roleLabel_(new QLabel(viewport())),
      statusLabel_(new QLabel(viewport()))
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
    roleLabel_->setFont(roleFont);
    QPalette rolePalette = roleLabel_->palette();
    rolePalette.setColor(QPalette::WindowText, Qt::white);
    roleLabel_->setPalette(rolePalette);
    roleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);

    QFont statusFont;
    statusFont.setPointSize(11);
    statusFont.setBold(true);
    statusLabel_->setFont(statusFont);
    statusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    clearImage();
}

void CalibrationImageView::setImage(const QImage& image)
{
    if (image.isNull()) {
        clearImage();
        return;
    }

    hasImage_ = true;
    pixmapItem_->setPixmap(QPixmap::fromImage(image));
    scene_.setSceneRect(pixmapItem_->boundingRect());
    fitImage();
    updateOverlayGeometry();
}

void CalibrationImageView::setOverlay(
    const QString& role,
    const QString& status,
    const QColor& statusColor)
{
    roleLabel_->setText(role);
    statusLabel_->setText(status);
    QPalette statusPalette = statusLabel_->palette();
    statusPalette.setColor(QPalette::WindowText, statusColor);
    statusLabel_->setPalette(statusPalette);
    updateOverlayGeometry();
}

void CalibrationImageView::setCalibrationPoints(
    const QVector<CalibrationOverlayPoint>& points)
{
    clearCalibrationPoints();
    calibrationPoints_ = points;

    constexpr qreal radius = 7.0;
    for (const auto& point : points) {
        const QColor color = point.accepted
            ? QColor(0, 210, 135)
            : QColor(220, 70, 70);
        auto* marker = new QGraphicsEllipseItem(
            -radius, -radius, radius * 2.0, radius * 2.0);
        marker->setPen(QPen(color, 2.0));
        marker->setPos(point.pixel);
        marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        marker->setZValue(10.0);
        scene_.addItem(marker);
        pointItems_.push_back(marker);

        auto* label = new QGraphicsSimpleTextItem(point.label, marker);
        QFont labelFont;
        labelFont.setPointSize(9);
        labelFont.setBold(true);
        label->setFont(labelFont);
        label->setBrush(color);
        label->setPos(radius + 3.0, radius + 1.0);
        label->setZValue(11.0);
    }
}

void CalibrationImageView::clearCalibrationPoints()
{
    for (QGraphicsItem* item : pointItems_) {
        scene_.removeItem(item);
        delete item;
    }
    pointItems_.clear();
    calibrationPoints_.clear();
}

void CalibrationImageView::setPointSelectionEnabled(bool enabled) noexcept
{
    pointSelectionEnabled_ = enabled;
}

void CalibrationImageView::clearImage(const QString& placeholder)
{
    clearCalibrationPoints();
    hasImage_ = false;
    pixmapItem_->setPixmap(QPixmap());
    scene_.setSceneRect(QRectF());
    resetTransform();
    displayMode_ = CalibrationImageDisplayMode::Fit;
    setOverlay(QString(), placeholder);
}

void CalibrationImageView::fitImage()
{
    displayMode_ = CalibrationImageDisplayMode::Fit;
    applyFitTransform();
}

void CalibrationImageView::showActualPixels()
{
    displayMode_ = CalibrationImageDisplayMode::ActualPixels;
    resetTransform();
    if (hasImage_) {
        centerOn(pixmapItem_);
    }
}

void CalibrationImageView::refreshForLayoutChange()
{
    if (displayMode_ == CalibrationImageDisplayMode::Fit) {
        applyFitTransform();
    }
    updateOverlayGeometry();
}

CalibrationImageDisplayMode CalibrationImageView::displayMode() const noexcept
{
    return displayMode_;
}

void CalibrationImageView::mousePressEvent(QMouseEvent* event)
{
    pressPosition_ = event->position().toPoint();
    QGraphicsView::mousePressEvent(event);
}

void CalibrationImageView::mouseReleaseEvent(QMouseEvent* event)
{
    const QPoint releasePosition = event->position().toPoint();
    const int movement = (releasePosition - pressPosition_).manhattanLength();
    QGraphicsView::mouseReleaseEvent(event);
    if (!pointSelectionEnabled_ || !hasImage_ ||
        event->button() != Qt::LeftButton || movement > 4) {
        return;
    }

    constexpr qreal maximumDistance = 12.0;
    qreal bestDistanceSquared = maximumDistance * maximumDistance;
    int bestIndex = -1;
    for (const auto& point : calibrationPoints_) {
        if (point.sourceIndex < 0) continue;
        const QPoint viewportPoint = mapFromScene(point.pixel);
        const QPoint delta = viewportPoint - releasePosition;
        const qreal distanceSquared =
            static_cast<qreal>(delta.x() * delta.x() + delta.y() * delta.y());
        if (distanceSquared <= bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestIndex = point.sourceIndex;
        }
    }
    if (bestIndex >= 0) emit calibrationPointClicked(bestIndex);
}

void CalibrationImageView::wheelEvent(QWheelEvent* event)
{
    if (!hasImage_ || event->angleDelta().y() == 0) {
        event->ignore();
        return;
    }

    constexpr qreal minimumScale = 0.05;
    constexpr qreal maximumScale = 20.0;
    const qreal requestedFactor =
        event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const qreal currentScale = std::abs(transform().m11());
    const qreal targetScale = std::clamp(
        currentScale * requestedFactor, minimumScale, maximumScale);
    const qreal factor = currentScale > 0.0
        ? targetScale / currentScale : 1.0;
    scale(factor, factor);
    displayMode_ = CalibrationImageDisplayMode::ManualZoom;
    event->accept();
}

void CalibrationImageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    fitImage();
    event->accept();
}

void CalibrationImageView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (displayMode_ == CalibrationImageDisplayMode::Fit) {
        applyFitTransform();
    }
    updateOverlayGeometry();
}

void CalibrationImageView::applyFitTransform()
{
    resetTransform();
    if (hasImage_ && !pixmapItem_->pixmap().isNull() &&
        viewport()->width() > 1 && viewport()->height() > 1) {
        fitInView(pixmapItem_, Qt::KeepAspectRatio);
    }
}

void CalibrationImageView::updateOverlayGeometry()
{
    constexpr int left = 14;
    constexpr int top = 10;
    constexpr int spacing = 3;
    const int maximumWidth = std::max(0, viewport()->width() - left * 2);
    roleLabel_->setMaximumWidth(maximumWidth);
    statusLabel_->setMaximumWidth(maximumWidth);
    roleLabel_->adjustSize();
    statusLabel_->adjustSize();
    roleLabel_->move(left, top);
    statusLabel_->move(left, top + roleLabel_->height() + spacing);
    roleLabel_->raise();
    statusLabel_->raise();
}

} // namespace vision::app
