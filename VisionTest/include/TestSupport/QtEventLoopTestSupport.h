#include "TestSupport/TestDirectory.h"
#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

namespace vision::test::support {


inline void ensureQCoreApplication() {
    if (!QCoreApplication::instance())
        throw std::logic_error("Qt application must be owned by the test Environment");
}

template <typename Predicate>
bool processEventsUntil(Predicate&& predicate,
                        std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (std::forward<Predicate>(predicate)()) {
            return true;
        }
    } while (elapsed.elapsed() < timeout.count());

    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return std::forward<Predicate>(predicate)();
}

} // namespace vision::test::support
