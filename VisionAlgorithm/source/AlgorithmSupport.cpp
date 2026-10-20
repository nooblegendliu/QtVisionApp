#include "AlgorithmInternal.h"

#include <cmath>
#include <string>

namespace vision::algorithm::detail {

    vision::core::Error makeAlgorithmError(
        std::string_view message,
        std::string_view diagnostic)
    {
        return vision::core::Error{
            vision::core::ErrorCode::AlgorithmFailure,
            std::string(message),
            std::string(diagnostic)
        };
    }


    std::optional<vision::core::Error> checkDeadline(
        const ExecutionContext& context,
        std::string_view stage)
    {
        if (!context.expired()) {
            return std::nullopt;
        }

        return makeTimeoutError(
            stage,
            context.elapsed(),
            context.budget());
    }


    vision::core::Error makeAlgorithmDisabledError(
        std::string_view algorithmName)
    {
        return vision::core::Error{
            vision::core::ErrorCode::AlgorithmFailure,
            "The requested VisionAlgorithm mode is disabled.",
            "Algorithm mode '" + std::string(algorithmName) +
                "' is disabled by the main configuration."
        };
    }


    vision::core::Error makeTimeoutError(
        std::string_view stage,
        std::chrono::milliseconds elapsed,
        std::chrono::milliseconds budget)
    {
        return vision::core::Error{
            vision::core::ErrorCode::AlgorithmTimeout,
            "VisionAlgorithm exceeded its processing deadline.",
            "Timeout observed at stage: " + std::string(stage) +
                ". Elapsed: " + std::to_string(elapsed.count()) +
                " ms; configured budget: " +
                std::to_string(budget.count()) + " ms."
        };
    }

    vision::core::Error makeInvalidImageError(
        std::string_view cameraRole,
        std::string_view diagnostic)
    {
        return vision::core::Error{
            vision::core::ErrorCode::InvalidImage,
            "VisionAlgorithm received an invalid " +
                std::string(cameraRole) + " image.",
            std::string(diagnostic)
        };
    }

	
    vision::core::AlgorithmOutput makeNoTargetOutput() noexcept
    {
        return vision::core::AlgorithmOutput{
            .x = 0.0,
            .y = 0.0,
            .r = 0.0,
            .valid = false
        };
    }

    vision::core::AlgorithmOutcome makeValidOutput(
        double x,
        double y,
        double r)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(r)) {
            return vision::core::Error{
                vision::core::ErrorCode::InvalidResult,
                "VisionAlgorithm produced a non-finite pose.",
                "The final x, y or r value contains NaN or infinity."
            };
        }

        return vision::core::AlgorithmOutput{
            .x = x,
            .y = y,
            .r = r,
            .valid = true
        };
    }

}	// namespace vision::algorithm::detail