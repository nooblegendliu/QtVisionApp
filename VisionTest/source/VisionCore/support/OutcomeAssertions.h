#pragma once

#include <variant>

#include <gtest/gtest.h>

#include "VisionCore/Error.h"

namespace vision::test::core {

template <typename T>
::testing::AssertionResult IsSuccess(const vision::core::Outcome<T>& outcome) {
    if (std::holds_alternative<T>(outcome)) {
        return ::testing::AssertionSuccess();
    }

    const vision::core::Error& error = std::get<vision::core::Error>(outcome);
    return ::testing::AssertionFailure()
           << "Expected a successful outcome, but received ErrorCode="
           << static_cast<int>(error.code) << ", message='" << error.message << "'.";
}

inline ::testing::AssertionResult IsSuccess(const vision::core::Status& status) {
    if (std::holds_alternative<std::monostate>(status)) {
        return ::testing::AssertionSuccess();
    }

    const vision::core::Error& error = std::get<vision::core::Error>(status);
    return ::testing::AssertionFailure()
           << "Expected successful Status, but received ErrorCode=" << static_cast<int>(error.code)
           << ", message='" << error.message << "'.";
}

template <typename T>
const T& SuccessValue(const vision::core::Outcome<T>& outcome) {
    return std::get<T>(outcome);
}

inline ::testing::AssertionResult HasErrorCode(const vision::core::Status& status,
                                                vision::core::ErrorCode expected) {
    if (!std::holds_alternative<vision::core::Error>(status)) {
        return ::testing::AssertionFailure()
               << "Expected ErrorCode=" << static_cast<int>(expected) << ", but Status succeeded.";
    }

    const vision::core::Error& error = std::get<vision::core::Error>(status);
    if (error.code == expected) {
        return ::testing::AssertionSuccess();
    }

    return ::testing::AssertionFailure()
           << "Expected ErrorCode=" << static_cast<int>(expected)
           << ", actual=" << static_cast<int>(error.code) << ".";
}

template <typename T>
::testing::AssertionResult HasErrorCode(const vision::core::Outcome<T>& outcome,
                                        vision::core::ErrorCode expected) {
    if (!std::holds_alternative<vision::core::Error>(outcome)) {
        return ::testing::AssertionFailure()
               << "Expected ErrorCode=" << static_cast<int>(expected) << ", but outcome succeeded.";
    }

    const vision::core::Error& error = std::get<vision::core::Error>(outcome);
    if (error.code == expected) {
        return ::testing::AssertionSuccess();
    }

    return ::testing::AssertionFailure()
           << "Expected ErrorCode=" << static_cast<int>(expected)
           << ", actual=" << static_cast<int>(error.code) << ".";
}

template <typename T>
::testing::AssertionResult IsOriginalError(const vision::core::Outcome<T>& outcome,
                                           const vision::core::Error& expected) {
    if (!std::holds_alternative<vision::core::Error>(outcome)) {
        return ::testing::AssertionFailure() << "Expected an Error outcome, but it succeeded.";
    }

    const vision::core::Error& actual = std::get<vision::core::Error>(outcome);
    if (actual.code == expected.code && actual.message == expected.message &&
        actual.diagnostic == expected.diagnostic) {
        return ::testing::AssertionSuccess();
    }

    return ::testing::AssertionFailure()
           << "Expected original Error{code=" << static_cast<int>(expected.code)
           << ", message='" << expected.message << "', diagnostic='" << expected.diagnostic
           << "'}, actual Error{code=" << static_cast<int>(actual.code) << ", message='"
           << actual.message << "', diagnostic='" << actual.diagnostic << "'}.";
}

inline ::testing::AssertionResult IsOriginalError(const vision::core::Status& status,
                                                  const vision::core::Error& expected) {
    if (!std::holds_alternative<vision::core::Error>(status)) {
        return ::testing::AssertionFailure() << "Expected an Error Status, but it succeeded.";
    }

    const vision::core::Error& actual = std::get<vision::core::Error>(status);
    if (actual.code == expected.code && actual.message == expected.message &&
        actual.diagnostic == expected.diagnostic) {
        return ::testing::AssertionSuccess();
    }

    return ::testing::AssertionFailure()
           << "Expected original Error{code=" << static_cast<int>(expected.code)
           << ", message='" << expected.message << "', diagnostic='" << expected.diagnostic
           << "'}, actual Error{code=" << static_cast<int>(actual.code) << ", message='"
           << actual.message << "', diagnostic='" << actual.diagnostic << "'}.";
}

} // namespace vision::test::core
