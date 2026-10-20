#pragma once

#include <string>
#include <variant>

namespace vision::core {

enum class ErrorCode {
    NotReady,
    Busy,
    InvalidConfiguration,
    HardwareUnavailable,
    HardwareFailure,
    HardwareTimeout,
    InvalidImage,
    AlgorithmFailure,
    AlgorithmTimeout,
    InvalidResult,
    InternalFailure
};

struct Error {
    ErrorCode code;
    std::string message;
    std::string diagnostic;
};

template <typename T>
using Outcome = std::variant<T, Error>;

using Status = std::variant<std::monostate, Error>;

} // namespace vision::core
