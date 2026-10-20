#include "AlgorithmConfig.h"

#include <opencv2/core.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vision::algorithm::detail {
	namespace
	{
		std::invalid_argument configurationError(
			const std::filesystem::path& path,
			std::string_view message)
		{
			return std::invalid_argument(
				"Invalid algorithm configuration '" + path.string() + "': " +
				std::string(message));
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

		/* @brief: 文件有效性验证 */
		void requireReadableFile(const std::filesystem::path& path,
			std::string_view description)
		{
			std::error_code error;
			const bool exists = std::filesystem::exists(path, error);

			if (error || !exists) {
				throw configurationError(
					path,
					std::string(description) + " does not exists or cannot be accessed.");
			}

			if (!std::filesystem::is_regular_file(path, error) || error) {
				throw configurationError(
					path,
					std::string(description) + " is not a regular file.");
			}
		}


		/* @brief: 打开文件夹 */
		cv::FileStorage openFileStorage(
			const std::filesystem::path& path,
			std::string_view description)
		{
			requireReadableFile(path, description);

			try {
				cv::FileStorage storage(path.string(), cv::FileStorage::READ);
				if (!storage.isOpened()) {
					throw configurationError(path,
						std::string("OpenCV FileStorage could not open ") +
						std::string(description) + ".");
				}

				return storage;
			}
			catch (const std::invalid_argument&) {
				throw;
			}
			catch (const cv::Exception& exception) {
				if (!isOpenCvConfigurationFormatError(exception)) {
					throw;
				}

				throw configurationError(
					path,
					std::string("OpenCV could not parse ") +
					std::string(description) + ": " +
					exception.what());
			}
		}


		/* @brief: 获取文件内容 */
		const cv::FileNode requireMapNode(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const cv::FileNode node = parent[key];
			if (node.empty() || !node.isMap()) {
				throw configurationError(
					path,
					std::string("required map node '") + key + "' is missing.");
			}
			return node;
		}

		/* @brief: 获取整数参数 */
		int requireInteger(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const cv::FileNode node = parent[key];
			if (node.empty() || !node.isInt()) {
				throw configurationError(
					path,
					std::string("required integer '") + key + "' is missing.");
			}

			return static_cast<int>(node);
		}


		/* @brief: 获取字符串 */
		std::string requireString(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const cv::FileNode node = parent[key];
			if (node.empty() || !node.isString()) {
				throw configurationError(
					path,
					std::string("required string '") + key + "' is missing.");
			}

			const std::string value = static_cast<std::string>(node);
			if (value.empty()) {
				throw configurationError(
					path,
					std::string("string '") + key + "' must not be empty.");
			}

			return value;
		}


		// 0804.add
		bool requireEnableFlag(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& configPath)
		{
			const int value = requireInteger(
				parent,
				key,
				configPath);

			if (value != 0 && value != 1) {
				throw configurationError(
					configPath,
					std::string("enable flag '") + key +
					"' must be either 0 or 1.");
			}

			return value == 1;
		}


		std::filesystem::path normalizeExistingPath(
			const std::filesystem::path& path)
		{
			std::error_code error;
			const auto absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		std::filesystem::path resolveReferencedPath(
			const std::filesystem::path& ownerConfigPath,
			const std::string& configuredPath)
		{
			std::filesystem::path result(configuredPath);
			if (result.is_relative()) {
				result = ownerConfigPath.parent_path() / result;
			}
			return normalizeExistingPath(result);
		}

		// ------------------------------------------------------------------------
		// add for edge
		// ------------------------------------------------------------------------
		double requireNumber(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const cv::FileNode node = parent[key];

			if (node.empty() || (!node.isReal() && !node.isInt())) {
				throw configurationError(
					path,
					std::string("required number '") +
					key + "' is missing.");
			}

			return static_cast<double>(node);
		}

		MeasurementRangeConfig loadMeasurementRange(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const cv::FileNode rangeNode =
				requireMapNode(parent, key, path);

			MeasurementRangeConfig range;
			range.lower = requireNumber(rangeNode, "lower", path);
			range.upper = requireNumber(rangeNode, "upper", path);

			if (!std::isfinite(range.lower) ||
				!std::isfinite(range.upper) ||
				range.lower < 0.0 ||
				range.upper < range.lower) {
				throw configurationError(
					path,
					std::string("measurement range '") + key +
					"' must satisfy 0 <= lower <= upper.");
			}

			return range;
		}


		MeasurementRangeConfig loadSignedMeasurementRange(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const cv::FileNode rangeNode =
				requireMapNode(parent, key, path);

			MeasurementRangeConfig range;
			range.lower = requireNumber(rangeNode, "lower", path);
			range.upper = requireNumber(rangeNode, "upper", path);

			if (!std::isfinite(range.lower) ||
				!std::isfinite(range.upper) ||
				range.upper < range.lower) {
				throw configurationError(
					path,
					std::string("measurement range '") + key +
					"' must contain finite values satisfying lower <= upper.");
			}

			return range;
		}

		cv::Point2f requirePoint2f(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const cv::FileNode node = parent[key];

			if (node.empty() ||
				!node.isSeq() ||
				node.size() != 2) {
				throw configurationError(
					path,
					std::string("'") + key +
					"' must be a sequence containing two numbers.");
			}

			const cv::FileNode xNode = node[0];
			const cv::FileNode yNode = node[1];

			const bool xIsNumber = xNode.isInt() || xNode.isReal();
			const bool yIsNumber = yNode.isInt() || yNode.isReal();

			if (!xIsNumber || !yIsNumber) {
				throw configurationError(
					path,
					std::string("'") + key +
					"' must contain two numeric values.");
			}

			return {
				static_cast<float>(static_cast<double>(xNode)),
				static_cast<float>(static_cast<double>(yNode))
			};
		}

		std::vector<int> requireIntegerSequence(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const cv::FileNode node = parent[key];
			if (node.empty() || !node.isSeq()) {
				throw configurationError(
					path,
					std::string("required integer sequence '") + key +
					"' is missing.");
			}

			std::vector<int> values;
			values.reserve(node.size());
			for (const cv::FileNode& valueNode : node) {
				if (!valueNode.isInt()) {
					throw configurationError(
						path,
						std::string("sequence '") + key +
						"' must contain only integers.");
				}
				values.push_back(static_cast<int>(valueNode));
			}

			return values;
		}

		cv::Rect requireRect(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const cv::FileNode node = requireMapNode(parent, key, path);
			return {
				requireInteger(node, "x_px", path),
				requireInteger(node, "y_px", path),
				requireInteger(node, "width_px", path),
				requireInteger(node, "height_px", path)
			};
		}

		EdgePolarity parseEdgePolarity(
			const cv::FileNode& parent,
			const char* key,
			const std::filesystem::path& path)
		{
			const std::string value =
				requireString(parent, key, path);

			if (value == "dark_to_light") {
				return EdgePolarity::DarkToLight;
			}

			if (value == "light_to_dark") {
				return EdgePolarity::LightToDark;
			}

			if (value == "any") {
				return EdgePolarity::Any;
			}

			throw configurationError(
				path,
				std::string("unsupported edge polarity '") +
				value + "' for '" + key + "'.");
		}

		CalibrationBlobColor parseCalibrationBlobColor(
			const cv::FileNode& parent,
			const std::filesystem::path& path)
		{
			const std::string value =
				requireString(parent, "blob_color", path);

			if (value == "dark") {
				return CalibrationBlobColor::Dark;
			}
			if (value == "light") {
				return CalibrationBlobColor::Light;
			}

			throw configurationError(
				path,
				"blob_color must be either 'dark' or 'light'.");
		}

		CircleDetectionConfig loadCircleDetectionConfig(
			const cv::FileNode& detectionNode,
			const char* role,
			const std::filesystem::path& path)
		{
			const cv::FileNode roleNode =
				requireMapNode(detectionNode, role, path);

			const double minimumThreshold =
				requireNumber(roleNode, "minimum_threshold", path);
			const double maximumThreshold =
				requireNumber(roleNode, "maximum_threshold", path);
			const double thresholdStep =
				requireNumber(roleNode, "threshold_step", path);
			const int minimumRepeatability =
				requireInteger(roleNode, "minimum_repeatability", path);
			const double minimumDistancePx =
				requireNumber(roleNode, "minimum_distance_px", path);
			const double minimumAreaPx2 =
				requireNumber(roleNode, "minimum_area_px2", path);
			const double maximumAreaPx2 =
				requireNumber(roleNode, "maximum_area_px2", path);
			const double minimumCircularity =
				requireNumber(roleNode, "minimum_circularity", path);

			if (!std::isfinite(minimumThreshold) ||
				!std::isfinite(maximumThreshold) ||
				minimumThreshold < 0.0 ||
				maximumThreshold > 255.0 ||
				minimumThreshold >= maximumThreshold) {
				throw configurationError(
					path,
					std::string("detection thresholds for '") + role +
					"' must satisfy 0 <= minimum < maximum <= 255.");
			}

			if (!std::isfinite(thresholdStep) ||
				thresholdStep <= 0.0 ||
				thresholdStep > maximumThreshold - minimumThreshold) {
				throw configurationError(
					path,
					std::string("threshold_step for '") + role +
					"' must be finite, greater than zero, and no larger than "
					"the configured threshold range.");
			}

			if (minimumRepeatability < 1) {
				throw configurationError(
					path,
					std::string("minimum_repeatability for '") + role +
					"' must be at least one.");
			}

			const double thresholdLevelCount = std::ceil(
				(maximumThreshold - minimumThreshold) /
				thresholdStep);
			if (static_cast<double>(minimumRepeatability) >
				thresholdLevelCount) {
				throw configurationError(
					path,
					std::string("threshold range for '") + role +
					"' does not provide enough levels for minimum_repeatability.");
			}

			if (!std::isfinite(minimumDistancePx) ||
				minimumDistancePx <= 0.0 ||
				minimumDistancePx > std::numeric_limits<float>::max()) {
				throw configurationError(
					path,
					std::string("minimum_distance_px for '") + role +
					"' must be finite and greater than zero.");
			}

			if (!std::isfinite(minimumAreaPx2) ||
				!std::isfinite(maximumAreaPx2) ||
				minimumAreaPx2 <= 0.0 ||
				minimumAreaPx2 >= maximumAreaPx2 ||
				maximumAreaPx2 > std::numeric_limits<float>::max()) {
				throw configurationError(
					path,
					std::string("blob area for '") + role +
					"' must satisfy 0 < minimum_area_px2 < maximum_area_px2.");
			}

			if (!std::isfinite(minimumCircularity) ||
				minimumCircularity <= 0.0 ||
				minimumCircularity > 1.0) {
				throw configurationError(
					path,
					std::string("minimum_circularity for '") + role +
					"' must be in (0, 1].");
			}

			return CircleDetectionConfig{
				.blobColor = parseCalibrationBlobColor(roleNode, path),
				.minimumThreshold = static_cast<float>(minimumThreshold),
				.maximumThreshold = static_cast<float>(maximumThreshold),
				.thresholdStep = static_cast<float>(thresholdStep),
				.minimumRepeatability =
					static_cast<std::size_t>(minimumRepeatability),
				.minimumDistancePx = static_cast<float>(minimumDistancePx),
				.minimumAreaPx2 = static_cast<float>(minimumAreaPx2),
				.maximumAreaPx2 = static_cast<float>(maximumAreaPx2),
				.minimumCircularity = static_cast<float>(minimumCircularity)
			};
		}

	}	// namespace

	/* @brief: 主配置文件的加载 */
	VisionAlgorithmConfig loadVisionAlgorithmConfig(
		const std::filesystem::path& configPath)
	{
		const auto normalizedPath =
			normalizeExistingPath(configPath);

		cv::FileStorage storage = openFileStorage(
			normalizedPath,
			"VisionAlgorithm main configuration file");

		const cv::FileNode root = storage.root();

		const int timeoutMilliseconds = requireInteger(
			root,
			"timeout_ms",
			normalizedPath);

		if (timeoutMilliseconds <= 0) {
			throw configurationError(
				normalizedPath,
				"timeout_ms must be greater than zero.");
		}

		const cv::FileNode enabledAlgorithms =
			requireMapNode(
				root,
				"enabled_algorithms",
				normalizedPath);

		const bool edgeEnabled = requireEnableFlag(
			enabledAlgorithms,
			"edge",
			normalizedPath);

		const bool markPointEnabled = requireEnableFlag(
			enabledAlgorithms,
			"mark",
			normalizedPath);

		if (!edgeEnabled && !markPointEnabled) {
			throw configurationError(
				normalizedPath,
				"at least one algorithm must be enabled.");
		}

		VisionAlgorithmConfig config;
		config.timeout =
			std::chrono::milliseconds(timeoutMilliseconds);

		const cv::FileNode calibrationNode = requireMapNode(
			root,
			"calibration",
			normalizedPath);

		config.calibration = CalibrationResourceConfig{
			.parameterPath = resolveReferencedPath(
				normalizedPath,
				requireString(calibrationNode, "parameters", normalizedPath)),
			.centerPath = resolveReferencedPath(
				normalizedPath,
				requireString(calibrationNode, "center", normalizedPath)),
			.topLeftPath = resolveReferencedPath(
				normalizedPath,
				requireString(calibrationNode, "top_left", normalizedPath)),
			.topRightPath = resolveReferencedPath(
				normalizedPath,
				requireString(calibrationNode, "top_right", normalizedPath)),
			.bottomRightPath = resolveReferencedPath(
				normalizedPath,
				requireString(calibrationNode, "bottom_right", normalizedPath)),
			.bottomLeftPath = resolveReferencedPath(
				normalizedPath,
				requireString(calibrationNode, "bottom_left", normalizedPath))
		};

		if (edgeEnabled) {
			const cv::FileNode edgeNode = requireMapNode(
				root,
				"edge",
				normalizedPath);

			config.edge.emplace(EdgeResourceConfig{
				.parameterPath = resolveReferencedPath(
					normalizedPath,
					requireString(
						edgeNode,
						"parameters",
						normalizedPath))
				});
		}

		if (markPointEnabled) {
			const cv::FileNode markPointNode = requireMapNode(
				root,
				"mark",
				normalizedPath);

			config.mark.emplace(
				MarkResourceConfig{
					.parameterPath =
						resolveReferencedPath(
							normalizedPath,
							requireString(
								markPointNode,
								"parameters",
								normalizedPath))
				});
		}

		return config;
	}


	CalibrationAlgorithmConfig loadCalibrationAlgorithmConfig(
		const std::filesystem::path& parameterPath)
	{
		const auto normalizedPath =
			normalizeExistingPath(parameterPath);

		cv::FileStorage storage = openFileStorage(
			normalizedPath,
			"CalibrationAlgorithm parameter file");

		const cv::FileNode root = storage.root();
		const int gridSidePoints = requireInteger(
			root, "grid_side_points", normalizedPath);
		if (gridSidePoints < 3 || gridSidePoints > 15) {
			throw configurationError(normalizedPath,
				"grid_side_points must be an integer between 3 and 15.");
		}
		const int expectedPointCount = gridSidePoints * gridSidePoints;
		const cv::FileNode detectionNode = requireMapNode(
			root,
			"detection",
			normalizedPath);
		const cv::FileNode solverNode = requireMapNode(
			root,
			"solver",
			normalizedPath);
		const cv::FileNode qualityNode = requireMapNode(
			root,
			"quality",
			normalizedPath);

		const double ransacThresholdMm = requireNumber(
			solverNode,
			"ransac_threshold_mm",
			normalizedPath);
		const int minimumUsedPoints = requireInteger(
			qualityNode,
			"minimum_used_points",
			normalizedPath);
		const double maximumRmsErrorMm = requireNumber(
			qualityNode,
			"maximum_rms_error_mm",
			normalizedPath);
		const double maximumResidualMm = requireNumber(
			qualityNode,
			"maximum_residual_mm",
			normalizedPath);

		if (!std::isfinite(ransacThresholdMm) ||
			ransacThresholdMm <= 0.0) {
			throw configurationError(
				normalizedPath,
				"ransac_threshold_mm must be finite and greater than zero.");
		}
		if (minimumUsedPoints != expectedPointCount - 1 &&
			minimumUsedPoints != expectedPointCount) {
			throw configurationError(
				normalizedPath,
				"minimum_used_points must be either " +
				std::to_string(expectedPointCount - 1) + " or " +
				std::to_string(expectedPointCount) + ".");
		}
		if (!std::isfinite(maximumRmsErrorMm) ||
			maximumRmsErrorMm <= 0.0) {
			throw configurationError(
				normalizedPath,
				"maximum_rms_error_mm must be finite and greater than zero.");
		}
		if (!std::isfinite(maximumResidualMm) ||
			maximumResidualMm <= 0.0 ||
			maximumResidualMm < maximumRmsErrorMm) {
			throw configurationError(
				normalizedPath,
				"maximum_residual_mm must be finite, greater than zero, "
				"and not less than maximum_rms_error_mm.");
		}

		return CalibrationAlgorithmConfig{
			.gridSidePoints = gridSidePoints,
			.center = loadCircleDetectionConfig(
				detectionNode, "center", normalizedPath),
			.topLeft = loadCircleDetectionConfig(
				detectionNode, "top_left", normalizedPath),
			.topRight = loadCircleDetectionConfig(
				detectionNode, "top_right", normalizedPath),
			.bottomRight = loadCircleDetectionConfig(
				detectionNode, "bottom_right", normalizedPath),
			.bottomLeft = loadCircleDetectionConfig(
				detectionNode, "bottom_left", normalizedPath),
			.ransacThresholdMm = ransacThresholdMm,
			.minimumUsedPoints = static_cast<std::size_t>(minimumUsedPoints),
			.maximumRmsErrorMm = maximumRmsErrorMm,
			.maximumResidualMm = maximumResidualMm
		};
	}


	// ----------------------------------------------------------
	// load edge config
	// ----------------------------------------------------------

	// ROI配置读取函数
	CaliperRoiConfig loadCaliperRoiConfig(
		const cv::FileNode& parent,
		const char* key,
		const std::filesystem::path& path)
	{
		const cv::FileNode node =
			requireMapNode(parent, key, path);

		CaliperRoiConfig config;

		config.centerPx =
			requirePoint2f(node, "center_px", path);

		config.scanAngleDeg =
			requireNumber(node, "scan_angle_deg", path);

		config.searchLengthPx =
			requireNumber(node, "search_length_px", path);

		config.spanLengthPx =
			requireNumber(node, "span_length_px", path);

		config.sampleCount =
			requireInteger(node, "sample_count", path);

		config.polarity =
			parseEdgePolarity(node, "polarity", path);

		if (config.centerPx.x < 0.0F ||
			config.centerPx.y < 0.0F) {
			throw configurationError(
				path,
				std::string("ROI '") + key +
				"' center coordinates must not be negative.");
		}

		if (config.searchLengthPx <= 0.0) {
			throw configurationError(
				path,
				std::string("ROI '") + key +
				"' search_length_px must be greater than zero.");
		}

		if (config.spanLengthPx <= 0.0) {
			throw configurationError(
				path,
				std::string("ROI '") + key +
				"' span_length_px must be greater than zero.");
		}

		if (config.sampleCount < 2) {
			throw configurationError(
				path,
				std::string("ROI '") + key +
				"' sample_count must be at least 2.");
		}

		return config;
	}
	
	// 同一条边的配置读取
	RoiPairConfig loadRoiPairConfig(
		const cv::FileNode& edgesNode,
		const char* sideKey,
		const std::filesystem::path& path)
	{
		const cv::FileNode sideNode =
			requireMapNode(edgesNode, sideKey, path);

		return RoiPairConfig{
			.first = loadCaliperRoiConfig(
				sideNode,
				"first",
				path),

			.second = loadCaliperRoiConfig(
				sideNode,
				"second",
				path)
		};
	}
	

	// Edge算法配置文件的读取
	EdgeAlgorithmConfig loadEdgeAlgorithmConfig(
		const std::filesystem::path& configPath)
	{
		const auto normalizedPath =
			normalizeExistingPath(configPath);

		cv::FileStorage storage = openFileStorage(
			normalizedPath,
			"edge algorithm parameter file");

		const cv::FileNode root = storage.root();

		const cv::FileNode edgesNode =
			requireMapNode(
				root,
				"edges",
				normalizedPath);

		const cv::FileNode caliperNode =
			requireMapNode(
				root,
				"caliper",
				normalizedPath);

		const cv::FileNode qualityNode =
			requireMapNode(
				root,
				"quality",
				normalizedPath);

		const cv::FileNode edgeModeNode =
			requireMapNode(
				qualityNode,
				"edge_mode",
				normalizedPath);

		const cv::FileNode offsetResultNode =
			requireMapNode(
				qualityNode,
				"offset_result",
				normalizedPath);

		EdgeAlgorithmConfig config;

		config.top =
			loadRoiPairConfig(
				edgesNode,
				"top",
				normalizedPath);

		config.right =
			loadRoiPairConfig(
				edgesNode,
				"right",
				normalizedPath);

		config.bottom =
			loadRoiPairConfig(
				edgesNode,
				"bottom",
				normalizedPath);

		config.left =
			loadRoiPairConfig(
				edgesNode,
				"left",
				normalizedPath);

		config.caliper.smoothingKernelSize =
			requireInteger(
				caliperNode,
				"smoothing_kernel_size",
				normalizedPath);

		config.caliper.minimumGradient =
			requireNumber(
				caliperNode,
				"minimum_gradient",
				normalizedPath);

		config.caliper.enableSubpixel =
			requireEnableFlag(
				caliperNode,
				"enable_subpixel",
				normalizedPath);

		config.caliper.minimumPointsPerRoi =
			requireInteger(
				caliperNode,
				"minimum_points_per_roi",
				normalizedPath);

		config.quality.topLength =
			loadMeasurementRange(
				edgeModeNode,
				"top_length",
				normalizedPath);

		config.quality.rightLength =
			loadMeasurementRange(
				edgeModeNode,
				"right_length",
				normalizedPath);

		config.quality.bottomLength =
			loadMeasurementRange(
				edgeModeNode,
				"bottom_length",
				normalizedPath);

		config.quality.leftLength =
			loadMeasurementRange(
				edgeModeNode,
				"left_length",
				normalizedPath);

		config.quality.diagonalTopLeftToBottomRight =
			loadMeasurementRange(
				edgeModeNode,
				"diagonal_top_left_to_bottom_right",
				normalizedPath);

		config.quality.diagonalTopRightToBottomLeft =
			loadMeasurementRange(
				edgeModeNode,
				"diagonal_top_right_to_bottom_left",
				normalizedPath);

		config.quality.absoluteAngleDeg =
			loadMeasurementRange(
				offsetResultNode,
				"absolute_angle_deg",
				normalizedPath);

		config.quality.maximumCenterOffset =
			requireNumber(
				offsetResultNode,
				"maximum_center_offset",
				normalizedPath);

		if (!std::isfinite(config.quality.maximumCenterOffset) ||
			config.quality.maximumCenterOffset < 0.0) {
			throw configurationError(
				normalizedPath,
				"maximum_center_offset must be a finite non-negative number.");
		}

		if (config.caliper.smoothingKernelSize <= 0 ||
			config.caliper.smoothingKernelSize % 2 == 0) {
			throw configurationError(
				normalizedPath,
				"smoothing_kernel_size must be a positive odd number.");
		}

		if (config.caliper.minimumGradient <= 0.0) {
			throw configurationError(
				normalizedPath,
				"minimum_gradient must be greater than zero.");
		}

		if (config.caliper.minimumPointsPerRoi < 2) {
			throw configurationError(
				normalizedPath,
				"minimum_points_per_roi must be at least 2.");
		}

		const auto validateMinimumPoints =
			[&](const RoiPairConfig& pair,
				std::string_view side)
			{
				const int minimum =
					config.caliper.minimumPointsPerRoi;

				if (minimum > pair.first.sampleCount ||
					minimum > pair.second.sampleCount) {
					throw configurationError(
						normalizedPath,
						std::string("minimum_points_per_roi exceeds "
							"sample_count of the ") +
						std::string(side) + " ROI pair.");
				}
			};

		validateMinimumPoints(config.top, "top");
		validateMinimumPoints(config.right, "right");
		validateMinimumPoints(config.bottom, "bottom");
		validateMinimumPoints(config.left, "left");

		return config;
	}
	
	// -----------------------------------------------------------
	// load mark config
	// -----------------------------------------------------------
	MarkAlgorithmConfig loadMarkAlgorithmConfig(
		const std::filesystem::path& configPath)
	{
		const auto normalizedPath =
			normalizeExistingPath(configPath);

		cv::FileStorage storage = openFileStorage(
			normalizedPath,
			"Mark-point algorithm parameter file");

		const cv::FileNode root = storage.root();
		const cv::FileNode inputImageNode =
			requireMapNode(root, "input_image", normalizedPath);
		const cv::FileNode cameraRoisNode =
			requireMapNode(root, "camera_rois", normalizedPath);
		const cv::FileNode shapeMatcherNode =
			requireMapNode(root, "shape_matcher", normalizedPath);
		const cv::FileNode qualityNode =
			requireMapNode(root, "quality", normalizedPath);
		const cv::FileNode markGeometryNode =
			requireMapNode(qualityNode, "mark_geometry", normalizedPath);
		const cv::FileNode poseResultNode =
			requireMapNode(qualityNode, "pose_result", normalizedPath);

		MarkAlgorithmConfig config;
		config.inputImageSizePx = {
			requireInteger(inputImageNode, "width_px", normalizedPath),
			requireInteger(inputImageNode, "height_px", normalizedPath)
		};

		config.topLeftCameraRoiPx =
			requireRect(cameraRoisNode, "top_left", normalizedPath);
		config.topRightCameraRoiPx =
			requireRect(cameraRoisNode, "top_right", normalizedPath);
		config.bottomRightCameraRoiPx =
			requireRect(cameraRoisNode, "bottom_right", normalizedPath);
		config.bottomLeftCameraRoiPx =
			requireRect(cameraRoisNode, "bottom_left", normalizedPath);

		ShapeMatcherConfig& shapeMatcher = config.shapeMatcher;
		shapeMatcher.classId =
			requireString(shapeMatcherNode, "class_id", normalizedPath);
		shapeMatcher.templateFilePattern = resolveReferencedPath(
			normalizedPath,
			requireString(
				shapeMatcherNode,
				"template_file_pattern",
				normalizedPath));
		shapeMatcher.templateInfoFile = resolveReferencedPath(
			normalizedPath,
			requireString(
				shapeMatcherNode,
				"template_info_file",
				normalizedPath));
		shapeMatcher.templateImageSizePx = {
			requireInteger(
				shapeMatcherNode,
				"template_image_width_px",
				normalizedPath),
			requireInteger(
				shapeMatcherNode,
				"template_image_height_px",
				normalizedPath)
		};

		const cv::FileNode detectorNode =
			requireMapNode(shapeMatcherNode, "detector", normalizedPath);
		shapeMatcher.detector.numFeatures =
			requireInteger(detectorNode, "num_features", normalizedPath);
		shapeMatcher.detector.pyramidT =
			requireIntegerSequence(detectorNode, "pyramid_t", normalizedPath);
		shapeMatcher.detector.weakThreshold = static_cast<float>(
			requireNumber(detectorNode, "weak_threshold", normalizedPath));
		shapeMatcher.detector.strongThreshold = static_cast<float>(
			requireNumber(detectorNode, "strong_threshold", normalizedPath));

		config.quality.topLength =
			loadMeasurementRange(
				markGeometryNode,
				"top_length",
				normalizedPath);
		config.quality.rightLength =
			loadMeasurementRange(
				markGeometryNode,
				"right_length",
				normalizedPath);
		config.quality.bottomLength =
			loadMeasurementRange(
				markGeometryNode,
				"bottom_length",
				normalizedPath);
		config.quality.leftLength =
			loadMeasurementRange(
				markGeometryNode,
				"left_length",
				normalizedPath);
		config.quality.diagonalTopLeftToBottomRight =
			loadMeasurementRange(
				markGeometryNode,
				"diagonal_top_left_to_bottom_right",
				normalizedPath);
		config.quality.diagonalTopRightToBottomLeft =
			loadMeasurementRange(
				markGeometryNode,
				"diagonal_top_right_to_bottom_left",
				normalizedPath);

		config.quality.centerX =
			loadSignedMeasurementRange(
				poseResultNode,
				"center_x",
				normalizedPath);
		config.quality.centerY =
			loadSignedMeasurementRange(
				poseResultNode,
				"center_y",
				normalizedPath);
		config.quality.absoluteAngleDeg =
			loadMeasurementRange(
				poseResultNode,
				"absolute_angle_deg",
				normalizedPath);

		if (config.quality.absoluteAngleDeg.upper > 90.0) {
			throw configurationError(
				normalizedPath,
				"absolute_angle_deg upper must not exceed 90 degrees.");
		}

		return config;
	}

}	// namespace vision::algorithm::detail
