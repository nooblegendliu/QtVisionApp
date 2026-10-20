#pragma once

#include <QColor>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QString>

class QGraphicsPixmapItem;
class QGraphicsSimpleTextItem;

namespace vision::app {

class ImageView final : public QGraphicsView {
public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setOverlay(const QString& role, const QString& result, const QColor& color);
    void clearImage(const QString& placeholder = QStringLiteral("等待采集"));
    void fitImage();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateOverlayPosition();

    QGraphicsScene scene_;
    QGraphicsPixmapItem* pixmapItem_{nullptr};
    QGraphicsSimpleTextItem* roleItem_{nullptr};
    QGraphicsSimpleTextItem* resultItem_{nullptr};
    bool fitOnResize_{true};
};

} // namespace vision::app
