#include "MainWindow.h"

#include "EvdevRecorder.h"
#include "MacroFile.h"
#include "MacroPlayer.h"
#include "KWinCursorBridge.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMetaType>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

#ifndef TASKAUTOMATION_VERSION
#define TASKAUTOMATION_VERSION "0.1.0"
#endif

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_appIcon(":/icons/taskautomation.png"),
      m_recordingIcon(":/icons/recording-stop-red.png")
{
    setWindowTitle("Task Automation");
    setWindowIcon(m_appIcon);
    resize(640, 420);
    setCentralWidget(buildCentralWidget());
    buildTray();
    m_cursorBridge = new KWinCursorBridge(this);
    connect(m_cursorBridge, &KWinCursorBridge::statusMessage, this, &MainWindow::setStatus);
    QString bridgeError;
    m_cursorBridge->start(&bridgeError);
    loadSettings();
    updatePreview();
    refreshControls();
    updateTrayState();
}

MainWindow::~MainWindow()
{
    saveSettings();
    if (m_recorder) {
        m_recorder->requestStop();
        m_recorder->wait(1000);
    }
    if (m_player) {
        m_player->requestStop();
        m_player->wait(1000);
    }
}

QWidget* MainWindow::buildCentralWidget()
{
    auto* root = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(root);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto* topGrid = new QGridLayout();
    topGrid->setHorizontalSpacing(8);
    topGrid->setVerticalSpacing(6);

    auto* recordGroup = new QGroupBox("Record", root);
    auto* recordLayout = new QGridLayout(recordGroup);
    recordLayout->setContentsMargins(8, 8, 8, 8);
    recordLayout->setHorizontalSpacing(6);
    recordLayout->setVerticalSpacing(4);

    m_countdownSlider = new QSlider(Qt::Horizontal, recordGroup);
    m_countdownSlider->setRange(0, 10);
    m_countdownSlider->setTickPosition(QSlider::TicksBelow);
    m_countdownSlider->setTickInterval(1);
    m_countdownSlider->setValue(3);
    m_countdownValueLabel = new QLabel("3 sec", recordGroup);
    m_countdownValueLabel->setMinimumWidth(48);

    m_minimizeOnRecordCheck = new QCheckBox("Minimize to tray on start", recordGroup);
    m_showTrayCheck = new QCheckBox("Show in systray", recordGroup);

    m_recordButton = new QPushButton("Record", recordGroup);
    m_stopRecordButton = new QPushButton("Stop", recordGroup);

    recordLayout->addWidget(new QLabel("Countdown", recordGroup), 0, 0);
    recordLayout->addWidget(m_countdownSlider, 0, 1);
    recordLayout->addWidget(m_countdownValueLabel, 0, 2);
    recordLayout->addWidget(m_minimizeOnRecordCheck, 1, 0, 1, 3);
    recordLayout->addWidget(m_showTrayCheck, 2, 0, 1, 3);
    recordLayout->addWidget(new QLabel("", recordGroup), 3, 0, 1, 3);
    recordLayout->addWidget(m_recordButton, 4, 0, 1, 2);
    recordLayout->addWidget(m_stopRecordButton, 4, 2);

    auto* playbackGroup = new QGroupBox("Playback", root);
    auto* playbackLayout = new QGridLayout(playbackGroup);
    playbackLayout->setContentsMargins(8, 8, 8, 8);
    playbackLayout->setHorizontalSpacing(6);
    playbackLayout->setVerticalSpacing(4);

    m_speedSpin = new QDoubleSpinBox(playbackGroup);
    m_speedSpin->setRange(0.05, 100.0);
    m_speedSpin->setDecimals(2);
    m_speedSpin->setSingleStep(0.25);
    m_speedSpin->setSuffix("x");
    m_speedSpin->setValue(1.0);

    m_replayCountSpin = new QSpinBox(playbackGroup);
    m_replayCountSpin->setRange(1, 999999);
    m_replayCountSpin->setValue(1);
    m_infiniteCheck = new QCheckBox("Infinite", playbackGroup);

    m_replayGapSpin = new QDoubleSpinBox(playbackGroup);
    m_replayGapSpin->setRange(0.0, 86400.0);
    m_replayGapSpin->setDecimals(3);
    m_replayGapSpin->setSingleStep(0.5);
    m_replayGapSpin->setSuffix(" sec");

    m_afterTaskCombo = new QComboBox(playbackGroup);
    m_afterTaskCombo->addItems(QStringList()
                               << "Do nothing"
                               << "Exit"
                               << "Log out"
                               << "Hibernate"
                               << "Reboot"
                               << "Shutdown");

    m_playButton = new QPushButton("Play", playbackGroup);
    m_stopPlaybackButton = new QPushButton("Stop", playbackGroup);

    playbackLayout->addWidget(new QLabel("Speed", playbackGroup), 0, 0);
    playbackLayout->addWidget(m_speedSpin, 0, 1, 1, 2);
    playbackLayout->addWidget(new QLabel("Replay", playbackGroup), 1, 0);
    playbackLayout->addWidget(m_replayCountSpin, 1, 1);
    playbackLayout->addWidget(m_infiniteCheck, 1, 2);
    playbackLayout->addWidget(new QLabel("Gap", playbackGroup), 2, 0);
    playbackLayout->addWidget(m_replayGapSpin, 2, 1, 1, 2);
    playbackLayout->addWidget(new QLabel("After task done", playbackGroup), 3, 0);
    playbackLayout->addWidget(m_afterTaskCombo, 3, 1, 1, 2);
    playbackLayout->addWidget(m_playButton, 4, 0, 1, 2);
    playbackLayout->addWidget(m_stopPlaybackButton, 4, 2);


    // Keep the Record/Stop row horizontally aligned with the Play/Stop row.
    for (int row = 0; row <= 4; ++row) {
        recordLayout->setRowMinimumHeight(row, 36);
        playbackLayout->setRowMinimumHeight(row, 36);
    }

    topGrid->addWidget(recordGroup, 0, 0);
    topGrid->addWidget(playbackGroup, 0, 1);
    topGrid->setColumnStretch(0, 1);
    topGrid->setColumnStretch(1, 1);
    mainLayout->addLayout(topGrid);

    auto* fileRow = new QWidget(root);
    auto* fileLayout = new QHBoxLayout(fileRow);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(6);
    m_loadButton = new QPushButton("Load", fileRow);
    m_saveButton = new QPushButton("Save", fileRow);
    m_clearButton = new QPushButton("Clear", fileRow);
    m_aboutButton = new QPushButton("About", fileRow);
    m_exitButton = new QPushButton("Exit", fileRow);
    fileLayout->addWidget(m_loadButton);
    fileLayout->addWidget(m_saveButton);
    fileLayout->addWidget(m_clearButton);
    fileLayout->addStretch(1);
    fileLayout->addWidget(m_aboutButton);
    fileLayout->addWidget(m_exitButton);
    mainLayout->addWidget(fileRow);

    m_fileLabel = new QLabel("Loaded file: No file loaded", root);
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fileLabel->setWordWrap(true);
    mainLayout->addWidget(m_fileLabel);

    m_preview = new QTextEdit(root);
    m_preview->setReadOnly(true);
    m_preview->setLineWrapMode(QTextEdit::NoWrap);
    m_preview->setMinimumHeight(180);
    mainLayout->addWidget(new QLabel("Macro preview: timestamps and events only", root));
    mainLayout->addWidget(m_preview, 1);

    m_statusLabel = new QLabel("Ready", root);
    mainLayout->addWidget(m_statusLabel);

    connect(m_countdownSlider, &QSlider::valueChanged, this, &MainWindow::updateCountdownLabel);
    connect(m_recordButton, &QPushButton::clicked, this, &MainWindow::startRecordingCountdown);
    connect(m_stopRecordButton, &QPushButton::clicked, this, &MainWindow::stopRecording);
    connect(m_loadButton, &QPushButton::clicked, this, &MainWindow::loadMacro);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::saveMacro);
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearMacro);
    connect(m_aboutButton, &QPushButton::clicked, this, &MainWindow::showAbout);
    connect(m_exitButton, &QPushButton::clicked, this, &MainWindow::exitApp);
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::playMacro);
    connect(m_stopPlaybackButton, &QPushButton::clicked, this, &MainWindow::stopPlayback);
    connect(m_infiniteCheck, &QCheckBox::toggled, this, &MainWindow::toggleInfinite);
    connect(m_showTrayCheck, &QCheckBox::toggled, this, &MainWindow::updateTrayState);

    return root;
}

void MainWindow::buildTray()
{
    m_tray = new QSystemTrayIcon(m_appIcon, this);
    m_tray->setToolTip("Task Automation");
    auto* menu = new QMenu(this);
    m_trayShowAction = menu->addAction("Show Task Automation", this, [this]() {
        showMainWindow();
    });
    m_trayStopAction = menu->addAction("Stop Recording/Playback", this, [this]() {
        if (isRecording()) stopRecording();
        if (isPlaying()) stopPlayback();
        showMainWindow();
    });
    menu->addSeparator();
    m_trayAboutAction = menu->addAction("About", this, &MainWindow::showAbout);
    m_trayQuitAction = menu->addAction("Exit", this, &MainWindow::exitApp);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
}

void MainWindow::loadSettings()
{
    QSettings s("TaskAutomation", "Task Automation");
    m_countdownSlider->setValue(s.value("record/countdown", 3).toInt());
    m_minimizeOnRecordCheck->setChecked(s.value("record/minimizeOnStart", false).toBool());
    m_showTrayCheck->setChecked(s.value("ui/showTray", true).toBool());
    m_speedSpin->setValue(s.value("playback/speed", 1.0).toDouble());
    m_replayCountSpin->setValue(s.value("playback/replayCount", 1).toInt());
    m_infiniteCheck->setChecked(s.value("playback/infinite", false).toBool());
    m_replayGapSpin->setValue(s.value("playback/replayGap", 0.0).toDouble());
    const QVariant afterTaskValue = s.value("playback/afterTaskDone", QStringLiteral("Do nothing"));
    if (afterTaskValue.typeId() == QMetaType::QString) {
        const int index = m_afterTaskCombo->findText(afterTaskValue.toString());
        m_afterTaskCombo->setCurrentIndex(index >= 0 ? index : 0);
    } else {
        // Backward compatibility for older config files that stored the selected row index
        // before "Exit" was inserted after "Do nothing".
        const int oldIndex = afterTaskValue.toInt();
        QString oldAction = QStringLiteral("Do nothing");
        switch (oldIndex) {
        case 1: oldAction = QStringLiteral("Log out"); break;
        case 2: oldAction = QStringLiteral("Hibernate"); break;
        case 3: oldAction = QStringLiteral("Reboot"); break;
        case 4: oldAction = QStringLiteral("Shutdown"); break;
        default: break;
        }
        const int index = m_afterTaskCombo->findText(oldAction);
        m_afterTaskCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    updateCountdownLabel(m_countdownSlider->value());
    toggleInfinite(m_infiniteCheck->isChecked());
}

void MainWindow::saveSettings()
{
    QSettings s("TaskAutomation", "Task Automation");
    s.setValue("record/countdown", m_countdownSlider->value());
    s.setValue("record/minimizeOnStart", m_minimizeOnRecordCheck->isChecked());
    s.setValue("ui/showTray", m_showTrayCheck->isChecked());
    s.setValue("playback/speed", m_speedSpin->value());
    s.setValue("playback/replayCount", m_replayCountSpin->value());
    s.setValue("playback/infinite", m_infiniteCheck->isChecked());
    s.setValue("playback/replayGap", m_replayGapSpin->value());
    s.setValue("playback/afterTaskDone", m_afterTaskCombo->currentText());
}

bool MainWindow::isRecording() const
{
    return m_recorder && m_recorder->isRunning();
}

bool MainWindow::isPlaying() const
{
    return m_player && (m_player->isRunning() || m_playbackStarting);
}

void MainWindow::refreshControls()
{
    const bool recording = isRecording();
    const bool playing = isPlaying();
    m_recordButton->setEnabled(!recording && !playing);
    m_stopRecordButton->setEnabled(recording);
    m_playButton->setEnabled(!recording && !playing && !m_events.isEmpty());
    m_stopPlaybackButton->setEnabled(playing);
    m_loadButton->setEnabled(!recording && !playing);
    m_saveButton->setEnabled(!recording && !m_events.isEmpty());
    if (m_clearButton) m_clearButton->setEnabled(!recording && !playing && !m_events.isEmpty());
    if (m_aboutButton) m_aboutButton->setEnabled(true);
    if (m_exitButton) m_exitButton->setEnabled(true);
    m_countdownSlider->setEnabled(!recording && !playing);
    m_minimizeOnRecordCheck->setEnabled(!recording && !playing);
    m_speedSpin->setEnabled(!recording && !playing);
    m_replayGapSpin->setEnabled(!recording && !playing);
    if (m_afterTaskCombo) m_afterTaskCombo->setEnabled(!recording && !playing);
    m_infiniteCheck->setEnabled(!recording && !playing);
    m_replayCountSpin->setEnabled(!recording && !playing && !m_infiniteCheck->isChecked());

    if (m_trayStopAction) {
        m_trayStopAction->setEnabled(recording || playing);
        m_trayStopAction->setText(recording ? "Stop Recording" : (playing ? "Stop Playback" : "Stop Recording/Playback"));
    }
    if (m_trayShowAction) m_trayShowAction->setEnabled(!recording);
    updateTrayState();
}

void MainWindow::setStatus(const QString& message)
{
    m_statusLabel->setText(message);
    if (m_tray && m_tray->isVisible()) {
        m_tray->setToolTip(QString("Task Automation - %1").arg(message));
    }
}

void MainWindow::updateCountdownLabel(int value)
{
    if (m_countdownValueLabel) {
        m_countdownValueLabel->setText(QString("%1 sec").arg(value));
    }
}

void MainWindow::startRecordingCountdown()
{
    if (m_recorder || m_player) return;
    m_countdownRemaining = m_countdownSlider->value();
    countdownStep();
}

void MainWindow::countdownStep()
{
    if (m_countdownRemaining <= 0) {
        beginRecording();
        return;
    }
    setStatus(QString("Recording starts in %1...").arg(m_countdownRemaining));
    --m_countdownRemaining;
    QTimer::singleShot(1000, this, &MainWindow::countdownStep);
}

void MainWindow::beginRecording()
{
    m_events.clear();
    m_currentPath.clear();
    if (m_fileLabel) m_fileLabel->setText("Loaded file: No file loaded");
    updatePreview();

    m_recorder = new EvdevRecorder(this);
    m_recorder->setCursorBridge(m_cursorBridge);

    connect(m_recorder, &EvdevRecorder::statusMessage, this, &MainWindow::setStatus);
    connect(m_recorder, &EvdevRecorder::failed, this, [this](const QString& error) {
        QMessageBox::critical(this, "Recording failed", error);
        setStatus(error);
    });
    connect(m_recorder, &EvdevRecorder::eventRecorded, this, [this](const MacroEvent&, int total) {
        setStatus(QString("Recording... %1 event(s)").arg(total));
    });
    connect(m_recorder, &QThread::finished, this, &MainWindow::onRecordingFinished);

    setStatus("Recording started.");
    if (m_tray) {
        m_tray->setVisible(true);
        m_tray->setIcon(m_recordingIcon);
        m_tray->setToolTip("Task Automation - Recording. Click red icon to stop.");
    }
    m_recorder->start();
    refreshControls();

    if (m_minimizeOnRecordCheck->isChecked()) {
        if (m_tray) {
            m_tray->setVisible(true);
            hide();
        } else {
            showMinimized();
        }
    }
}

void MainWindow::stopRecording()
{
    if (m_recorder) {
        // The Stop button and red tray icon are allowed. Their own click is removed
        // from the tail of the recording by the recorder.
        m_recorder->requestStop(1.25);
        setStatus("Stopping recording and removing Stop click...");
    }
}

void MainWindow::onRecordingFinished()
{
    if (m_recorder) {
        m_events = m_recorder->events();
        m_recorder->deleteLater();
        m_recorder = nullptr;
    }
    updatePreview();
    showMainWindow();
    setStatus(QString("Recording stopped. %1 event(s).").arg(m_events.size()));
    refreshControls();
}

void MainWindow::loadMacro()
{
    const QString path = QFileDialog::getOpenFileName(this, "Load macro file", QString(), "Task Automation (*.auto);;Text files (*.txt);;All files (*)");
    if (path.isEmpty()) return;
    MacroEvents loaded;
    QString error;
    if (!MacroFile::load(path, loaded, &error)) {
        QMessageBox::critical(this, "Load failed", error);
        return;
    }
    m_events = loaded;
    m_currentPath = path;
    m_fileLabel->setText(QString("Loaded file: %1").arg(path));
    updatePreview();
    setStatus(QString("Loaded %1 event(s).").arg(m_events.size()));
    refreshControls();
}

void MainWindow::saveMacro()
{
    QString path = m_currentPath;
    const QString filter = "Task Automation (*.auto);;Text files (*.txt);;All files (*)";
    if (path.isEmpty()) {
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
        path = QFileDialog::getSaveFileName(this, "Save macro file", QString("macro-%1.auto").arg(stamp), filter);
    } else {
        path = QFileDialog::getSaveFileName(this, "Save macro file", path, filter);
    }
    if (path.isEmpty()) return;
    if (!path.endsWith(".auto") && !path.endsWith(".txt")) path += ".auto";

    QString error;
    if (!MacroFile::save(path, m_events, &error)) {
        QMessageBox::critical(this, "Save failed", error);
        return;
    }
    m_currentPath = path;
    m_fileLabel->setText(QString("Loaded file: %1").arg(path));
    setStatus("Saved macro file.");
}

void MainWindow::clearMacro()
{
    if (isRecording() || isPlaying()) return;
    m_events.clear();
    m_currentPath.clear();
    if (m_fileLabel) m_fileLabel->setText("Loaded file: No file loaded");
    updatePreview();
    setStatus("Cleared recorded log.");
}

void MainWindow::showAbout()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Task Automation");
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel("<b>Task Automation</b>", &dialog);
    titleLabel->setAlignment(Qt::AlignCenter);

    auto* versionLabel = new QLabel(QString("Version: %1").arg(QStringLiteral(TASKAUTOMATION_VERSION)), &dialog);
    versionLabel->setAlignment(Qt::AlignCenter);

    auto* githubLabel = new QLabel(&dialog);
    githubLabel->setText(QStringLiteral("GitHub: <a href=\"https://github.com/yousefvand/Task-Automation\">https://github.com/yousefvand/Task-Automation</a>"));
    githubLabel->setTextFormat(Qt::RichText);
    githubLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    githubLabel->setOpenExternalLinks(true);
    githubLabel->setAlignment(Qt::AlignCenter);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

    layout->addWidget(titleLabel);
    layout->addWidget(versionLabel);
    layout->addWidget(githubLabel);
    layout->addSpacing(6);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::exitApp()
{
    saveSettings();

    if (m_recorder) {
        m_recorder->requestStop(0.0);
        m_recorder->wait(1000);
    }

    if (m_player) {
        m_player->requestStop();
        m_player->wait(1000);
    }

    if (m_tray) {
        m_tray->hide();
    }

    qApp->quit();
}

void MainWindow::playMacro()
{
    if (m_events.isEmpty() || m_recorder || m_player) return;

    auto options = MacroPlayer::Options{};
    options.speed = m_speedSpin->value();
    options.replayCount = m_replayCountSpin->value();
    options.infinite = m_infiniteCheck->isChecked();
    options.replayGapSeconds = m_replayGapSpin->value();
    QRect screenGeometry;
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (!screen) continue;
        screenGeometry = screenGeometry.isNull() ? screen->geometry() : screenGeometry.united(screen->geometry());
    }
    options.screenGeometry = screenGeometry;

    m_player = new MacroPlayer(this);
    m_player->setCursorBridge(m_cursorBridge);
    m_player->setMacro(m_events);
    m_player->setOptions(options);

    connect(m_player, &MacroPlayer::statusMessage, this, &MainWindow::setStatus);
    connect(m_player, &MacroPlayer::failed, this, [this](const QString& error) {
        m_playbackHadError = true;
        showMainWindow();
        QMessageBox::critical(this, "Playback failed", error);
        setStatus(error);
    });
    connect(m_player, &MacroPlayer::replayStarted, this, [this](int index) {
        setStatus(QString("Playing replay %1...").arg(index));
    });
    connect(m_player, &QThread::finished, this, &MainWindow::onPlaybackFinished);

    m_playbackStarting = true;
    m_playbackStoppedByUser = false;
    m_playbackHadError = false;

    if (m_tray) {
        m_tray->setVisible(true);
        m_tray->setIcon(m_appIcon);
        m_tray->setToolTip("Task Automation - Playing macro...");
    }
    hide();
    refreshControls();

    // Give KDE a moment to hide the app window before the first click is replayed.
    // Without this, the first recorded click can hit Task Automation itself.
    QTimer::singleShot(600, this, [this]() {
        if (!m_player || !m_playbackStarting) return;
        m_playbackStarting = false;
        m_player->start();
        refreshControls();
    });
}

void MainWindow::stopPlayback()
{
    if (m_player) {
        m_playbackStoppedByUser = true;
        if (m_playbackStarting && !m_player->isRunning()) {
            m_player->deleteLater();
            m_player = nullptr;
            m_playbackStarting = false;
            setStatus("Playback cancelled.");
            refreshControls();
            showMainWindow();
            return;
        }
        m_player->requestStop();
        setStatus("Stopping playback...");
    }
}

void MainWindow::onPlaybackFinished()
{
    if (m_player) {
        m_player->deleteLater();
        m_player = nullptr;
    }
    m_playbackStarting = false;
    const bool shouldRunAfterAction = !m_playbackStoppedByUser && !m_playbackHadError;
    setStatus(shouldRunAfterAction ? "Playback finished." : "Playback stopped.");
    refreshControls();
    showMainWindow();
    if (shouldRunAfterAction) {
        runAfterTaskDoneAction();
    }
}

void MainWindow::runAfterTaskDoneAction()
{
    if (!m_afterTaskCombo) return;

    const QString action = m_afterTaskCombo->currentText();
    if (action == "Do nothing") return;

    if (action == "Exit") {
        exitApp();
        return;
    }

    if (action == "Log out") {
        if (!QProcess::startDetached("qdbus6", QStringList() << "org.kde.Shutdown" << "/Shutdown" << "logout")) {
            QProcess::startDetached("qdbus", QStringList() << "org.kde.Shutdown" << "/Shutdown" << "logout");
        }
        return;
    }

    if (action == "Hibernate") {
        QProcess::startDetached("systemctl", QStringList() << "hibernate");
        return;
    }

    if (action == "Reboot") {
        QProcess::startDetached("systemctl", QStringList() << "reboot");
        return;
    }

    if (action == "Shutdown") {
        QProcess::startDetached("systemctl", QStringList() << "poweroff");
        return;
    }
}

void MainWindow::updatePreview()
{
    if (!m_preview) return;
    m_preview->setPlainText(MacroFile::toText(m_events));
    refreshControls();
}

void MainWindow::updateTrayState()
{
    if (!m_tray) return;
    const bool recording = isRecording();
    const bool playing = isPlaying();
    const bool shouldShow = m_showTrayCheck->isChecked() || recording || playing;
    m_tray->setVisible(shouldShow);
    if (recording) {
        m_tray->setIcon(m_recordingIcon);
        m_tray->setToolTip("Task Automation - Recording. Click red icon to stop.");
    } else if (playing) {
        m_tray->setIcon(m_appIcon);
        m_tray->setToolTip("Task Automation - Playing macro...");
    } else {
        m_tray->setIcon(m_appIcon);
        m_tray->setToolTip("Task Automation");
    }
}

void MainWindow::toggleInfinite(bool enabled)
{
    if (m_replayCountSpin) m_replayCountSpin->setEnabled(!enabled && !isRecording() && !isPlaying());
}

void MainWindow::showMainWindow()
{
    showNormal();
    raise();
    activateWindow();
    updateTrayState();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick) return;

    if (isRecording()) {
        stopRecording();
        showMainWindow();
        return;
    }

    if (isPlaying()) {
        return;
    }

    showMainWindow();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    if (m_tray && m_tray->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
