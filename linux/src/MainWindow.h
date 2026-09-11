#pragma once

#include <QMainWindow>
#include <QTimer>

class QLabel;
class QPushButton;
class QSlider;
class QWidget;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
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
    void togglePause();
    void seekBackward();
    void seekForward();
    void seekTo(int value);
    void setVolume(int value);
    void updatePlaybackUi();

private:
    bool initializeMpv();
    void buildUi();
    void loadFile(const QString& path);
    void command(const char** args);
    double getPropertyDouble(const char* name) const;
    void setPropertyDouble(const char* name, double value);
    void updatePlayButton(bool paused);
    void showError(const QString& message);
    QString formatTime(double seconds) const;

    mpv_handle* m_mpv = nullptr;
    QTimer m_eventTimer;
    QTimer m_uiTimer;
    QWidget* m_videoWidget = nullptr;
    QSlider* m_seekSlider = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    bool m_seeking = false;
};
