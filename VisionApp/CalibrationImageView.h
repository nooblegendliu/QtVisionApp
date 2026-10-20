#pragma once

#include <QColor>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector>

#include <vector>

class QGraphicsItem;
class QGraphicsPixmapItem;
class QLabel;

namespace vision::app {

struct CalibrationOverlayPoint {
    int sourceIndex{-1};
    QString label;
    QPointF pixel;
    bool accepted{true};
};

enum class CalibrationImageDisplayMode {
    Fit,
    ManualZoom,
    ActualPixels
};

class CalibrationImageView final : public QGraphicsView {
    Q_OBJECT

public:
    explicit CalibrationImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setOverlay(
        const QString& role,
        const QString& status,
        const QColor& statusColor = QColor(170, 180, 190));
    void setCalibrationPoints(
        const QVector<CalibrationOverlayPoint>& points);
    void clearCalibrationPoints();
    void setPointSelectionEnabled(bool enabled) noexcept;
    void clearImage(const QString& placeholder = QStringLiteral("等待后端图像"));
    void fitImage();
    void showActualPixels();
    void refreshForLayoutChange();
    [[nodiscard]] CalibrationImageDisplayMode displayMode() const noexcept;

signals:
    void calibrationPointClicked(int sourceIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyFitTransform();
    void updateOverlayGeometry();

    QGraphicsScene scene_;
    QGraphicsPixmapItem* pixmapItem_{nullptr};
    QLabel* roleLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    std::vector<QGraphicsItem*> pointItems_;
    QVector<CalibrationOverlayPoint> calibrationPoints_;
    QPoint pressPosition_;
    bool pointSelectionEnabled_{false};
    CalibrationImageDisplayMode displayMode_{CalibrationImageDisplayMode::Fit};
    bool hasImage_{false};
};

} // namespace vision::app
