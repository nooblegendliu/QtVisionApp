#pragma once

#include "MarkTypes.h"
#include "../../extend/line2Dup.h"

#include <cstddef>
#include <vector>

#include <opencv2/core.hpp>

namespace vision::algorithm::detail {

    /*
        meiqua/shape_based_matching 的薄封装。

        本类只收拢原 test.cpp 中的以下流程：
        1. 生成并保存 Detector 模板文件和 Info 文件；
        2. 加载上述两个文件；
        3. 执行 Detector::match；
        4. 根据 template_id 取得 Info 中的 angle/scale；
        5. 根据配置决定是否对选中的匹配候选执行原库 ICP；
        6. 将第三方结果按值转换为 MatchResult。

        本类不负责算法计时、四相机调度、标定或位姿融合。
    */
    class ShapeMatcher final {
    public:
        /*
            构造时加载并校验模板文件和 Info 文件。
            构造成功后的对象只用于在线匹配。
        */
        explicit ShapeMatcher(ShapeMatcherConfig config);

        /*
            使用 config 中的建模参数生成两个原库模型文件。

            返回成功加入 Detector 并写入 Info 文件的模板数量。
            同一 classId 始终写入配置指定的固定路径；重建成功后覆盖旧模型，
            不创建版本目录。替换前会先校验临时模板文件与 Info 文件的一致性。
            这是离线建模入口，不修改任何在线 ShapeMatcher 实例。
        */
        [[nodiscard]] static std::size_t buildTemplates(
            const ShapeMatcherConfig& config,
            const cv::Mat& image,
            const cv::Mat& mask);

        /*
            对一张图像执行模板粗匹配、NMS，以及配置启用时的 ICP 精修。

            返回空集合表示 Detector 没有产生满足阈值的候选；第三方执行
            异常不转换为空集合，而是继续交由 VisionAlgorithm 模块边界处理。
            输入图像仅在本次同步调用期间读取，不会被保存或修改。
        */
        [[nodiscard]] std::vector<MatchResult> match(
            const cv::Mat& image) const;

    private:
        ShapeMatcherConfig config_;
        line2Dup::Detector detector_;
        std::vector<shape_based_matching::shapeInfo_producer::Info>
            templateInfos_;
    };

}   // namespace vision::algorithm::detail
