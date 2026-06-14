#pragma once

#include "MacroEvent.h"

#include <QIcon>
#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTextEdit;
class QAction;
class QCloseEvent;

class EvdevRecorder;
class MacroPlayer;
class KWinCursorBridge;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void startRecordingCountdown();
    void beginRecording();
    void stopRecording();
    void onRecordingFinished();

    void loadMacro();
    void saveMacro();
    void clearMacro();
    void showAbout();
    void exitApp();
    void playMacro();
    void stopPlayback();
    void onPlaybackFinished();
    void runAfterTaskDoneAction();

    void updatePreview();
    void updateTrayState();
    void setStatus(const QString& message);
    void toggleInfinite(bool enabled);
    void updateCountdownLabel(int value);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    QWidget* buildCentralWidget();
    void buildTray();
    void loadSettings();
    void saveSettings();
    void refreshControls();
    void countdownStep();
    void showMainWindow();
    bool isRecording() const;
    bool isPlaying() const;

    MacroEvents m_events;
    QString m_currentPath;

    QPointer<EvdevRecorder> m_recorder;
    QPointer<MacroPlayer> m_player;
    KWinCursorBridge* m_cursorBridge = nullptr;

    QIcon m_appIcon;
    QIcon m_recordingIcon;

    QSlider* m_countdownSlider = nullptr;
    QLabel* m_countdownValueLabel = nullptr;
    QCheckBox* m_minimizeOnRecordCheck = nullptr;
    QCheckBox* m_showTrayCheck = nullptr;
    QPushButton* m_recordButton = nullptr;
    QPushButton* m_stopRecordButton = nullptr;

    QDoubleSpinBox* m_speedSpin = nullptr;
    QSpinBox* m_replayCountSpin = nullptr;
    QCheckBox* m_infiniteCheck = nullptr;
    QDoubleSpinBox* m_replayGapSpin = nullptr;
    QComboBox* m_afterTaskCombo = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_stopPlaybackButton = nullptr;

    QPushButton* m_loadButton = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_aboutButton = nullptr;
    QPushButton* m_exitButton = nullptr;
    QLabel* m_fileLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTextEdit* m_preview = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_trayStopAction = nullptr;
    QAction* m_trayShowAction = nullptr;
    QAction* m_trayAboutAction = nullptr;
    QAction* m_trayQuitAction = nullptr;

    int m_countdownRemaining = 0;
    bool m_playbackStarting = false;
    bool m_playbackStoppedByUser = false;
    bool m_playbackHadError = false;
};
