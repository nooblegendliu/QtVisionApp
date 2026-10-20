#include "TestSupport/TestDirectory.h"
#include "VisionAlgorithm/Edge/support/EdgePipelineTestSupport.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace vision::test::algorithm::edge_pipeline {
namespace {

constexpr unsigned char kBackgroundGray = 230;
constexpr unsigned char kWaferGray = 30;
constexpr unsigned char kArmShadowGray = 15;

const cv::Scalar kGreen(0, 255, 0);
const cv::Scalar kRed(0, 0, 255);
const cv::Scalar kYellow(0, 255, 255);
const cv::Scalar kCyan(255, 255, 0);
const cv::Scalar kBlue(255, 120, 0);
const cv::Scalar kWhite(255, 255, 255);

[[nodiscard]] cv::Point2d rotateLocalPoint(
    const cv::Point2d& localPoint,
    const FactorySceneSpec& spec)
{
    const double radians = spec.angleDeg * CV_PI / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {
        spec.center.x + localPoint.x * cosine - localPoint.y * sine,
        spec.center.y + localPoint.x * sine + localPoint.y * cosine};
}

[[nodiscard]] std::vector<cv::Point> makeChamferedWaferPolygon(
    const FactorySceneSpec& spec)
{
    const double halfWidth = kWaferWidth * 0.5;
    const double halfHeight = kWaferHeight * 0.5;
    const std::array<cv::Point2d, 8> localCorners{
        cv::Point2d(-halfWidth + kChamferLength, -halfHeight),
        cv::Point2d(halfWidth - kChamferLength, -halfHeight),
        cv::Point2d(halfWidth, -halfHeight + kChamferLength),
        cv::Point2d(halfWidth, halfHeight - kChamferLength),
        cv::Point2d(halfWidth - kChamferLength, halfHeight),
        cv::Point2d(-halfWidth + kChamferLength, halfHeight),
        cv::Point2d(-halfWidth, halfHeight - kChamferLength),
        cv::Point2d(-halfWidth, -halfHeight + kChamferLength)};

    std::vector<cv::Point> polygon;
    polygon.reserve(localCorners.size());
    for (const cv::Point2d& localCorner : localCorners) {
        const cv::Point2d corner = rotateLocalPoint(localCorner, spec);
        polygon.emplace_back(cvRound(corner.x), cvRound(corner.y));
    }
    return polygon;
}

void drawMechanicalArmShadows(cv::Mat& image)
{
    image(cv::Rect(2180, 4050, 761, kImageHeight - 4050))
        .setTo(cv::Scalar::all(kArmShadowGray));
    image(cv::Rect(4460, 2240, kImageWidth - 4460, 641))
        .setTo(cv::Scalar::all(kArmShadowGray));
}

[[nodiscard]] double pointDistance(
    const cv::Point2d& first,
    const cv::Point2d& second) noexcept
{
    return std::hypot(second.x - first.x, second.y - first.y);
}

[[nodiscard]] bool isInRange(
    const double value,
    const vision::algorithm::detail::MeasurementRangeConfig& range) noexcept
{
    return std::isfinite(value) &&
        value >= range.lower &&
        value <= range.upper;
}

[[nodiscard]] double normalizeLineAngleDeg(double angleDeg) noexcept
{
    while (angleDeg >= 90.0) {
        angleDeg -= 180.0;
    }
    while (angleDeg < -90.0) {
        angleDeg += 180.0;
    }
    return angleDeg;
}

[[nodiscard]] cv::Point toPixel(
    const vision::algorithm::DemoOverlayPoint& point)
{
    return {cvRound(point.x), cvRound(point.y)};
}

[[nodiscard]] cv::Point toPixel(const cv::Point2d& point)
{
    return {cvRound(point.x), cvRound(point.y)};
}

void putOutlinedText(
    cv::Mat& image,
    const std::string& text,
    const cv::Point origin,
    const double scale,
    const cv::Scalar& color,
    const int thickness = 6)
{
    cv::putText(
        image,
        text,
        origin,
        cv::FONT_HERSHEY_SIMPLEX,
        scale,
        cv::Scalar(0, 0, 0),
        thickness + 4,
        cv::LINE_AA);
    cv::putText(
        image,
        text,
        origin,
        cv::FONT_HERSHEY_SIMPLEX,
        scale,
        color,
        thickness,
        cv::LINE_AA);
}

[[nodiscard]] const cv::Scalar& statusColor(const bool qualified) noexcept
{
    return qualified ? kGreen : kRed;
}

[[nodiscard]] std::string statusText(const bool qualified)
{
    return qualified ? "OK" : "NG";
}

[[nodiscard]] std::string measurementRow(
    const std::string& label,
    const double value,
    const bool qualified,
    const std::string& unit = {})
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3)
           << label << ": " << value;
    if (!unit.empty()) {
        stream << ' ' << unit;
    }
    stream << "  " << statusText(qualified);
    return stream.str();
}

void drawClippedLine(
    cv::Mat& image,
    cv::Point start,
    cv::Point end,
    const cv::Scalar& color,
    const int thickness)
{
    if (cv::clipLine(cv::Rect(0, 0, image.cols, image.rows), start, end)) {
        cv::line(image, start, end, color, thickness, cv::LINE_AA);
    }
}

void drawResultPanel(
    cv::Mat& image,
    const EdgePostprocessMeasurements& measurements,
    const vision::core::AlgorithmOutput& output)
{
    constexpr int kTextStartX = 120;
    constexpr int kTextStartY = 220;
    constexpr int kLineStep = 220;
    constexpr double kTitleTextScale = 4.0;
    constexpr double kStatusTextScale = 3.2;
    constexpr double kPoseTextScale = 2.8;
    constexpr double kMeasurementTextScale = 2.8;
    constexpr int kTitleTextThickness = 8;

    const cv::Rect panelRect(35, 35, 3400, 2800);
    cv::Mat overlay = image.clone();
    cv::rectangle(overlay, panelRect, cv::Scalar(18, 18, 18), cv::FILLED);
    cv::addWeighted(overlay, 0.78, image, 0.22, 0.0, image);

    int y = kTextStartY;
    putOutlinedText(
        image,
        "FINAL RESULT: " + statusText(output.valid),
        cv::Point(kTextStartX, y),
        kTitleTextScale,
        statusColor(output.valid),
        kTitleTextThickness);

    y += kLineStep;
    putOutlinedText(
        image,
        "EDGE MODE: " + statusText(measurements.edgeModeQualified),
        cv::Point(kTextStartX, y),
        kStatusTextScale,
        statusColor(measurements.edgeModeQualified));
    y += kLineStep;
    putOutlinedText(
        image,
        "OFFSET RESULT: " + statusText(measurements.offsetResultQualified),
        cv::Point(kTextStartX, y),
        kStatusTextScale,
        statusColor(measurements.offsetResultQualified));

    y += kLineStep;
    std::ostringstream centerText;
    centerText << std::fixed << std::setprecision(3)
               << "POSE CENTER: X=" << measurements.center.x
               << "  Y=" << measurements.center.y;
    putOutlinedText(
        image,
        centerText.str(),
        cv::Point(kTextStartX, y),
        kPoseTextScale,
        statusColor(measurements.offsetResultQualified));

    y += kLineStep;
    putOutlinedText(
        image,
        measurementRow(
            "ABS ANGLE",
            std::abs(measurements.angleDeg),
            measurements.angleQualified,
            "deg"),
        cv::Point(kTextStartX, y),
        kPoseTextScale,
        statusColor(measurements.angleQualified));
    y += kLineStep;
    putOutlinedText(
        image,
        measurementRow(
            "CENTER OFFSET",
            measurements.centerOffset,
            measurements.centerOffsetQualified),
        cv::Point(kTextStartX, y),
        kPoseTextScale,
        statusColor(measurements.centerOffsetQualified));

    y += kLineStep;
    putOutlinedText(
        image,
        measurementRow("TOP LENGTH", measurements.topLength, measurements.topQualified),
        cv::Point(kTextStartX, y),
        kMeasurementTextScale,
        statusColor(measurements.topQualified));

    y += kLineStep;
    putOutlinedText(
        image,
        measurementRow("RIGHT LENGTH", measurements.rightLength, measurements.rightQualified),
        cv::Point(kTextStartX, y),
        kMeasurementTextScale,
        statusColor(measurements.rightQualified));

    y += kLineStep;
    putOutlinedText(
        image,
        measurementRow("BOTTOM LENGTH", measurements.bottomLength, measurements.bottomQualified),
        cv::Point(kTextStartX, y),
        kMeasurementTextScale,
        statusColor(measurements.bottomQualified));

    y += kLineStep;
    putOutlinedText(
        image,
        measurementRow("LEFT LENGTH", measurements.leftLength, measurements.leftQualified),
        cv::Point(kTextStartX, y),
        kMeasurementTextScale,
        statusColor(measurements.leftQualified));

    y += kLineStep;
    putOutlinedText(
        image,
        measurementRow(
            "DIAGONAL 1",
            measurements.diagonalTopLeftToBottomRight,
            measurements.diagonalTopLeftToBottomRightQualified),
        cv::Point(kTextStartX, y),
        kMeasurementTextScale,
        statusColor(measurements.diagonalTopLeftToBottomRightQualified));

    y += kLineStep;
    putOutlinedText(
        image,
        measurementRow(
            "DIAGONAL 2",
            measurements.diagonalTopRightToBottomLeft,
            measurements.diagonalTopRightToBottomLeftQualified),
        cv::Point(kTextStartX, y),
        kMeasurementTextScale,
        statusColor(measurements.diagonalTopRightToBottomLeftQualified));
}

} // namespace

std::filesystem::path findWorkspaceRoot()
{
    const std::array<std::filesystem::path, 2> startingPoints{
        std::filesystem::current_path(),
        std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path()};

    for (auto current : startingPoints) {
        while (!current.empty()) {
            if (std::filesystem::exists(current / "QtVisionApp.slnx")) {
                return current;
            }
            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
    }
    return {};
}

vision::algorithm::detail::EdgeAlgorithmConfig loadDefaultEdgeConfig()
{
    const auto workspaceRoot = findWorkspaceRoot();
    return vision::algorithm::detail::loadEdgeAlgorithmConfig(
        workspaceRoot / "VisionAlgorithm" / "config" /
        "parameters" / "edge.example.yaml");
}

vision::algorithm::detail::CameraGeometry loadDefaultCenterGeometry()
{
    const auto workspaceRoot = findWorkspaceRoot();
    return vision::algorithm::detail::CameraGeometry::loadFromFile(
        workspaceRoot / "VisionAlgorithm" / "config" /
        "calibration" / "center.example.yaml");
}

vision::algorithm::detail::CameraGeometry loadGeneratedCenterGeometry(
    const cv::Mat& imageToPhysical)
{
    const std::filesystem::path outputDirectory =
        vision::test::support::uniqueTestDirectory("edge-calibration");
    std::error_code fileError;
    std::filesystem::create_directories(outputDirectory, fileError);
    if (fileError) {
        throw std::runtime_error(
            "Could not create the EdgePipeline test calibration directory.");
    }

    const auto uniqueSuffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path calibrationPath =
        outputDirectory /
        ("generated_center_calibration_" +
         std::to_string(uniqueSuffix) + ".yaml");
    {
        cv::FileStorage storage(
            calibrationPath.string(),
            cv::FileStorage::WRITE);
        if (!storage.isOpened()) {
            throw std::runtime_error(
                "Could not create the EdgePipeline test calibration file.");
        }

        const cv::Mat identity = cv::Mat::eye(3, 3, CV_64F);
        const cv::Mat zeroDistortion = cv::Mat::zeros(1, 5, CV_64F);
        storage << "image_width" << kImageWidth;
        storage << "image_height" << kImageHeight;
        storage << "camera_matrix" << identity;
        storage << "distortion_coefficients" << zeroDistortion;
        storage << "new_camera_matrix" << identity;
        storage << "image_to_physical" << imageToPhysical;
    }

    try {
        auto geometry =
            vision::algorithm::detail::CameraGeometry::loadFromFile(
                calibrationPath);
        std::filesystem::remove(calibrationPath, fileError);
        return geometry;
    }
    catch (...) {
        std::filesystem::remove(calibrationPath, fileError);
        throw;
    }
}

std::unique_ptr<vision::algorithm::detail::EdgePipeline> makePipeline(
    vision::algorithm::detail::EdgeAlgorithmConfig config,
    const std::shared_ptr<
        vision::algorithm::DemoEdgeVisualizationStore>& visualizationStore)
{
    return makePipeline(
        std::move(config),
        loadDefaultCenterGeometry(),
        visualizationStore);
}

std::unique_ptr<vision::algorithm::detail::EdgePipeline> makePipeline(
    vision::algorithm::detail::EdgeAlgorithmConfig config,
    vision::algorithm::detail::CameraGeometry centerGeometry,
    const std::shared_ptr<
        vision::algorithm::DemoEdgeVisualizationStore>& visualizationStore)
{
    return std::make_unique<vision::algorithm::detail::EdgePipeline>(
        std::move(config),
        std::move(centerGeometry),
        visualizationStore);
}

cv::Mat makeFactoryScene(const FactorySceneSpec& spec)
{
    cv::Mat image(
        kImageHeight,
        kImageWidth,
        CV_8UC1,
        cv::Scalar::all(kBackgroundGray));
    cv::fillConvexPoly(
        image,
        makeChamferedWaferPolygon(spec),
        cv::Scalar::all(kWaferGray),
        cv::LINE_8);
    if (spec.drawArmShadows) {
        drawMechanicalArmShadows(image);
    }
    return image;
}

vision::algorithm::detail::ExecutionContext makeExecutionContext()
{
    const auto startedAt = std::chrono::steady_clock::now();
    return {startedAt, startedAt + std::chrono::seconds(30)};
}

bool imagesAreEqual(const cv::Mat& first, const cv::Mat& second)
{
    if (first.size() != second.size() || first.type() != second.type()) {
        return false;
    }
    cv::Mat difference;
    cv::compare(first, second, difference, cv::CMP_NE);
    return cv::countNonZero(difference) == 0;
}

EdgePostprocessMeasurements evaluatePostprocess(
    const vision::algorithm::DemoEdgeVisualization& visualization,
    const vision::algorithm::detail::EdgeAlgorithmConfig& config)
{
    EdgePostprocessMeasurements result;
    if (visualization.corners.size() != 4U ||
        visualization.rectifiedImage.empty()) {
        return result;
    }

    for (std::size_t index = 0; index < result.corners.size(); ++index) {
        result.corners[index] = {
            visualization.corners[index].x,
            visualization.corners[index].y};
    }

    const auto& topLeft = result.corners[0];
    const auto& topRight = result.corners[1];
    const auto& bottomRight = result.corners[2];
    const auto& bottomLeft = result.corners[3];
    result.sideMidpoints = {
        (topLeft + topRight) * 0.5,
        (topRight + bottomRight) * 0.5,
        (bottomLeft + bottomRight) * 0.5,
        (topLeft + bottomLeft) * 0.5};
    result.center =
        (topLeft + topRight + bottomRight + bottomLeft) * 0.25;

    const cv::Point2d horizontalDirection =
        result.sideMidpoints[1] - result.sideMidpoints[3];
    result.angleDeg = normalizeLineAngleDeg(
        std::atan2(horizontalDirection.y, horizontalDirection.x) *
        180.0 / CV_PI);

    result.topLength = pointDistance(topLeft, topRight);
    result.rightLength = pointDistance(topRight, bottomRight);
    result.bottomLength = pointDistance(bottomLeft, bottomRight);
    result.leftLength = pointDistance(topLeft, bottomLeft);
    result.diagonalTopLeftToBottomRight =
        pointDistance(topLeft, bottomRight);
    result.diagonalTopRightToBottomLeft =
        pointDistance(topRight, bottomLeft);
    result.centerOffset = pointDistance(
        result.center,
        cv::Point2d(
            visualization.rectifiedImage.cols * 0.5,
            visualization.rectifiedImage.rows * 0.5));

    result.topQualified = isInRange(result.topLength, config.quality.topLength);
    result.rightQualified =
        isInRange(result.rightLength, config.quality.rightLength);
    result.bottomQualified =
        isInRange(result.bottomLength, config.quality.bottomLength);
    result.leftQualified =
        isInRange(result.leftLength, config.quality.leftLength);
    result.diagonalTopLeftToBottomRightQualified = isInRange(
        result.diagonalTopLeftToBottomRight,
        config.quality.diagonalTopLeftToBottomRight);
    result.diagonalTopRightToBottomLeftQualified = isInRange(
        result.diagonalTopRightToBottomLeft,
        config.quality.diagonalTopRightToBottomLeft);
    result.angleQualified = isInRange(
        std::abs(result.angleDeg),
        config.quality.absoluteAngleDeg);
    result.centerOffsetQualified =
        result.centerOffset <= config.quality.maximumCenterOffset;
    result.edgeModeQualified =
        result.topQualified &&
        result.rightQualified &&
        result.bottomQualified &&
        result.leftQualified &&
        result.diagonalTopLeftToBottomRightQualified &&
        result.diagonalTopRightToBottomLeftQualified;
    result.offsetResultQualified =
        result.angleQualified && result.centerOffsetQualified;
    result.finalQualified =
        result.edgeModeQualified && result.offsetResultQualified;
    result.geometryAvailable = true;
    return result;
}

cv::Mat renderFactoryResult(
    const cv::Mat& original,
    const vision::algorithm::DemoEdgeVisualization& visualization,
    const vision::algorithm::detail::EdgeAlgorithmConfig& config,
    const vision::core::AlgorithmOutput& output)
{
    cv::Mat result;
    cv::cvtColor(original, result, cv::COLOR_GRAY2BGR);
    const EdgePostprocessMeasurements measurements =
        evaluatePostprocess(visualization, config);

    for (const auto& roi : visualization.rois) {
        std::vector<cv::Point> polygon;
        polygon.reserve(roi.corners.size());
        for (const auto& corner : roi.corners) {
            polygon.push_back(toPixel(corner));
        }
        cv::polylines(
            result,
            std::vector<std::vector<cv::Point>>{polygon},
            true,
            kYellow,
            3,
            cv::LINE_AA);
    }

    const std::array<bool, 4> sideStatus{
        measurements.topQualified,
        measurements.rightQualified,
        measurements.bottomQualified,
        measurements.leftQualified};
    for (std::size_t index = 0;
         index < visualization.fittedLines.size();
         ++index) {
        const bool qualified =
            index < sideStatus.size() && sideStatus[index];
        drawClippedLine(
            result,
            toPixel(visualization.fittedLines[index].start),
            toPixel(visualization.fittedLines[index].end),
            statusColor(qualified),
            4);
    }

    if (measurements.geometryAvailable) {
        for (std::size_t index = 0; index < measurements.corners.size(); ++index) {
            cv::circle(
                result,
                toPixel(measurements.corners[index]),
                11,
                kCyan,
                cv::FILLED,
                cv::LINE_AA);
        }

        cv::line(
            result,
            toPixel(measurements.corners[0]),
            toPixel(measurements.corners[2]),
            statusColor(
                measurements.diagonalTopLeftToBottomRightQualified),
            3,
            cv::LINE_AA);
        cv::line(
            result,
            toPixel(measurements.corners[1]),
            toPixel(measurements.corners[3]),
            statusColor(
                measurements.diagonalTopRightToBottomLeftQualified),
            3,
            cv::LINE_AA);

        cv::line(
            result,
            toPixel(measurements.sideMidpoints[3]),
            toPixel(measurements.sideMidpoints[1]),
            kCyan,
            2,
            cv::LINE_AA);
        cv::line(
            result,
            toPixel(measurements.sideMidpoints[0]),
            toPixel(measurements.sideMidpoints[2]),
            kCyan,
            2,
            cv::LINE_AA);

        const cv::Point imageCenter(
            visualization.rectifiedImage.cols / 2,
            visualization.rectifiedImage.rows / 2);
        const cv::Point poseCenter = toPixel(measurements.center);
        cv::drawMarker(
            result,
            imageCenter,
            kBlue,
            cv::MARKER_CROSS,
            70,
            5,
            cv::LINE_AA);
        cv::line(
            result,
            imageCenter,
            poseCenter,
            statusColor(measurements.centerOffsetQualified),
            4,
            cv::LINE_AA);
        cv::drawMarker(
            result,
            poseCenter,
            statusColor(measurements.offsetResultQualified),
            cv::MARKER_CROSS,
            70,
            6,
            cv::LINE_AA);
        cv::circle(
            result,
            poseCenter,
            20,
            statusColor(measurements.offsetResultQualified),
            4,
            cv::LINE_AA);

        const double radians = measurements.angleDeg * CV_PI / 180.0;
        const cv::Point angleEnd(
            cvRound(measurements.center.x + 380.0 * std::cos(radians)),
            cvRound(measurements.center.y + 380.0 * std::sin(radians)));
        cv::arrowedLine(
            result,
            poseCenter,
            angleEnd,
            statusColor(measurements.angleQualified),
            5,
            cv::LINE_AA,
            0,
            0.15);

        drawResultPanel(result, measurements, output);
    }

    return result;
}

bool saveFactoryResult(
    const cv::Mat& original,
    const vision::algorithm::DemoEdgeVisualization& visualization,
    const vision::algorithm::detail::EdgeAlgorithmConfig& config,
    const vision::core::AlgorithmOutput& output,
    const std::string& fileName,
    std::filesystem::path& outputPath)
{
    const auto runOutput = vision::test::support::environmentDirectory("VISIONTEST_OUTPUT");
    const std::filesystem::path outputDirectory = runOutput ? *runOutput / "EdgeImages" :
        vision::test::support::uniqueTestDirectory("edge-images");
    std::error_code fileError;
    std::filesystem::create_directories(outputDirectory, fileError);
    if (fileError) {
        return false;
    }
    outputPath = outputDirectory / fileName;
    return cv::imwrite(
        outputPath.string(),
        renderFactoryResult(original, visualization, config, output));
}

} // namespace vision::test::algorithm::edge_pipeline
