#pragma once

#include <QMainWindow>
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
class QDockWidget;
class QKeyEvent;

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
    void playPrevious();
    void playNext();
    void showTracksMenu();

private:
    bool initializeMpv();
    void buildUi();
    void loadFile(const QString& path);
    void addToPlaylist(const QString& path);
    void playPlaylistIndex(int index);
    void syncPlaylistSelection();
    void command(const char** args);
    double getPropertyDouble(const char* name) const;
    void setPropertyDouble(const char* name, double value);
    void updatePlayButton(bool paused);
    void showError(const QString& message);
    QString formatTime(double seconds) const;
    void setControlsVisible(bool visible);

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
    QLabel* m_titleLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_previousButton = nullptr;
    QPushButton* m_nextButton = nullptr;
    bool m_seeking = false;
    int m_currentPlaylistIndex = -1;
};
