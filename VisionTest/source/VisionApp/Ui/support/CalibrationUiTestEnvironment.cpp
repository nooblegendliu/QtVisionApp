#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QMessageBox>
#include <memory>
#include <string>

namespace vision::test::app { namespace {
class TestEnvironment final : public testing::Environment {
public:
    void SetUp() override {
        const QByteArray group = qgetenv("VISIONTEST_GROUP");
        const bool ui = group == "Ui" || group == "ManualUi";
        int count = 0;
        const auto* unit = testing::UnitTest::GetInstance();
        for (int i = 0; i < unit->total_test_case_count(); ++i) {
            const auto* suite = unit->GetTestCase(i);
            for (int j = 0; j < suite->total_test_count(); ++j) {
                if (!suite->GetTestInfo(j)->should_run()) continue;
                ++count;
                const std::string name = suite->name();
                const bool automaticUi = name.find("DISABLED_App_Ui_") != std::string::npos;
                const bool manualUi = name.find("DISABLED_App_ManualUi_") != std::string::npos;
                const bool mvs = name.find("DISABLED_Hardware_Mvs_") != std::string::npos;
                ASSERT_EQ(automaticUi, group == "Ui") << "UI must run in its isolated group";
                ASSERT_EQ(manualUi, group == "ManualUi") << "Manual UI requires explicit group";
                ASSERT_EQ(mvs, group == "MvsEnvironment") << "MVS must run in its isolated group";
            }
        }
        ASSERT_GT(count, 0) << "The selected group contains no executable tests";
        ASSERT_EQ(QCoreApplication::instance(), nullptr);
        resources_ = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(resources_->isValid());
        qputenv("VISIONTEST_RESOURCE_ROOT", resources_->path().toUtf8());
        if (ui) application_ = std::make_unique<QApplication>(argc_, argv_);
        else application_ = std::make_unique<QCoreApplication>(argc_, argv_);
        QCoreApplication::setOrganizationName("QtVisionApp-Tests");
        QCoreApplication::setApplicationName("VisionTest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, resources_->path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, resources_->path());
        if (ui) {
            static_cast<QApplication*>(application_.get())->setQuitOnLastWindowClosed(false);
            ASSERT_FALSE(qEnvironmentVariable("VISIONTEST_OUTPUT").isEmpty());
            qputenv("VISIONTEST_CALIB_UI_OUTPUT", qgetenv("VISIONTEST_OUTPUT"));
            // Manual observation has its own user-driven lifetime; automatic tests
            // must not wait indefinitely in an unexpected nested modal event loop.
            if (group == "Ui") {
                modalWatch_ = std::make_unique<QTimer>();
                QObject::connect(modalWatch_.get(), &QTimer::timeout, [this] {
                    auto* current = QApplication::activeModalWidget();
                    if (current != modal_) { modal_ = current; modalAge_.restart(); }
                    if (current && modalAge_.isValid() && modalAge_.elapsed() >= 5000) {
                        ADD_FAILURE() << "Unhandled modal dialog: " << current->windowTitle().toStdString();
                        if (auto* box = qobject_cast<QMessageBox*>(current)) box->done(QMessageBox::No);
                        else current->close();
                        modal_.clear();
                    }
                });
                modalWatch_->start(100);
            }
        }
    }
    void TearDown() override {
        modalWatch_.reset();
        application_.reset();
        qunsetenv("VISIONTEST_RESOURCE_ROOT");
        resources_.reset();
    }
private:
    int argc_{1};
    char name_[11]{"VisionTest"};
    char* argv_[2]{name_, nullptr};
    std::unique_ptr<QTemporaryDir> resources_;
    std::unique_ptr<QCoreApplication> application_;
    std::unique_ptr<QTimer> modalWatch_;
    QPointer<QWidget> modal_;
    QElapsedTimer modalAge_;
};
[[maybe_unused]] auto* environment = testing::AddGlobalTestEnvironment(new TestEnvironment);
} }
