#pragma once

#include <VisionCore/Error.h>
#include <VisionCore/ImageFrame.h>

#include <opencv2/core.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace vision::algorithm {

	/*
	* 理论实际坐标点集
	* 
	* 点的容器顺序表示理论点编号
	*	points[0] -> 1号点
	*	points[1] -> 2号点
	*	...
	*	完整N=n*n点从物理左下角P1开始，每行沿+X、各行沿+Y编号；
	*	不依赖中心圆，奇数和偶数边长使用相同规则
	*	坐标统一使用设备加工平面XY坐标，单位固定为毫米
	*/
	using PhysicalPointSet = std::vector<cv::Point2d>;

	/*
	* n*n对称圆阵列的人工方向定义
	*
	* 方向相对于去畸变后的完整图像：P1表示理论1号点所在角，
	* X表示与P1相邻的物理+X方向。八种取值覆盖阵列的旋转和镜像关系。
	*/
	enum class CalibrationGridOrientation {
		P1TopLeftXRight,
		P1TopLeftXDown,
		P1TopRightXLeft,
		P1TopRightXDown,
		P1BottomRightXLeft,
		P1BottomRightXUp,
		P1BottomLeftXRight,
		P1BottomLeftXUp
	};


	/*
	* 单张图像的圆心检测结果
	* 
	* rectifiedImage:
	*	根据对应相机内参完成畸变校正的完整CV_8UC1连续灰度图像；
	*	像素独立于输入及其他检测调用，输入释放后仍有效
	* imagePoints:
	*	rectifiedImage坐标系中的圆心坐标，保持OpenCV原生网格顺序；
	*	此时尚未根据人工方向重排为物理P1～PN，N由配置的n*n确定
	* usableForSolving:
	*	true表示完整N点，坐标有限、位于图像内且互不重复；
	*	未找到完整网格时为false，保留rectifiedImage且imagePoints为空
	*/
	struct CalibrationDetection {
		using Role = vision::core::CameraRole;

		Role cameraRole{ Role::Unknown };
		cv::Mat rectifiedImage;
		std::vector<cv::Point2d> imagePoints;
		bool usableForSolving{ false };
	};

	/*
	* 一组已经建立对应关系的标定点
	* 
	* pointIndex对应理论点集中的下标，即0表示物理P1
	* residual单位固定为毫米
	*/
	struct CalibrationPoint {
		std::size_t pointIndex{ 0 };

		cv::Point2d imagePoint;
		cv::Point2d physicalPoint;

		double residual{ 0.0 };
		bool used{ false };
	};

	/*
	* 候选矩阵的质量评估结果
	* 
	* rmsError和maximumError的单位固定为毫米
	* 质量统计直接使用solve保存的residual，覆盖全部N点，包括未参与
	* RANSAC拟合的点；不重新投影，也不验证残差与矩阵的一致性
	* passed = false表示评估正常完成，但质量未通过
	*/
	struct CalibrationEvaluation {
		std::size_t detectedPointCount{ 0 };
		std::size_t usedPointCount{ 0 };

		double rmsError{ 0.0 };
		double maximumError{ 0.0 };

		bool passed{ false };
	};


	/*
	* 单台相机的候选校正结果
	* 对应全部N组标定点；变换矩阵始终为3*3，与阵列规格无关
	*/
	struct CalibrationSolution {
		using Role = vision::core::CameraRole;
		Role cameraRole = Role::Unknown;

		cv::Matx33d imageToPhysical{
			cv::Matx33d::zeros()
		};

		std::vector<CalibrationPoint> points;
	};

	using CalibrationDetectionOutcome =
		vision::core::Outcome<CalibrationDetection>;

	using CalibrationSolutionOutcome =
		vision::core::Outcome<CalibrationSolution>;

	using CalibrationEvaluationOutcome =
		vision::core::Outcome<CalibrationEvaluation>;

	/*
	* 相机平面校正算法
	* 
	* 对象只负责去畸变、圆心检测、矩阵求解和质量评估。
	* 采图、理论点文件解析、标定文件写入及应用事务不属于本类。
	* 方法在现有视觉线程中同步调用，类内不保存UI工作流状态。
	*/
	class CalibrationAlgorithm final {
	public:
		using CreateOutcome =
			vision::core::Outcome<std::unique_ptr<CalibrationAlgorithm>>;

		/*
		* 使用VisionAlgorithm主配置创建校正算法。
		*
		* 加载校正算法参数，并从五份角色标定资源中准备图像尺寸、
		* 内参、畸变参数和新内参。任一资源无效时不返回部分对象。
		*/
		[[nodiscard]] static CreateOutcome create(
			const std::filesystem::path& mainConfigPath);

		/*
		* 同步检测配置的n*n对称圆阵列，不修改或保存输入像素。
		* 五角色共用n，支持整数3～15；示例默认5，配置字段不可缺失。
		* 输入必须是非空、二维、连续CV_8UC1，尺寸匹配该角色标定。
		* 未找到完整网格是正常检测结果，不是Error；不返回部分点。
		* 非法角色返回InvalidConfiguration，非法图像返回InvalidImage；
		* OpenCV异常或非法成功点集返回AlgorithmFailure，其他未预期异常
		* 返回InternalFailure。无超时、物理点重排、文件写入或结果缓存。
		*/
		[[nodiscard]] CalibrationDetectionOutcome detect(
			vision::core::CameraRole cameraRole,
			const cv::Mat& rawImage) const;

		/*
		* 将完整N点检测结果按人工方向映射为物理P1～PN，使用毫米
		* RANSAC阈值求取校正图像像素到加工平面毫米的3*3单应矩阵。
		* 输出点按物理编号排序，used来自RANSAC，residual统计全部N点。
		* 不按坐标重排或推断方向，不使用旧image_to_physical，也不执行
		* minimum_used_points、RMS或最大残差质量判定；后者属于evaluate。
		* 无效检测结构返回InvalidResult，非法校正图像返回InvalidImage，
		* 非法理论点或方向返回InvalidConfiguration，求解失败返回
		* AlgorithmFailure。函数不修改或保存输入，也不缓存候选结果。
		*/
		[[nodiscard]] CalibrationSolutionOutcome solve(
			const CalibrationDetection& detection,
			const PhysicalPointSet& physicalPoints,
			CalibrationGridOrientation orientation) const;

		/*
		* 校验候选解并统计全部N点的毫米RMS和最大残差，采用点数量
		* 只来自used标记。minimum_used_points、RMS及最大残差三项门槛
		* 同时满足时passed=true；质量不达标正常返回passed=false。
		* residual由solve生成并作为权威统计值，不重新投影或比对矩阵。
		* 非法候选结构返回InvalidResult，质量计算失败返回AlgorithmFailure，
		* 其他未预期异常返回InternalFailure；函数不修改或保存输入。
		*/
		[[nodiscard]] CalibrationEvaluationOutcome evaluate(
			const CalibrationSolution& solution) const;

		~CalibrationAlgorithm();

		CalibrationAlgorithm(const CalibrationAlgorithm&) = delete;
		CalibrationAlgorithm& operator=(const CalibrationAlgorithm&) = delete;
		CalibrationAlgorithm(CalibrationAlgorithm&&) = delete;
		CalibrationAlgorithm& operator=(CalibrationAlgorithm&&) = delete;

	private:
		struct Resources;
		std::unique_ptr<Resources> resources_;

		explicit CalibrationAlgorithm(
			const std::filesystem::path& mainConfigPath);
	};

}	// namespace vision::algorithm
