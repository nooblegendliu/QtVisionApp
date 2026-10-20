#include <VisionAlgorithm/VisionAlgorithm.h>

#include <gtest/gtest.h>

namespace vision::test::algorithm::creation {
namespace {

vision::algorithm::DemoEdgeVisualization makeVisualization() {
    vision::algorithm::DemoEdgeVisualization value;
    value.rectifiedImage = cv::Mat(3, 4, CV_8UC1, cv::Scalar{17}).clone();
    value.rois.push_back({{{{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}, {7.0, 8.0}}}});
    value.fittedLines.push_back({{9.0, 10.0}, {11.0, 12.0}});
    value.corners.push_back({13.0, 14.0});
    return value;
}

TEST(Algorithm_DemoEdgeVisualizationStoreTests, PublishDeepCopiesImageAndGeometryValues) {
    vision::algorithm::DemoEdgeVisualizationStore store;
    auto source = makeVisualization();
    store.publish(source);

    source.rectifiedImage.setTo(cv::Scalar{99});
    source.rois.front().corners.front().x = -1.0;
    source.fittedLines.front().start.y = -2.0;
    source.corners.front().x = -3.0;
    const auto snapshot = store.snapshot();

    ASSERT_FALSE(snapshot.rectifiedImage.empty());
    EXPECT_EQ(snapshot.rectifiedImage.at<unsigned char>(0, 0), 17U);
    EXPECT_DOUBLE_EQ(snapshot.rois.front().corners.front().x, 1.0);
    EXPECT_DOUBLE_EQ(snapshot.fittedLines.front().start.y, 10.0);
    EXPECT_DOUBLE_EQ(snapshot.corners.front().x, 13.0);
}

TEST(Algorithm_DemoEdgeVisualizationStoreTests, SnapshotIsIndependentFromStoredValue) {
    vision::algorithm::DemoEdgeVisualizationStore store;
    store.publish(makeVisualization());
    auto first = store.snapshot();
    first.rectifiedImage.setTo(cv::Scalar{77});
    first.rois.front().corners.front().x = -10.0;
    first.fittedLines.clear();
    first.corners.clear();

    const auto second = store.snapshot();

    EXPECT_EQ(second.rectifiedImage.at<unsigned char>(0, 0), 17U);
    EXPECT_DOUBLE_EQ(second.rois.front().corners.front().x, 1.0);
    EXPECT_EQ(second.fittedLines.size(), 1U);
    EXPECT_EQ(second.corners.size(), 1U);
}

TEST(Algorithm_DemoEdgeVisualizationStoreTests, ClearRemovesAllPublishedState) {
    vision::algorithm::DemoEdgeVisualizationStore store;
    store.publish(makeVisualization());

    store.clear();
    const auto snapshot = store.snapshot();

    EXPECT_TRUE(snapshot.rectifiedImage.empty());
    EXPECT_TRUE(snapshot.rois.empty());
    EXPECT_TRUE(snapshot.fittedLines.empty());
    EXPECT_TRUE(snapshot.corners.empty());
}

} // namespace
} // namespace vision::test::algorithm::creation
