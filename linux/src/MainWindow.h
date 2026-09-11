#pragma once

#include <QMainWindow>
#include <QTimer>

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

private slots:
    void pumpMpvEvents();

private:
    bool initializeMpv();
    void loadFile(const QString& path);
    void showError(const QString& message);

    mpv_handle* m_mpv = nullptr;
    QTimer m_eventTimer;
};
