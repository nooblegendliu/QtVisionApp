#pragma once

#include <VisionAlgorithm/VisionAlgorithm.h>
#include <VisionCore/LocateTypes.h>

#include "AlgorithmInternal.h"
#include "CameraGeometry.h"

#include "EdgeGeometry.h"
#include "EdgeLineDetector.h"
#include "EdgeTypes.h"

namespace vision::algorithm::detail {

	class EdgePipeline final {
	public:
		EdgePipeline(
			EdgeAlgorithmConfig config,
			CameraGeometry centerGeometry,
			std::shared_ptr<DemoEdgeVisualizationStore> demoVisualization);

		[[nodiscard]] vision::core::AlgorithmOutcome locate(
			const vision::core::EdgeAlgorithmInput& input,
			const ExecutionContext& context) const;

	private:
		EdgeAlgorithmConfig config_;
		CameraGeometry centerGeometry_;
		std::shared_ptr<DemoEdgeVisualizationStore> demoVisualization_;

		// add
		EdgeLineDetector lineDetector_;
		EdgeGeometry geometry_;
	};

}	// namespace vision::algorithm::detail
