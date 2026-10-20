#pragma once

#include <VisionCore/Ports.h>

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace vision::algorithm {

	struct DemoOverlayPoint {
		double x{ 0.0 };
		double y{ 0.0 };
	};

	struct DemoOverlayLine {
		DemoOverlayPoint start;
		DemoOverlayPoint end;
	};

	struct DemoOverlayRoi {
		std::array<DemoOverlayPoint, 4> corners;
	};

	struct DemoEdgeVisualization {
		cv::Mat rectifiedImage;
		std::vector<DemoOverlayRoi> rois;
		std::vector<DemoOverlayLine> fittedLines;
		std::vector<DemoOverlayPoint> corners;
	};

	class DemoEdgeVisualizationStore final {
	public:
		void clear() noexcept;
		void publish(const DemoEdgeVisualization& visualization) noexcept;

		[[nodiscard]] DemoEdgeVisualization snapshot() const;

	private:
		DemoEdgeVisualization current_;
	};

	class VisionAlgorithm final : public vision::core::ILocateAlgorithm {
	public:
		using CreateOutcome = vision::core::Outcome<
			std::unique_ptr<vision::core::ILocateAlgorithm>>;

		[[nodiscard]] static CreateOutcome create(
			const std::filesystem::path& configPath,
			std::shared_ptr<DemoEdgeVisualizationStore> demoVisualization = nullptr);
		[[nodiscard]] static vision::core::Status validateConfiguration(
			const std::filesystem::path& configPath);

		~VisionAlgorithm() override;

		VisionAlgorithm(const VisionAlgorithm&) = delete;
		VisionAlgorithm& operator=(const VisionAlgorithm&) = delete;
		VisionAlgorithm(VisionAlgorithm&&) = delete;
		VisionAlgorithm& operator=(VisionAlgorithm&&) = delete;

		/*
			@brief: 主定位函数
		*/
		vision::core::AlgorithmOutcome locate(
			const vision::core::AlgorithmInput& input) override;

	private:
		explicit VisionAlgorithm(
			const std::filesystem::path& configPath,
			std::shared_ptr<DemoEdgeVisualizationStore> demoVisualization);

		struct RuntimeResources;
		std::unique_ptr<RuntimeResources> resources_;
	};
}	// namespace vision::algorithm
