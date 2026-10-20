#include <VisionAlgorithm/VisionAlgorithm.h>

#include <opencv2/core.hpp>

#include <exception>
#include <chrono>
#include <string_view>
#include <memory>
#include <string>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include "AlgorithmConfig.h"
#include "AlgorithmInternal.h"
#include "CameraGeometry.h"
#include "edge/EdgePipeline.h"
#include "mark/MarkPipeline.h"	// TODO fix

namespace vision::algorithm {

	namespace {

		using vision::core::AlgorithmOutcome;
		using vision::core::Error;
		using vision::core::ErrorCode;


		template <typename>
		inline constexpr bool kUnhandledInputType = false;


		Error makeMovedFromError()
		{
			return Error{
				ErrorCode::InternalFailure,
				"VisionAlgorithm is not initialized.",
				"Runtime resources are null; the object may have been moved from."
			};
		}


		Error makeInvalidVariantError()
		{
			return Error{
				ErrorCode::InternalFailure,
				"VisionAlgorithm received an invalid input variant.",
				"AlgorithmInput is valueless_by_exception."
			};
		}


		Error makeOpenCvError(const cv::Exception& exception)
		{
			return Error{
				ErrorCode::AlgorithmFailure,
				"VisionAlgorithm failed inside OpenCV.",
				exception.what()
			};
		}


		Error makeUnexpectedExceptionError(
			const std::exception& exception)
		{
			return Error{
				ErrorCode::InternalFailure,
				"VisionAlgorithm encountered an unexpected internal failure.",
				exception.what()
			};
		}


		Error makeUnknownExceptionError()
		{
			return Error{
				ErrorCode::InternalFailure,
				"VisionAlgorithm encountered an unknown internal failure.",
				"A non-standard exception escaped from an algorithm pipeline."
			};
		}


		Error makeInvalidConfigurationError(
			std::string_view diagnostic)
		{
			return Error{
				ErrorCode::InvalidConfiguration,
				"VisionAlgorithm configuration or resources are invalid.",
				std::string(diagnostic)
			};
		}


		Error makeInitializationFailureError(
			std::string_view diagnostic)
		{
			return Error{
				ErrorCode::InternalFailure,
				"VisionAlgorithm initialization failed unexpectedly.",
				std::string(diagnostic)
			};
		}


		Error makeUnknownInitializationFailureError()
		{
			return Error{
				ErrorCode::InternalFailure,
				"VisionAlgorithm initialization failed unexpectedly.",
				"A non-standard exception escaped from an initialization component."
			};
		}
	}	// namespace


	void DemoEdgeVisualizationStore::clear() noexcept
	{
		current_ = DemoEdgeVisualization{};
	}


	void DemoEdgeVisualizationStore::publish(
		const DemoEdgeVisualization& visualization) noexcept
	{
		try {
			DemoEdgeVisualization copy = visualization;
			copy.rectifiedImage = visualization.rectifiedImage.clone();
			current_ = std::move(copy);
		}
		catch (...) {
			// Temporary visualization must never alter the algorithm outcome.
		}
	}


	DemoEdgeVisualization DemoEdgeVisualizationStore::snapshot() const
	{
		DemoEdgeVisualization copy = current_;
		copy.rectifiedImage = current_.rectifiedImage.clone();
		return copy;
	}


	struct VisionAlgorithm::RuntimeResources final {
	public:
		std::chrono::milliseconds timeout{ 500 };

		std::optional<detail::EdgePipeline> edgePipeline;
		std::optional<detail::MarkPipeline> markPipeline;
		std::shared_ptr<DemoEdgeVisualizationStore> demoVisualization;


		explicit RuntimeResources(
			const std::filesystem::path& configPath,
			std::shared_ptr<DemoEdgeVisualizationStore> visualization)
			: demoVisualization(std::move(visualization))
		{
			const detail::VisionAlgorithmConfig config =
				detail::loadVisionAlgorithmConfig(configPath);

			timeout = config.timeout;

			initializeEdgePipeline(config);
			initializeMarkPipeline(config);
		}

	private:
		void initializeEdgePipeline(
			const detail::VisionAlgorithmConfig& config)
		{
			if (!config.edge.has_value()) {
				return;
			}

			const detail::EdgeResourceConfig& resources =
				*config.edge;

			detail::EdgeAlgorithmConfig algorithmConfig =
				detail::loadEdgeAlgorithmConfig(
					resources.parameterPath);

			detail::CameraGeometry centerGeometry =
				detail::CameraGeometry::loadFromFile(
					config.calibration.centerPath);

			edgePipeline.emplace(
				std::move(algorithmConfig),
				std::move(centerGeometry),
				demoVisualization);
		}


		void initializeMarkPipeline(
			const detail::VisionAlgorithmConfig& config)
		{
			if (!config.mark.has_value()) {
				return;
			}

			const detail::MarkResourceConfig& resources =
				*config.mark;

			detail::MarkAlgorithmConfig algorithmConfig =
				detail::loadMarkAlgorithmConfig(
					resources.parameterPath);

			detail::CameraGeometry topLeftGeometry =
				detail::CameraGeometry::loadFromFile(
					config.calibration.topLeftPath);

			detail::CameraGeometry topRightGeometry =
				detail::CameraGeometry::loadFromFile(
					config.calibration.topRightPath);

			detail::CameraGeometry bottomRightGeometry =
				detail::CameraGeometry::loadFromFile(
					config.calibration.bottomRightPath);

			detail::CameraGeometry bottomLeftGeometry =
				detail::CameraGeometry::loadFromFile(
					config.calibration.bottomLeftPath);

			markPipeline.emplace(
				std::move(algorithmConfig),
				std::move(topLeftGeometry),
				std::move(topRightGeometry),
				std::move(bottomRightGeometry),
				std::move(bottomLeftGeometry));
		}
	};


	VisionAlgorithm::CreateOutcome VisionAlgorithm::create(
		const std::filesystem::path& configPath,
		std::shared_ptr<DemoEdgeVisualizationStore> demoVisualization)
	{
		try {
			return std::unique_ptr<vision::core::ILocateAlgorithm>(
				new VisionAlgorithm(
					configPath,
					std::move(demoVisualization)));
		}
		catch (const std::invalid_argument& exception) {
			return makeInvalidConfigurationError(exception.what());
		}
		catch (const std::exception& exception) {
			return makeInitializationFailureError(exception.what());
		}
		catch (...) {
			return makeUnknownInitializationFailureError();
		}
	}

	vision::core::Status VisionAlgorithm::validateConfiguration(
		const std::filesystem::path& configPath)
	{
		auto outcome = create(configPath);
		if (const auto* error = std::get_if<vision::core::Error>(&outcome)) {
			return *error;
		}
		return std::monostate{};
	}


	VisionAlgorithm::VisionAlgorithm(
		const std::filesystem::path& configPath,
		std::shared_ptr<DemoEdgeVisualizationStore> demoVisualization)
		: resources_(
			std::make_unique<RuntimeResources>(
				configPath,
				std::move(demoVisualization)))
	{
	}


	VisionAlgorithm::~VisionAlgorithm() = default;


	AlgorithmOutcome VisionAlgorithm::locate(
		const vision::core::AlgorithmInput& input)
	{
		if (!resources_) {
			return makeMovedFromError();
		}

		if (input.valueless_by_exception()) {
			return makeInvalidVariantError();
		}

		if (resources_->demoVisualization) {
			resources_->demoVisualization->clear();
		}

		const auto startedAt =
			std::chrono::steady_clock::now();

		const detail::ExecutionContext context{
			.startedAt = startedAt,
			.deadline = startedAt + resources_->timeout
		};

		try {
			return std::visit(
				[this, &context](
					const auto& typedInput) -> AlgorithmOutcome {
						using Input =
							std::decay_t<decltype(typedInput)>;

						if constexpr (std::is_same_v<
							Input,
							vision::core::EdgeAlgorithmInput>) {

							if (!resources_->edgePipeline.has_value()) {
								return detail::makeAlgorithmDisabledError(
									"edge");
							}

							return resources_->edgePipeline->locate(
								typedInput,
								context);
						}
						else if constexpr (std::is_same_v<
							Input,
							vision::core::MarkPointAlgorithmInput>) {

							if (!resources_->markPipeline.has_value()) {
								return detail::makeAlgorithmDisabledError(
									"mark_point");
							}

							return resources_->markPipeline->locate(
								typedInput,
								context);
						}
						else {
							static_assert(
								kUnhandledInputType<Input>,
								"Unhandled vision::core::AlgorithmInput alternative.");
						}
				},
				input);
		}
		catch (const cv::Exception& exception) {
			return makeOpenCvError(exception);
		}
		catch (const std::runtime_error& exception) {
			return detail::makeAlgorithmError(
				"VisionAlgorithm failed during algorithm execution.",
				exception.what());
		}
		catch (const std::exception& exception) {
			return makeUnexpectedExceptionError(exception);
		}
		catch (...) {
			return makeUnknownExceptionError();
		}
	}



}	// namespace vision::algorithm
