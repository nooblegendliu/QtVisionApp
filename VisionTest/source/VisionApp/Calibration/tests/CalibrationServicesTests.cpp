#include "TestSupport/TestDirectory.h"
#include "CalibrationServices.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

#include <opencv2/core.hpp>

namespace {

class TemporaryPointFile {
public:
    explicit TemporaryPointFile(const std::string& contents)
    {
        directory_ = vision::test::support::uniqueTestDirectory("point-file");
        path_ = directory_ / "arbitrary-file-name.data";
        std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    ~TemporaryPointFile()
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

class TemporaryCalibrationResources {
public:
    TemporaryCalibrationResources()
    {
        directory = vision::test::support::uniqueTestDirectory("calibration-transaction");
        for (const char* name : {
                 "center.yml", "top_left.yml", "top_right.yml",
                 "bottom_right.yml", "bottom_left.yml"}) {
            writeCalibration(directory / name, cv::Matx33d::eye());
        }
        mainConfig = directory / "algorithm.yml";
        cv::FileStorage output(
            mainConfig.string(),
            cv::FileStorage::WRITE | cv::FileStorage::FORMAT_YAML);
        output << "calibration" << "{"
               << "center" << "center.yml"
               << "top_left" << "top_left.yml"
               << "top_right" << "top_right.yml"
               << "bottom_right" << "bottom_right.yml"
               << "bottom_left" << "bottom_left.yml"
               << "}";
    }

    ~TemporaryCalibrationResources()
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    static void writeCalibration(
        const std::filesystem::path& path, const cv::Matx33d& transform)
    {
        cv::FileStorage output(
            path.string(),
            cv::FileStorage::WRITE | cv::FileStorage::FORMAT_YAML);
        output << "image_width" << 640;
        output << "image_height" << 480;
        output << "camera_matrix" << cv::Mat(cv::Matx33d::eye());
        output << "distortion_coefficients" << cv::Mat::zeros(1, 5, CV_64F);
        output << "new_camera_matrix" << cv::Mat(cv::Matx33d::eye());
        output << "image_to_physical" << cv::Mat(transform);
    }

    static cv::Matx33d readTransform(const std::filesystem::path& path)
    {
        cv::FileStorage input(path.string(), cv::FileStorage::READ);
        cv::Mat value;
        input["image_to_physical"] >> value;
        cv::Mat converted;
        value.convertTo(converted, CV_64F);
        cv::Matx33d result;
        std::copy(converted.begin<double>(), converted.end<double>(), result.val);
        return result;
    }

    std::filesystem::path directory;
    std::filesystem::path mainConfig;
};

std::string squareGridText(int side, bool bom = false)
{
    std::string text = bom ? "\xEF\xBB\xBF" : "";
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            text += "  " + std::to_string(column * 0.5) + " , " +
                std::to_string(row * 0.75) + "  \r\n";
        }
        text += "\r\n";
    }
    return text;
}

TEST(App_CalibrationPointFileTests, AcceptsBomWhitespaceBlankLinesAndArbitraryName)
{
    TemporaryPointFile file(squareGridText(5, true));
    const auto outcome = vision::app::loadCalibrationPointFile(file.path());
    ASSERT_TRUE(std::holds_alternative<vision::app::CalibrationPointFile>(outcome));
    const auto& result = std::get<vision::app::CalibrationPointFile>(outcome);
    EXPECT_EQ(result.path, file.path());
    EXPECT_EQ(result.gridSidePoints, 5);
    ASSERT_EQ(result.points.size(), 25U);
    EXPECT_DOUBLE_EQ(result.points.front().x, 0.0);
    EXPECT_DOUBLE_EQ(result.points.back().y, 3.0);
}

TEST(App_CalibrationPointFileTests, AcceptsMinimumAndMaximumGridSizes)
{
    for (const int side : {3, 15}) {
        TemporaryPointFile file(squareGridText(side));
        const auto outcome = vision::app::loadCalibrationPointFile(file.path());
        ASSERT_TRUE(std::holds_alternative<vision::app::CalibrationPointFile>(outcome));
        EXPECT_EQ(std::get<vision::app::CalibrationPointFile>(outcome)
                      .points.size(),
                  static_cast<std::size_t>(side * side));
    }
}

TEST(App_CalibrationPointFileTests, RejectsAnythingOtherThanExactlyTwoFiniteColumns)
{
    for (const std::string& invalid : {
             "X,Y\n0,0\n1,0\n0,1\n1,1\n2,2\n3,3\n4,4\n5,5\n6,6\n",
             "0,0,1\n",
             "0;0\n",
             "nan,0\n",
             "inf,0\n"}) {
        TemporaryPointFile file(invalid);
        const auto outcome = vision::app::loadCalibrationPointFile(file.path());
        ASSERT_TRUE(std::holds_alternative<vision::core::Error>(outcome));
        EXPECT_EQ(std::get<vision::core::Error>(outcome).code,
                  vision::core::ErrorCode::InvalidConfiguration);
    }
}

TEST(App_CalibrationPointFileTests, RejectsNonSquareDuplicateAndCollinearSets)
{
    const std::string duplicate =
        "0,0\n1,0\n2,0\n0,1\n1,1\n2,1\n0,2\n1,2\n1,2\n";
    const std::string collinear =
        "0,0\n1,1\n2,2\n3,3\n4,4\n5,5\n6,6\n7,7\n8,8\n";
    const std::string nonSquare =
        "0,0\n1,0\n2,0\n0,1\n1,1\n2,1\n0,2\n1,2\n";
    for (const auto& invalid : {duplicate, collinear, nonSquare}) {
        TemporaryPointFile file(invalid);
        const auto outcome = vision::app::loadCalibrationPointFile(file.path());
        ASSERT_TRUE(std::holds_alternative<vision::core::Error>(outcome));
        EXPECT_EQ(std::get<vision::core::Error>(outcome).code,
                  vision::core::ErrorCode::InvalidConfiguration);
    }
}

TEST(App_CalibrationOrientationTests, InfersAllEightCornerAndNeighbourCombinations)
{
    using Orientation = vision::algorithm::CalibrationGridOrientation;
    struct Case { std::size_t p1; std::size_t p2; Orientation expected; };
    constexpr std::size_t n = 5;
    const Case cases[] = {
        {0, 1, Orientation::P1TopLeftXRight},
        {0, n, Orientation::P1TopLeftXDown},
        {n - 1, n - 2, Orientation::P1TopRightXLeft},
        {n - 1, 2 * n - 1, Orientation::P1TopRightXDown},
        {n * n - 1, n * n - 2, Orientation::P1BottomRightXLeft},
        {n * n - 1, n * (n - 1) - 1, Orientation::P1BottomRightXUp},
        {n * (n - 1), n * (n - 1) + 1, Orientation::P1BottomLeftXRight},
        {n * (n - 1), n * (n - 2), Orientation::P1BottomLeftXUp},
    };
    for (const auto& test : cases) {
        const auto outcome = vision::app::inferCalibrationOrientation(
            static_cast<int>(n), test.p1, test.p2);
        ASSERT_TRUE(std::holds_alternative<Orientation>(outcome));
        EXPECT_EQ(std::get<Orientation>(outcome), test.expected);
    }
}

TEST(App_CalibrationOrientationTests, RejectsNonCornerAndNonNeighbourSelections)
{
    for (const auto [p1, p2] : {
             std::pair<std::size_t, std::size_t>{6, 7},
             {0, 2},
             {20, 14},
             {24, 24}}) {
        const auto outcome = vision::app::inferCalibrationOrientation(5, p1, p2);
        ASSERT_TRUE(std::holds_alternative<vision::core::Error>(outcome));
        EXPECT_EQ(std::get<vision::core::Error>(outcome).code,
                  vision::core::ErrorCode::InvalidConfiguration);
    }
}

TEST(App_CalibrationFileTransactionTests, RollbackRestoresOriginalCenterFile)
{
    TemporaryCalibrationResources resources;
    const cv::Matx33d candidate(
        0.5, 0.0, 12.0,
        0.0, 0.5, -3.0,
        0.0, 0.0, 1.0);
    auto outcome = vision::app::CalibrationFileTransaction::stageAndReplace(
        resources.mainConfig,
        {{vision::core::CameraRole::Center, candidate}});
    ASSERT_TRUE(std::holds_alternative<
        vision::app::CalibrationFileTransaction>(outcome));
    auto transaction = std::move(std::get<
        vision::app::CalibrationFileTransaction>(outcome));
    EXPECT_EQ(
        TemporaryCalibrationResources::readTransform(
            resources.directory / "center.yml"),
        candidate);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(transaction.rollback()));
    EXPECT_EQ(
        TemporaryCalibrationResources::readTransform(
            resources.directory / "center.yml"),
        cv::Matx33d::eye());
}

TEST(App_CalibrationFileTransactionTests, CommitKeepsAllFourCornerFiles)
{
    TemporaryCalibrationResources resources;
    const cv::Matx33d candidate(
        0.25, 0.01, 2.0,
        -0.02, 0.3, 5.0,
        0.0001, 0.0002, 1.0);
    vision::app::CalibrationFileTransaction::MatrixMap matrices{
        {vision::core::CameraRole::TopLeft, candidate},
        {vision::core::CameraRole::TopRight, candidate},
        {vision::core::CameraRole::BottomRight, candidate},
        {vision::core::CameraRole::BottomLeft, candidate}};
    auto outcome = vision::app::CalibrationFileTransaction::stageAndReplace(
        resources.mainConfig, matrices);
    ASSERT_TRUE(std::holds_alternative<
        vision::app::CalibrationFileTransaction>(outcome));
    auto transaction = std::move(std::get<
        vision::app::CalibrationFileTransaction>(outcome));
    ASSERT_TRUE(std::holds_alternative<std::monostate>(transaction.commit()));
    for (const char* name : {
             "top_left.yml", "top_right.yml",
             "bottom_right.yml", "bottom_left.yml"}) {
        EXPECT_EQ(
            TemporaryCalibrationResources::readTransform(
                resources.directory / name),
            candidate);
        EXPECT_FALSE(std::filesystem::exists(
            resources.directory / (std::string(name) + ".candidate.tmp")));
        EXPECT_FALSE(std::filesystem::exists(
            resources.directory / (std::string(name) + ".calibration.bak")));
    }
}

} // namespace
