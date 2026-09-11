#include "MainWindow.h"

#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <mpv/client.h>

MainWindow::MainWindow(const QString& mediaPath, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("REX Player"));
    resize(1100, 700);
    setAcceptDrops(true);
    setAttribute(Qt::WA_NativeWindow);
    setCentralWidget(new QWidget(this));
    centralWidget()->setAttribute(Qt::WA_NativeWindow);
    centralWidget()->setStyleSheet(QStringLiteral("background: black;"));

    show();

    if (!initializeMpv()) {
        return;
    }

    m_eventTimer.setInterval(10);
    connect(&m_eventTimer, &QTimer::timeout, this, &MainWindow::pumpMpvEvents);
    m_eventTimer.start();

    if (!mediaPath.isEmpty()) {
        loadFile(mediaPath);
    }
}

MainWindow::~MainWindow() {
    m_eventTimer.stop();
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

bool MainWindow::initializeMpv() {
    m_mpv = mpv_create();
    if (!m_mpv) {
        showError(QStringLiteral("Could not create libmpv instance."));
        return false;
    }

    const QByteArray wid = QByteArray::number(static_cast<qulonglong>(centralWidget()->winId()));
    if (mpv_set_option_string(m_mpv, "wid", wid.constData()) < 0 ||
        mpv_set_option_string(m_mpv, "terminal", "no") < 0 ||
        mpv_set_option_string(m_mpv, "osc", "no") < 0) {
        showError(QStringLiteral("Could not configure libmpv."));
        return false;
    }

    if (mpv_initialize(m_mpv) < 0) {
        showError(QStringLiteral("Could not initialize libmpv."));
        return false;
    }

    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    return true;
}

void MainWindow::loadFile(const QString& path) {
    if (!m_mpv || path.isEmpty()) {
        return;
    }

    const QByteArray encoded = QFileInfo(path).absoluteFilePath().toUtf8();
    const char* args[] = {"loadfile", encoded.constData(), "replace", nullptr};
    mpv_command_async(m_mpv, 0, args);
}

void MainWindow::pumpMpvEvents() {
    if (!m_mpv) {
        return;
    }

    while (true) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            break;
        }
        if (event->event_id == MPV_EVENT_SHUTDOWN) {
            close();
            break;
        }
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile()) {
        loadFile(urls.first().toLocalFile());
        event->acceptProposedAction();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_mpv) {
        const char* args[] = {"quit", nullptr};
        mpv_command(m_mpv, args);
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::showError(const QString& message) {
    setWindowTitle(QStringLiteral("REX Player — %1").arg(message));
}
