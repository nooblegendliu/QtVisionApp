#include "VisionApp/Ui/support/CalibrationUiTestSupport.h"
#include "WindowCoordinator.h"
#include "MonitorWindow.h"
#include <QApplication>
#include <QPushButton>
#include <QTabWidget>
#include <QTabBar>
#include <QSpinBox>
#include <QMessageBox>
#include <QTimer>
#include <QTest>
#include <gtest/gtest.h>

namespace vision::test::app { namespace {
using namespace vision::app;
void selectTabContaining(QWidget* control) {
    for (QWidget* page=control;page;page=page->parentWidget()) {
        auto* stack=page->parentWidget();
        auto* tabs=stack ? qobject_cast<QTabWidget*>(stack->parentWidget()) : nullptr;
        if(tabs && tabs->indexOf(page)>=0) {
            selectTabContaining(tabs);
            QTest::mouseClick(tabs->tabBar(),Qt::LeftButton,Qt::NoModifier,
                tabs->tabBar()->tabRect(tabs->indexOf(page)).center());
        }
    }
    QApplication::processEvents();
}
void typeNumber(QSpinBox* spin,int value) {
    selectTabContaining(spin);spin->setFocus();
    QTest::keyClick(spin,Qt::Key_A,Qt::ControlModifier);
    QTest::keyClicks(spin,QString::number(value));QTest::keyClick(spin,Qt::Key_Tab);
}

TEST(DISABLED_App_Ui_Configuration, EditRevertAndApplyPersistOnlyAcceptedConfiguration) {
    CalibrationUiSession s("ConfigurationActions");s.start();
    auto* spin=s.window->findChild<QSpinBox*>("maximumRetriesSpinBox");
    auto* revert=s.window->findChild<QPushButton*>("revertConfigurationButton");
    auto* apply=s.window->findChild<QPushButton*>("applyConfigurationButton");
    ASSERT_TRUE(spin && revert && apply);
    const int initial=s.controller->configuration().resultDelivery.maximumRetries;
    const auto revision=s.controller->snapshot().activeConfigurationRevision;
    typeNumber(spin,initial+1);
    ASSERT_TRUE(s.window->hasUnsavedChanges());ASSERT_TRUE(revert->isEnabled());
    QTest::mouseClick(revert,Qt::LeftButton);
    EXPECT_EQ(spin->value(),initial);EXPECT_FALSE(s.window->hasUnsavedChanges());
    typeNumber(spin,initial+1);ASSERT_TRUE(apply->isEnabled());
    bool confirmed=false;
    QTimer::singleShot(0,s.window.get(),[&] {
        auto* dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if(dialog && dialog->button(QMessageBox::Yes)) {
            confirmed=true;QTest::mouseClick(dialog->button(QMessageBox::Yes),Qt::LeftButton);
        }
    });
    QTest::mouseClick(apply,Qt::LeftButton);
    ASSERT_TRUE(confirmed);
    s.waitFor([&]{return s.controller->snapshot().activeConfigurationRevision==revision+1;},"configuration apply");
    EXPECT_EQ(s.controller->configuration().resultDelivery.maximumRetries,initial+1);
    EXPECT_FALSE(s.window->hasUnsavedChanges());
    auto disk=loadApplicationConfig(s.controller->configurationPath());
    ASSERT_TRUE(std::holds_alternative<ApplicationConfig>(disk));
    EXPECT_EQ(std::get<ApplicationConfig>(disk).resultDelivery.maximumRetries,initial+1);
    EXPECT_EQ(s.runtime->captures,0);EXPECT_EQ(s.runtime->locateCalls,0);
}

TEST(DISABLED_App_Ui_Windows, DisplaySwitchPreservesBusinessAndDirtyExitRequiresConfirmation) {
    CalibrationUiSession s("WindowSwitchAndExit");s.start();s.window->hide();
    WindowCoordinator coordinator(s.controller.get(),QStringLiteral("Test"));
    coordinator.showInitialWindow();
    MainWindow* detailed=nullptr;MonitorWindow* monitor=nullptr;
    for(auto* w:QApplication::topLevelWidgets()) {
        if(auto* value=qobject_cast<MainWindow*>(w);value && value!=s.window.get())detailed=value;
        if(auto* value=qobject_cast<MonitorWindow*>(w))monitor=value;
    }
    ASSERT_TRUE(detailed && monitor);
    const auto before=s.controller->snapshot();
    auto* toMonitor=detailed->findChild<QPushButton*>("openMonitorButton");ASSERT_TRUE(toMonitor);
    QTest::mouseClick(toMonitor,Qt::LeftButton);
    EXPECT_TRUE(monitor->isVisible());EXPECT_FALSE(detailed->isVisible());
    auto* toDetails=monitor->findChild<QPushButton*>("returnDetailedButton");ASSERT_TRUE(toDetails);
    QTest::mouseClick(toDetails,Qt::LeftButton);
    EXPECT_TRUE(detailed->isVisible());EXPECT_FALSE(monitor->isVisible());
    EXPECT_EQ(s.controller->snapshot().taskState,before.taskState);
    EXPECT_EQ(s.controller->snapshot().activeConfigurationRevision,before.activeConfigurationRevision);
    EXPECT_EQ(s.runtime->captures,0);EXPECT_EQ(s.runtime->locateCalls,0);
    auto* spin=detailed->findChild<QSpinBox*>("maximumRetriesSpinBox");ASSERT_TRUE(spin);
    typeNumber(spin,spin->value()+1);ASSERT_TRUE(detailed->hasUnsavedChanges());
    for(const auto answer:{QMessageBox::No,QMessageBox::Yes}) {
        bool answered=false;
        QTimer::singleShot(0,detailed,[&] {
            auto* dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if(dialog && dialog->button(answer)) {
                answered=true;QTest::mouseClick(dialog->button(answer),Qt::LeftButton);
            }
        });
        QTimer fallback;
        fallback.setSingleShot(true);
        QObject::connect(&fallback,&QTimer::timeout,detailed,[] {
            if(auto* dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))dialog->done(QMessageBox::No);
        });
        fallback.start(5000);detailed->close();fallback.stop();
        ASSERT_TRUE(answered);
        if(answer==QMessageBox::No) {EXPECT_FALSE(s.stopped);EXPECT_TRUE(detailed->isEnabled());}
        else s.waitFor([&]{return s.stopped;},"confirmed exit");
    }
}
} }
