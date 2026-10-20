#pragma once

#include <VisionCore/LocateTypes.h>

#include <chrono>
#include <optional>
#include <string_view>

namespace vision::algorithm::detail {
	struct ExecutionContext {
		std::chrono::steady_clock::time_point startedAt;
		std::chrono::steady_clock::time_point deadline;

		[[nodiscard]] bool expired() const noexcept
		{
			return std::chrono::steady_clock::now() >= deadline;
		}

		[[nodiscard]] std::chrono::milliseconds budget() const noexcept
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				deadline - startedAt);
		}

		[[nodiscard]] std::chrono::milliseconds elapsed() const noexcept
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - startedAt);
		}
	};


	[[nodiscard]] vision::core::Error makeAlgorithmError(
		std::string_view message,
		std::string_view diagnostic = {});


	vision::core::Error makeTimeoutError(
		std::string_view stage,
		std::chrono::milliseconds elapsed,
		std::chrono::milliseconds budget);


	// 0804.add
	[[nodiscard]] std::optional<vision::core::Error> checkDeadline(
		const ExecutionContext& context,
		std::string_view stage);


	[[nodiscard]] vision::core::Error makeInvalidImageError(
		std::string_view cameraRole,
		std::string_view diagnostic);


	[[nodiscard]] vision::core::Error makeAlgorithmDisabledError(
		std::string_view algorithmName);


	[[nodiscard]] vision::core::AlgorithmOutput makeNoTargetOutput() noexcept;


	[[nodiscard]] vision::core::AlgorithmOutcome makeValidOutput(
		double x,
		double y,
		double r);

}	// namespace vision::algorithm::detail