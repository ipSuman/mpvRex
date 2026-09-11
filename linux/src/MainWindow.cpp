#include "MainWindow.h"

#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include <mpv/client.h>

MainWindow::MainWindow(const QString& mediaPath, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("REX Player"));
    resize(1200, 760);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    buildUi();

    if (!initializeMpv()) return;

    m_eventTimer.setInterval(10);
    connect(&m_eventTimer, &QTimer::timeout, this, &MainWindow::pumpMpvEvents);
    m_eventTimer.start();

    m_uiTimer.setInterval(250);
    connect(&m_uiTimer, &QTimer::timeout, this, &MainWindow::updatePlaybackUi);
    m_uiTimer.start();

    if (!mediaPath.isEmpty()) loadFile(mediaPath);
}

MainWindow::~MainWindow() {
    m_eventTimer.stop();
    m_uiTimer.stop();
    if (m_mpv) mpv_terminate_destroy(m_mpv);
}

void MainWindow::buildUi() {
    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_videoWidget = new QWidget(root);
    m_videoWidget->setAttribute(Qt::WA_NativeWindow);
    m_videoWidget->setStyleSheet(QStringLiteral("background:#000;"));
    m_videoWidget->setMinimumSize(320, 180);
    layout->addWidget(m_videoWidget, 1);

    auto* controls = new QWidget(root);
    controls->setObjectName(QStringLiteral("controls"));
    controls->setStyleSheet(QStringLiteral(
        "QWidget#controls{background:#171717;color:#eee;}"
        "QPushButton{background:transparent;color:#eee;border:0;padding:8px 10px;}"
        "QPushButton:hover{background:#303030;border-radius:6px;}"
        "QSlider::groove:horizontal{height:4px;background:#555;border-radius:2px;}"
        "QSlider::handle:horizontal{width:12px;margin:-4px 0;border-radius:6px;background:#ddd;}"
        "QLabel{color:#ddd;}"));
    auto* controlsLayout = new QVBoxLayout(controls);
    controlsLayout->setContentsMargins(12, 8, 12, 10);
    controlsLayout->setSpacing(6);

    m_seekSlider = new QSlider(Qt::Horizontal, controls);
    m_seekSlider->setRange(0, 1000);
    connect(m_seekSlider, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this] {
        m_seeking = false;
        seekTo(m_seekSlider->value());
    });
    controlsLayout->addWidget(m_seekSlider);

    auto* row = new QHBoxLayout();
    auto* open = new QPushButton(QStringLiteral("Open"), controls);
    connect(open, &QPushButton::clicked, this, &MainWindow::openFile);
    row->addWidget(open);

    auto* back = new QPushButton(QStringLiteral("−10s"), controls);
    connect(back, &QPushButton::clicked, this, &MainWindow::seekBackward);
    row->addWidget(back);

    m_playButton = new QPushButton(QStringLiteral("▶"), controls);
    m_playButton->setFixedWidth(52);
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::togglePause);
    row->addWidget(m_playButton);

    auto* forward = new QPushButton(QStringLiteral("+10s"), controls);
    connect(forward, &QPushButton::clicked, this, &MainWindow::seekForward);
    row->addWidget(forward);

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), controls);
    row->addWidget(m_timeLabel);
    row->addStretch();

    row->addWidget(new QLabel(QStringLiteral("Volume"), controls));
    m_volumeSlider = new QSlider(Qt::Horizontal, controls);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(130);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &MainWindow::setVolume);
    row->addWidget(m_volumeSlider);

    controlsLayout->addLayout(row);
    m_titleLabel = new QLabel(QStringLiteral("No media loaded"), controls);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
    controlsLayout->addWidget(m_titleLabel);
    layout->addWidget(controls);
    setCentralWidget(root);
}

bool MainWindow::initializeMpv() {
    m_mpv = mpv_create();
    if (!m_mpv) { showError(QStringLiteral("Could not create libmpv instance.")); return false; }

    const QByteArray wid = QByteArray::number(static_cast<qulonglong>(m_videoWidget->winId()));
    if (mpv_set_option_string(m_mpv, "wid", wid.constData()) < 0 ||
        mpv_set_option_string(m_mpv, "terminal", "no") < 0 ||
        mpv_set_option_string(m_mpv, "osc", "no") < 0 ||
        mpv_set_option_string(m_mpv, "keep-open", "yes") < 0) {
        showError(QStringLiteral("Could not configure libmpv.")); return false;
    }
    if (mpv_initialize(m_mpv) < 0) {
        showError(QStringLiteral("Could not initialize libmpv.")); return false;
    }
    return true;
}

void MainWindow::loadFile(const QString& path) {
    if (!m_mpv || path.isEmpty()) return;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const QByteArray encoded = absolute.toUtf8();
    const char* args[] = {"loadfile", encoded.constData(), "replace", nullptr};
    mpv_command_async(m_mpv, 0, args);
    m_titleLabel->setText(QFileInfo(absolute).fileName());
    setWindowTitle(QStringLiteral("%1 — REX Player").arg(QFileInfo(absolute).fileName()));
}

void MainWindow::command(const char** args) {
    if (m_mpv) mpv_command_async(m_mpv, 0, args);
}

void MainWindow::togglePause() {
    const char* args[] = {"cycle", "pause", nullptr};
    command(args);
}
void MainWindow::seekBackward() {
    const char* args[] = {"seek", "-10", "relative", "exact", nullptr};
    command(args);
}
void MainWindow::seekForward() {
    const char* args[] = {"seek", "10", "relative", "exact", nullptr};
    command(args);
}
void MainWindow::seekTo(int value) {
    const double duration = getPropertyDouble("duration");
    if (duration > 0) setPropertyDouble("time-pos", duration * value / 1000.0);
}
void MainWindow::setVolume(int value) { setPropertyDouble("volume", value); }

double MainWindow::getPropertyDouble(const char* name) const {
    if (!m_mpv) return 0.0;
    double value = 0.0;
    return mpv_get_property(m_mpv, name, MPV_FORMAT_DOUBLE, &value) >= 0 ? value : 0.0;
}
void MainWindow::setPropertyDouble(const char* name, double value) {
    if (m_mpv) mpv_set_property_async(m_mpv, 0, name, MPV_FORMAT_DOUBLE, &value);
}

void MainWindow::pumpMpvEvents() {
    if (!m_mpv) return;
    while (true) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE) break;
        if (event->event_id == MPV_EVENT_SHUTDOWN) { close(); break; }
    }
}

void MainWindow::updatePlaybackUi() {
    if (!m_mpv) return;
    const double pos = getPropertyDouble("time-pos");
    const double duration = getPropertyDouble("duration");
    int paused = 0;
    if (mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &paused) < 0) paused = 0;

    if (!m_seeking) {
        const int value = duration > 0 ? static_cast<int>(std::clamp(pos / duration, 0.0, 1.0) * 1000.0) : 0;
        m_seekSlider->setValue(value);
    }
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(pos), formatTime(duration)));
    updatePlayButton(paused != 0);
}

void MainWindow::updatePlayButton(bool paused) { m_playButton->setText(paused ? QStringLiteral("▶") : QStringLiteral("Ⅱ")); }

QString MainWindow::formatTime(double seconds) const {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const int total = static_cast<int>(seconds);
    const int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
                 : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

void MainWindow::openFile() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open video"));
    if (!path.isEmpty()) loadFile(path);
}
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile()) {
        loadFile(urls.first().toLocalFile());
        event->acceptProposedAction();
    }
}
void MainWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Space: togglePause(); break;
    case Qt::Key_Left: seekBackward(); break;
    case Qt::Key_Right: seekForward(); break;
    case Qt::Key_F11: isFullScreen() ? showNormal() : showFullScreen(); break;
    case Qt::Key_Escape: if (isFullScreen()) showNormal(); break;
    default: QMainWindow::keyPressEvent(event); break;
    }
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_mpv) { const char* args[] = {"quit", nullptr}; mpv_command(m_mpv, args); }
    QMainWindow::closeEvent(event);
}
void MainWindow::showError(const QString& message) { setWindowTitle(QStringLiteral("REX Player — %1").arg(message)); }
