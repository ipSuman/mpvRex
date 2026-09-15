#pragma once

#include <QMainWindow>
#include <QPointF>
#include <QKeySequence>
#include <QTimer>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QWidget;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QDockWidget;
class QKeyEvent;
class QProcess;

struct mpv_handle;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& mediaPath = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void pumpMpvEvents();
    void openFile();
    void addFiles();
    void addFolder();
    void clearPlaylist();
    void playlistActivated();
    void togglePause();
    void seekBackward();
    void seekForward();
    void seekTo(int value);
    void setVolume(int value);
    void updatePlaybackUi();
    void toggleControls();
    void togglePlaylist();
    void toggleHardwareDecoding();
    void showControlsDialog();
    void playPrevious();
    void playNext();
    void showTracksMenu();
    void cutAbSelection();
    void saveLogReport();

private:
    bool initializeMpv();
    void buildUi();
    void loadFile(const QString& path);
    void addToPlaylist(const QString& path);
    void playPlaylistIndex(int index);
    void syncPlaylistSelection();
    void command(const char** args);
    double getPropertyDouble(const char* name) const;
    QString getPropertyString(const char* name) const;
    void setPropertyDouble(const char* name, double value);
    void updateHardwareButton();
    void updatePlayButton(bool paused);
    void updateSeekButtonLabels();
    void adjustVideoZoom(double amount);
    void resetVideoTransform();
    void setAbLoopStart();
    void setAbLoopEnd();
    void clearAbLoop();
    void updateAbLoopLabel();
    void stepFrame(bool forward);
    void showError(const QString& message);
    QString formatTime(double seconds) const;
    void setControlsVisible(bool visible);
    void loadControlSettings();
    bool keyMatches(QKeyEvent* event, const QKeySequence& sequence) const;

    mpv_handle* m_mpv = nullptr;
    QTimer m_eventTimer;
    QTimer m_uiTimer;
    QWidget* m_videoWidget = nullptr;
    QWidget* m_controls = nullptr;
    QDockWidget* m_playlistDock = nullptr;
    QListWidget* m_playlist = nullptr;
    QSlider* m_seekSlider = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_abLoopLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_hwButton = nullptr;
    QPushButton* m_previousButton = nullptr;
    QPushButton* m_nextButton = nullptr;
    QPushButton* m_seekBackButton = nullptr;
    QPushButton* m_seekForwardButton = nullptr;
    QPushButton* m_cutAbButton = nullptr;
    QPushButton* m_logButton = nullptr;
    QProcess* m_cutProcess = nullptr;
    QString m_cutOutputPath;
    bool m_seeking = false;
    bool m_panningVideo = false;
    QPointF m_panStart;
    double m_videoPanX = 0.0;
    double m_videoPanY = 0.0;
    double m_abLoopStart = -1.0;
    double m_abLoopEnd = -1.0;
    int m_currentPlaylistIndex = -1;

    QString m_seekWheelMode = QStringLiteral("wheel");
    QString m_zoomWheelMode = QStringLiteral("alt-wheel");
    QString m_volumeWheelMode = QStringLiteral("ctrl-wheel");
    Qt::MouseButton m_panButton = Qt::MiddleButton;
    Qt::MouseButton m_doubleClickButton = Qt::LeftButton;
    bool m_doubleClickZones = true;
    int m_seekDurationMinutes = 1;
    QKeySequence m_seekBackwardKey = QKeySequence(Qt::Key_Left);
    QKeySequence m_seekForwardKey = QKeySequence(Qt::Key_Right);
    QKeySequence m_loopAKey = QKeySequence(Qt::Key_A);
    QKeySequence m_loopBKey = QKeySequence(Qt::Key_B);
    QKeySequence m_loopClearKey = QKeySequence(Qt::Key_L);
    QKeySequence m_zoomInKey = QKeySequence(Qt::SHIFT | Qt::Key_Equal);
    QKeySequence m_zoomOutKey = QKeySequence(Qt::Key_Minus);
    QKeySequence m_zoomResetKey = QKeySequence(Qt::Key_Z);
    QKeySequence m_frameBackKey = QKeySequence(Qt::Key_Comma);
    QKeySequence m_frameForwardKey = QKeySequence(Qt::Key_Period);
};
