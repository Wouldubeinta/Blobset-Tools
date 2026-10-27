#include "app_orchestrator.h"
#include "app_paths.h"
#include "update_checker.h"
#include "command_line.h"
#include "dds_image_provider.h"
#include "audio_player.h"
#include "mod_manager.h"

#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSystemSemaphore>
#include <QSharedMemory>
#include <QMessageBox>
#include <QDir>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[]) {
    // QApplication (not QGuiApplication): the "already running" QMessageBox below is a widget.
    QApplication app(argc, argv);
    app.setApplicationName("Blobset Tools");
    // project(... VERSION x.x.x.x) in CMakeLists.txt: the same number the installer and the updater use.
    app.setApplicationVersion(QStringLiteral(BT_VERSION));
    app.setOrganizationName("Wouldy Mods");
    app.setWindowIcon(QIcon(":/qt/qml/com/wouldymods/blobset/assets/blobset_tools.ico"));

    // The writable files live in Documents\Blobset Tools (see app_paths.h): create / fill it before anything reads it.
    const QStringList startupLog = AppPaths::prepare();

    // Command line (scripts, the Vortex extensions): no window. It only needs to know whether the window is open.
    if (CommandLine::requested(argc, argv)) {
        QSharedMemory probe("BlobsetToolsSharedMemory");
        const bool guiRunning = probe.attach();
        if (guiRunning)
            probe.detach();
        return CommandLine::run(guiRunning, startupLog);
    }

    // Implement Single-Instance Application Lifecycle using cross-platform Qt IPC primitives
    QSystemSemaphore semaphore("BlobsetToolsSemaphore", 1);
    semaphore.acquire();
    
    QSharedMemory sharedMemory("BlobsetToolsSharedMemory");
    bool isRunning = sharedMemory.attach();
    if (!isRunning) {
        sharedMemory.create(1);
    }
    
    semaphore.release();

    // Prevent execution if the app is already actively mapped in memory
    if (isRunning) {
        QMessageBox::critical(nullptr,
                              "Application Already Running",
                              "Another instance of Blobset Tools is currently active.");
        return 0;
    }

    AppOrchestrator orchestrator;
    // What the data folder setup did (first run / moved files): shown once the workspace's Console Log opens.
    orchestrator.setStartupMessages(QStringList{u"Data folder: "_s + QDir::toNativeSeparators(AppPaths::dataDir())} + startupLog);
    AudioPlayer audioPlayer;
    ModManager modManager(&orchestrator);
    UpdateChecker updater;
    QQmlApplicationEngine engine;

    QObject::connect(&audioPlayer, &AudioPlayer::message,
                     &orchestrator, &AppOrchestrator::uiConsoleUpdate);

    engine.rootContext()->setContextProperty("appProductName", app.applicationName());
    engine.rootContext()->setContextProperty("appProductVersion", app.applicationVersion());
    engine.rootContext()->setContextProperty("appCopyright", "Copyright © 2026 Wouldy Mods. All rights reserved.");

    engine.rootContext()->setContextProperty("audioPlayer", &audioPlayer);
    engine.rootContext()->setContextProperty("modManager", &modManager);
    engine.rootContext()->setContextProperty("updater", &updater);

    engine.addImageProvider(QStringLiteral("dds_provider"), new DdsImageProvider());

    // Inject the AppOrchestrator singleton strictly into the QML engine's root context
    engine.rootContext()->setContextProperty("appOrchestrator", &orchestrator);

    // Expose the executable directory path (read only files) and the data folder (Documents\Blobset Tools) to QML
    engine.rootContext()->setContextProperty("applicationDirPath", app.applicationDirPath());
    engine.rootContext()->setContextProperty("dataDirPath", AppPaths::dataDir());
    
    // Defer loading main.qml until the QML subsystem is entirely synthesized in the next steps
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/com/wouldymods/blobset/qml/main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }


    return app.exec();
}
