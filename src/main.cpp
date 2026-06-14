#include "MainWindow.h"
#include "MacroEvent.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>

#ifndef TASKAUTOMATION_VERSION
#define TASKAUTOMATION_VERSION "0.1.0"
#endif

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("TaskAutomation");
    QCoreApplication::setApplicationName("Task Automation");
    QCoreApplication::setApplicationVersion(QStringLiteral(TASKAUTOMATION_VERSION));
    QApplication::setWindowIcon(QIcon(":/icons/taskautomation.png"));
    qRegisterMetaType<MacroEvent>("MacroEvent");

    MainWindow window;
    window.show();
    return app.exec();
}
