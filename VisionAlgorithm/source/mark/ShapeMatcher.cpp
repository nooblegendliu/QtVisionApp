#include "ShapeMatcher.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

#include <cuda_icp/icp.h>
#include <opencv2/dnn/dnn.hpp>


namespace vision::algorithm::detail {

    namespace {

        using ShapeInfoProducer =
            shape_based_matching::shapeInfo_producer;


        // 返回错误信息
        std::runtime_error makeError(
            const std::string& field,
            const std::string& message)
        {
            return std::runtime_error(field + ": " + message);
        }

        std::invalid_argument modelConfigurationError(
            const std::string& field,
            const std::string& message)
        {
            return std::invalid_argument(field + ": " + message);
        }

        bool isOpenCvConfigurationFormatError(
            const cv::Exception& exception) noexcept
        {
            switch (exception.code) {
            case cv::Error::StsBadArg:
            case cv::Error::StsObjectNotFound:
            case cv::Error::StsParseError:
            case cv::Error::StsUnsupportedFormat:
                return true;
            default:
                return false;
            }
        }

        // 返回角度/尺度范围
        std::vector<float> makeRange(float lhs, float rhs)
        {
            if (!std::isfinite(lhs) ||
                !std::isfinite(rhs)) {
                throw std::invalid_argument(
                    "range value must be finite");
            }
           
            constexpr float epsilon = 1.0e-6f;

            if (std::abs(lhs - rhs) < epsilon) {
                return { lhs };
            }

            if (lhs > rhs) {
                throw std::invalid_argument("lhs must be less than or eq to rhs");
            }

            return { lhs, rhs };
        }


        cv::Point2f calculateFeatureBoundsCenter(
            const line2Dup::Template& templ)
        {
            if (templ.features.empty()) {
                throw makeError(
                    "match shape model failed",
                    "level-zero template contains no features");
            }

            int minX = templ.features.front().x;
            int maxX = minX;
            int minY = templ.features.front().y;
            int maxY = minY;

            for (const auto& feature : templ.features) {
                minX = std::min(minX, feature.x);
                maxX = std::max(maxX, feature.x);
                minY = std::min(minY, feature.y);
                maxY = std::max(maxY, feature.y);
            }

            return {
                static_cast<float>(minX) +
                    static_cast<float>(maxX - minX) * 0.5F,
                static_cast<float>(minY) +
                    static_cast<float>(maxY - minY) * 0.5F
            };
        }


        void validateTemplateFilePattern(
            const std::filesystem::path& pattern)
        {
            if (pattern.empty()) {
                throw modelConfigurationError("templateFilePattern",
                    "path pattern must not be empty");
            }

            const std::string text = pattern.string();
            const std::size_t placeholder = text.find("%s");

            if (placeholder == std::string::npos) {
                throw modelConfigurationError(
                    "templateFilePattern",
                    "path pattern must contain exactly one '%s'");
            }

            if (text.find("%s", placeholder + 2) !=
                std::string::npos) {
                throw modelConfigurationError(
                    "templateFilePattern",
                    "path pattern must contain only one '%s'");
            }

            std::string remaining = text;
            remaining.erase(placeholder, 2);

            if (remaining.find('%') != std::string::npos) {
                throw modelConfigurationError(
                    "templateFilePattern",
                    "path pattern contains an unsupported format specifier");
            }
        }


        [[nodiscard]] std::filesystem::path resolveTemplateFile(
            const std::filesystem::path& pattern,
            const std::string& classId)
        {
            std::string resolved = pattern.string();
            resolved.replace(resolved.find("%s"), 2, classId);
            return std::filesystem::path(std::move(resolved));
        }


        [[nodiscard]] std::filesystem::path addFileNameTag(
            const std::filesystem::path& path,
            std::string_view tag)
        {
            return path.parent_path() /
                (path.stem().string() + std::string(tag) +
                    path.extension().string());
        }


        void ensureParentDirectory(const std::filesystem::path& path)
        {
            const std::filesystem::path parent = path.parent_path();
            if (parent.empty()) {
                return;
            }

            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                throw makeError(
                    "write shape model failed",
                    "cannot create directory '" + parent.string() +
                    "': " + error.message());
            }
        }


        void requireReadableModelFile(
            const std::filesystem::path& path,
            std::string_view description)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error) {
                throw modelConfigurationError(
                    "load shape model failed",
                    std::string(description) + " '" + path.string() +
                    "' is missing or is not a regular file");
            }

            const std::uintmax_t size =
                std::filesystem::file_size(path, error);
            if (error || size == 0U) {
                throw modelConfigurationError(
                    "load shape model failed",
                    std::string(description) + " '" + path.string() +
                    "' is empty or cannot be inspected");
            }

            std::ifstream stream(path, std::ios::binary);
            if (!stream.is_open()) {
                throw modelConfigurationError(
                    "load shape model failed",
                    std::string(description) + " '" + path.string() +
                    "' is not readable");
            }
        }


        void validateModelPaths(const ShapeMatcherConfig& config)
        {
            validateTemplateFilePattern(config.templateFilePattern);

            if (config.classId.empty()) {
                throw modelConfigurationError(
                    "shape model configuration",
                    "classId must not be empty");
            }

            if (config.templateInfoFile.empty()) {
                throw modelConfigurationError(
                    "shape model configuration",
                    "templateInfoFile must not be empty");
            }

            if (resolveTemplateFile(
                    config.templateFilePattern,
                    config.classId).lexically_normal() ==
                config.templateInfoFile.lexically_normal()) {
                throw modelConfigurationError(
                    "shape model configuration",
                    "template file and template info file must be different");
            }
        }


        void renameModelFile(
            const std::filesystem::path& source,
            const std::filesystem::path& destination)
        {
            std::error_code error;
            std::filesystem::rename(source, destination, error);
            if (error) {
                throw makeError(
                    "replace shape model failed",
                    "cannot rename '" + source.string() + "' to '" +
                    destination.string() + "': " + error.message());
            }
        }


        void removeModelFileIfPresent(
            const std::filesystem::path& path)
        {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if (error) {
                throw makeError(
                    "replace shape model failed",
                    "cannot inspect '" + path.string() + "': " +
                    error.message());
            }

            if (!exists) {
                return;
            }

            if (!std::filesystem::remove(path, error) || error) {
                throw makeError(
                    "replace shape model failed",
                    "cannot remove '" + path.string() + "': " +
                    error.message());
            }
        }


        void discardModelFile(const std::filesystem::path& path) noexcept
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }


        [[nodiscard]] bool modelFileExists(
            const std::filesystem::path& path)
        {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if (error) {
                throw makeError(
                    "replace shape model failed",
                    "cannot inspect '" + path.string() + "': " +
                    error.message());
            }
            return exists;
        }


        void recoverInterruptedReplacement(
            const std::filesystem::path& templateFile,
            const std::filesystem::path& infoFile,
            const std::filesystem::path& templateBackup,
            const std::filesystem::path& infoBackup)
        {
            const bool hasTemplateBackup = modelFileExists(templateBackup);
            const bool hasInfoBackup = modelFileExists(infoBackup);
            if (!hasTemplateBackup && !hasInfoBackup) {
                return;
            }

            // Both final files are installed only after the temporary pair has
            // passed model validation. Backups left in this state are stale.
            if (modelFileExists(templateFile) && modelFileExists(infoFile)) {
                discardModelFile(templateBackup);
                discardModelFile(infoBackup);
                return;
            }

            removeModelFileIfPresent(templateFile);
            removeModelFileIfPresent(infoFile);

            if (hasTemplateBackup) {
                renameModelFile(templateBackup, templateFile);
            }
            if (hasInfoBackup) {
                renameModelFile(infoBackup, infoFile);
            }
        }


        void replaceModelFiles(
            const std::filesystem::path& temporaryTemplate,
            const std::filesystem::path& temporaryInfo,
            const std::filesystem::path& templateFile,
            const std::filesystem::path& infoFile)
        {
            const std::filesystem::path templateBackup =
                addFileNameTag(templateFile, ".bak");
            const std::filesystem::path infoBackup =
                addFileNameTag(infoFile, ".bak");

            recoverInterruptedReplacement(
                templateFile,
                infoFile,
                templateBackup,
                infoBackup);

            bool templateBackedUp = false;
            bool infoBackedUp = false;
            bool templateInstalled = false;
            bool infoInstalled = false;

            try {
                if (modelFileExists(templateFile)) {
                    renameModelFile(templateFile, templateBackup);
                    templateBackedUp = true;
                }
                if (modelFileExists(infoFile)) {
                    renameModelFile(infoFile, infoBackup);
                    infoBackedUp = true;
                }

                renameModelFile(temporaryTemplate, templateFile);
                templateInstalled = true;
                renameModelFile(temporaryInfo, infoFile);
                infoInstalled = true;
            }
            catch (...) {
                const std::exception_ptr originalFailure =
                    std::current_exception();
                std::string rollbackFailure;

                const auto recordRollbackFailure =
                    [&rollbackFailure](
                        std::string_view operation,
                        const std::error_code& error) {
                        if (!error) {
                            return;
                        }
                        if (!rollbackFailure.empty()) {
                            rollbackFailure += "; ";
                        }
                        rollbackFailure += std::string(operation) +
                            ": " + error.message();
                    };

                std::error_code error;
                if (templateInstalled) {
                    std::filesystem::remove(templateFile, error);
                    recordRollbackFailure("remove new template", error);
                }
                error.clear();
                if (infoInstalled) {
                    std::filesystem::remove(infoFile, error);
                    recordRollbackFailure("remove new info", error);
                }
                error.clear();
                if (templateBackedUp) {
                    std::filesystem::rename(
                        templateBackup,
                        templateFile,
                        error);
                    recordRollbackFailure("restore old template", error);
                }
                error.clear();
                if (infoBackedUp) {
                    std::filesystem::rename(
                        infoBackup,
                        infoFile,
                        error);
                    recordRollbackFailure("restore old info", error);
                }

                if (!rollbackFailure.empty()) {
                    throw makeError(
                        "replace shape model failed",
                        "rollback was incomplete: " + rollbackFailure);
                }
                std::rethrow_exception(originalFailure);
            }

            discardModelFile(templateBackup);
            discardModelFile(infoBackup);
        }


        void validateLoadedModel(const line2Dup::Detector& detector,
            const std::vector<ShapeInfoProducer::Info>& infos,
            const ShapeMatcherConfig& config)
        {
            const std::vector<std::string> classIds = detector.classIds();
            if (std::find(
                    classIds.begin(),
                    classIds.end(),
                    config.classId) == classIds.end()) {
                throw modelConfigurationError(
                    "load shape model failed",
                    "template file does not contain classId '" +
                    config.classId + "'");
            }

            const int templateCount =
                detector.numTemplates(config.classId);
            if (templateCount <= 0) {
                throw modelConfigurationError(
                    "load shape model failed",
                    "classId '" + config.classId +
                    "' does not contain any templates");
            }

            if (infos.size() != static_cast<std::size_t>(templateCount)) {
                throw modelConfigurationError(
                    "load shape model failed",
                    "template count " + std::to_string(templateCount) +
                    " does not match info count " +
                    std::to_string(infos.size()));
            }

            for (int templateId = 0;
                templateId < templateCount;
                ++templateId) {
                const auto& levels = detector.getTemplates(
                    config.classId,
                    templateId);
                if (levels.size() != config.detector.pyramidT.size()) {
                    throw modelConfigurationError(
                        "load shape model failed",
                        "templateId " + std::to_string(templateId) +
                        " pyramid level count " +
                        std::to_string(levels.size()) +
                        " does not match detector configuration " +
                        std::to_string(config.detector.pyramidT.size()));
                }

                for (std::size_t level = 0;
                    level < levels.size();
                    ++level) {
                    const auto& templ = levels[level];
                    if (templ.width <= 0 || templ.height <= 0 ||
                        templ.features.empty() ||
                        templ.pyramid_level != static_cast<int>(level)) {
                        throw modelConfigurationError(
                            "load shape model failed",
                            "templateId " + std::to_string(templateId) +
                            " contains an invalid pyramid level");
                    }
                }

                const auto& info = infos[templateId];
                if (!std::isfinite(info.angle) ||
                    !std::isfinite(info.scale) ||
                    info.scale <= 0.0F) {
                    throw modelConfigurationError(
                        "load shape model failed",
                        "templateId " + std::to_string(templateId) +
                        " contains invalid angle or scale metadata");
                }
            }
        }


        [[nodiscard]] const std::vector<line2Dup::Template>&
            validatedTemplateLevels(
                const line2Dup::Detector& detector,
                const std::string& classId,
                int templateId,
                std::size_t infoCount)
        {
            const int templateCount = detector.numTemplates(classId);
            if (templateId < 0 || templateId >= templateCount ||
                static_cast<std::size_t>(templateId) >= infoCount) {
                throw makeError(
                    "match shape model failed",
                    "detector returned out-of-range templateId " +
                    std::to_string(templateId));
            }

            const auto& levels = detector.getTemplates(classId, templateId);
            if (levels.empty()) {
                throw makeError(
                    "match shape model failed",
                    "templateId " + std::to_string(templateId) +
                    " does not contain a pyramid level");
            }
            return levels;
        }


    }   // namespace


    std::size_t ShapeMatcher::buildTemplates(
        const ShapeMatcherConfig& config,
        const cv::Mat& image,
        const cv::Mat& mask)
    {
        validateModelPaths(config);

        // 1.输入校验
        if (image.empty() || (image.type() != CV_8UC1 && image.type() != CV_8UC3)) {
            throw makeError("build template failed", "invalid input");
        }
        if (!mask.empty() && (mask.type() != CV_8UC1 || mask.size() != image.size())) {
            throw makeError("build template failed", "invalid mask");
        }


        const auto& build_config = config.build;
        const int padding = build_config.templatePadding;

        const cv::Rect destinationRoi(
            padding,
            padding,
            image.cols,
            image.rows);

        cv::Mat paddedImage(
            image.rows + 2 * padding,
            image.cols + 2 * padding,
            image.type(),
            cv::Scalar::all(0));
        image.copyTo(paddedImage(destinationRoi));

        const cv::Mat effectiveMask = mask.empty()
            ? cv::Mat(image.size(), CV_8UC1, cv::Scalar(255))
            : mask;

        cv::Mat paddedMask(
            effectiveMask.rows + 2 * padding,
            effectiveMask.cols + 2 * padding,
            CV_8UC1,
            cv::Scalar(0));
        effectiveMask.copyTo(paddedMask(destinationRoi));

        ShapeInfoProducer shapes(paddedImage, paddedMask);
        shapes.angle_range = makeRange(build_config.angleMinDeg, build_config.angleMaxDeg);
        shapes.angle_step = build_config.angleStepDeg;
        shapes.scale_range = makeRange(build_config.scaleMin, build_config.scaleMax);
        shapes.scale_step = build_config.scaleStep;
        shapes.produce_infos();

        const auto& d_cfg = config.detector;
        line2Dup::Detector detector(
            d_cfg.numFeatures,
            d_cfg.pyramidT,
            d_cfg.weakThreshold,
            d_cfg.strongThreshold);


        std::vector<ShapeInfoProducer::Info> infos_have_templ;
        infos_have_templ.reserve(shapes.infos.size());
        const std::string& class_id = config.classId;


        bool has_base_template = false;
        int base_template_id = -1;
        float base_angle_of_scale = 0.0f;
        float current_scale = std::numeric_limits<float>::quiet_NaN();
        const cv::Point2f train_center(paddedImage.cols / 2.0f, paddedImage.rows / 2.0f);
       
        for (const auto& info : shapes.infos) {
            int templ_id = -1;
            const bool scale_changed = std::isnan(current_scale) ||
                std::fabs(info.scale - current_scale) > 1.0e-6f;

            // 新的尺寸批次
            if (scale_changed) {
                current_scale = info.scale;
                has_base_template = false;      // 新批次没有可供旋转的基础模板
                base_template_id = -1;
                base_angle_of_scale = 0.0f;
            }

            if (!has_base_template) {
                /*
                * 当前尺度没有成功生成的基础模板
                */
                const int scale_feature_count = static_cast<int>(d_cfg.numFeatures * info.scale);
                templ_id =
                    detector.addTemplate(shapes.src_of(info), class_id, shapes.mask_of(info), scale_feature_count);
                if (templ_id == -1) continue;

                // 当前角度成为基准模板
                base_template_id = templ_id;
                base_angle_of_scale = info.angle;
                has_base_template = true;
            }
            else {
                /*
                    当前尺度已有基础模板，后续角度直接旋转基础模板的特征
                */
                templ_id = detector.addTemplate_rotate(
                    class_id, base_template_id, info.angle - base_angle_of_scale, train_center);
            }

            if (templ_id != -1) {
                infos_have_templ.push_back(info);
            }
        }

        if (infos_have_templ.empty()) return 0;

        const std::filesystem::path templateFile =
            resolveTemplateFile(
                config.templateFilePattern,
                config.classId);
        const std::filesystem::path temporaryTemplatePattern =
            addFileNameTag(config.templateFilePattern, ".tmp");
        const std::filesystem::path temporaryTemplateFile =
            resolveTemplateFile(
                temporaryTemplatePattern,
                config.classId);
        const std::filesystem::path temporaryInfoFile =
            addFileNameTag(config.templateInfoFile, ".tmp");

        ensureParentDirectory(templateFile);
        ensureParentDirectory(config.templateInfoFile);
        removeModelFileIfPresent(temporaryTemplateFile);
        removeModelFileIfPresent(temporaryInfoFile);

        try {
            detector.writeClasses(temporaryTemplatePattern.string());
            ShapeInfoProducer::save_infos(
                infos_have_templ,
                temporaryInfoFile.string());

            ShapeMatcherConfig temporaryConfig = config;
            temporaryConfig.templateFilePattern =
                temporaryTemplatePattern;
            temporaryConfig.templateInfoFile = temporaryInfoFile;
            const ShapeMatcher validatedTemporaryModel(
                std::move(temporaryConfig));
            (void)validatedTemporaryModel;

            replaceModelFiles(
                temporaryTemplateFile,
                temporaryInfoFile,
                templateFile,
                config.templateInfoFile);
        }
        catch (...) {
            std::error_code ignored;
            std::filesystem::remove(temporaryTemplateFile, ignored);
            ignored.clear();
            std::filesystem::remove(temporaryInfoFile, ignored);
            throw;
        }

        return infos_have_templ.size();
    }



    ShapeMatcher::ShapeMatcher(ShapeMatcherConfig config) 
        : config_(std::move(config)),
        detector_(
            config_.detector.numFeatures,
            config_.detector.pyramidT,
            config_.detector.weakThreshold,
            config_.detector.strongThreshold)
    {
        validateModelPaths(config_);

        const std::filesystem::path templateFile =
            resolveTemplateFile(
                config_.templateFilePattern,
                config_.classId);
        try {
            recoverInterruptedReplacement(
                templateFile,
                config_.templateInfoFile,
                addFileNameTag(templateFile, ".bak"),
                addFileNameTag(config_.templateInfoFile, ".bak"));
            requireReadableModelFile(templateFile, "template file");
            requireReadableModelFile(
                config_.templateInfoFile,
                "template info file");

            const std::vector<std::string> classIds{ config_.classId };

            detector_.readClasses(
                classIds,
                config_.templateFilePattern.string());

            auto loadedInfos = ShapeInfoProducer::load_infos(
                config_.templateInfoFile.string());

            validateLoadedModel(detector_, loadedInfos, config_);

            templateInfos_ = std::move(loadedInfos);
        }
        catch (const std::invalid_argument&) {
            throw;
        }
        catch (const cv::Exception& exception) {
            if (!isOpenCvConfigurationFormatError(exception)) {
                throw;
            }

            throw modelConfigurationError(
                "load shape model failed",
                exception.what());
        }
        catch (const std::runtime_error& exception) {
            throw modelConfigurationError(
                "load shape model failed",
                exception.what());
        }
    }


    std::vector<MatchResult> ShapeMatcher::match(const cv::Mat& image) const
    {
        const auto& cfg = config_.match;
        const int padding = cfg.sourcePadding;

        // 1.补边
        cv::Mat paddedImage(image.rows + 2 * padding, image.cols + 2 * padding, image.type(), cv::Scalar::all(0));
        image.copyTo(paddedImage(cv::Rect(padding, padding, image.cols, image.rows)));

        // 2.按照原test.cpp的方式对齐图像尺寸
        const int stride = cfg.stride;
        const int alignedRows = paddedImage.rows / stride * stride;
        const int alignedCols = paddedImage.cols / stride * stride;

        cv::Mat matchImage = paddedImage(cv::Rect(0, 0, alignedCols, alignedRows)).clone();

        // 3.模板粗匹配
        const std::vector<std::string> classIds{ config_.classId };

        const std::vector<line2Dup::Match> raw_matches = detector_.match(matchImage, cfg.similarityThreshold, classIds);

        // 4.为每个候选构造nms矩形
        std::vector<cv::Rect> boxes;
        std::vector<float> scores;

        boxes.reserve(raw_matches.size());
        scores.reserve(raw_matches.size());
        for (const auto& raw : raw_matches) {
            const auto& levels = validatedTemplateLevels(
                detector_,
                config_.classId,
                raw.template_id,
                templateInfos_.size());
            const auto& templ = levels[0];

            boxes.emplace_back(raw.x, raw.y, templ.width, templ.height);
            scores.push_back(raw.similarity);
        }

        std::vector<int> keptIndices;
        cv::dnn::NMSBoxes(
            boxes,
            scores,
            cfg.similarityThreshold,
            cfg.nmsThreshold,
            keptIndices,
            1.0F,
            cfg.maxResults);

        Scene_edge scene{};
        std::vector<::Vec2f> scenePoints;
        std::vector<::Vec2f> sceneNormals;

        // 精定位总开关：false 时不构造 ICP 场景，也不执行 ICP。
        if (cfg.enableIcpRefinement) {
            scene.init_Scene_edge_cpu(matchImage, scenePoints, sceneNormals);
        }

        std::vector<MatchResult> results;
        results.reserve(keptIndices.size());

        for (const int index : keptIndices) {
            if (index < 0 ||
                static_cast<std::size_t>(index) >= raw_matches.size()) {
                throw makeError(
                    "match shape model failed",
                    "NMS returned an out-of-range match index");
            }

            const auto& raw = raw_matches[index];
            const auto& levels = validatedTemplateLevels(
                detector_,
                config_.classId,
                raw.template_id,
                templateInfos_.size());
            const auto& templ = levels[0];
            const auto& info = templateInfos_[raw.template_id];
            const cv::Point2f featureCenter =
                calculateFeatureBoundsCenter(templ);

            cv::Point2f referencePointInMatchImage(
                static_cast<float>(raw.x) + featureCenter.x,
                static_cast<float>(raw.y) + featureCenter.y);

            MatchResult result;
            result.classId = raw.class_id;
            result.templateId = raw.template_id;
            result.similarity = raw.similarity;
            result.angleDeg = info.angle;
            result.scale = info.scale;

            // 精定位功能全部限制在这个 if 中，按配置可直接开闭。
            if (cfg.enableIcpRefinement) {
                std::vector<::Vec2f> modelPoints;
                modelPoints.reserve(templ.features.size());

                for (const auto& feature : templ.features) {
                    modelPoints.push_back({
                        static_cast<float>(feature.x + raw.x),
                        static_cast<float>(feature.y + raw.y)
                    });
                }

                const cuda_icp::RegistrationResult registration =
                    cuda_icp::ICP2D_Point2Plane_cpu(modelPoints, scene);
                const auto& transformation = registration.transformation_;

                referencePointInMatchImage = cv::Point2f(
                    transformation[0][0] * referencePointInMatchImage.x +
                        transformation[0][1] * referencePointInMatchImage.y +
                        transformation[0][2],
                    transformation[1][0] * referencePointInMatchImage.x +
                        transformation[1][1] * referencePointInMatchImage.y +
                        transformation[1][2]);

                for (int row = 0; row < 3; ++row) {
                    for (int col = 0; col < 3; ++col) {
                        result.icpTransformation(row, col) =
                            transformation[row][col];
                    }
                }

                const float icpAngleDeg =
                    std::atan2(transformation[1][0], transformation[0][0]) *
                    180.0F / static_cast<float>(CV_PI);
                const float icpScale = std::hypot(
                    transformation[0][0],
                    transformation[1][0]);

                result.angleDeg -= icpAngleDeg;
                result.scale *= icpScale;
                result.icpInlierRmse = registration.inlier_rmse_;
                result.icpFitness = registration.fitness_;
            }

            // 返回坐标属于原始输入 image，去掉在线匹配时增加的补边。
            result.matchPositionPx = referencePointInMatchImage -
                cv::Point2f(
                    static_cast<float>(padding),
                    static_cast<float>(padding));

            results.push_back(std::move(result));
        }

        return results;
    }









}   // namespace vision::algorithm::detail
