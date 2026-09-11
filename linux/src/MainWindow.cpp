#include "MainWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include <mpv/client.h>

namespace {
const QStringList kMediaExtensions = {
    QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
    QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("m4v"),
    QStringLiteral("ts"), QStringLiteral("m2ts"), QStringLiteral("flv"),
    QStringLiteral("wmv"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
    QStringLiteral("3gp"), QStringLiteral("ogv"), QStringLiteral("mp3"),
    QStringLiteral("flac"), QStringLiteral("m4a"), QStringLiteral("aac"),
    QStringLiteral("opus"), QStringLiteral("wav")
};

bool isMediaFile(const QFileInfo& info) {
    return info.isFile() && kMediaExtensions.contains(info.suffix().toLower());
}

const mpv_node* mapValue(const mpv_node_list* map, const char* key) {
    if (!map || !map->keys || !map->values) return nullptr;
    for (int i = 0; i < map->num; ++i) {
        if (map->keys[i] && qstrcmp(map->keys[i], key) == 0) return &map->values[i];
    }
    return nullptr;
}

QString nodeString(const mpv_node* node) {
    if (!node) return {};
    if (node->format == MPV_FORMAT_STRING && node->u.string) return QString::fromUtf8(node->u.string);
    return {};
}

int nodeInt(const mpv_node* node, int fallback = -1) {
    if (!node) return fallback;
    if (node->format == MPV_FORMAT_INT64) return static_cast<int>(node->u.int64);
    if (node->format == MPV_FORMAT_DOUBLE) return static_cast<int>(node->u.double_);
    return fallback;
}

bool nodeFlag(const mpv_node* node) {
    return node && node->format == MPV_FORMAT_FLAG && node->u.flag != 0;
}
}

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
    root->setObjectName(QStringLiteral("root"));
    root->setStyleSheet(QStringLiteral("QWidget#root{background:#000;}"));
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_videoWidget = new QWidget(root);
    m_videoWidget->setAttribute(Qt::WA_NativeWindow);
    m_videoWidget->setFocusPolicy(Qt::StrongFocus);
    m_videoWidget->setStyleSheet(QStringLiteral("background:#000;"));
    m_videoWidget->setMinimumSize(320, 180);
    layout->addWidget(m_videoWidget, 1);

    m_controls = new QWidget(root);
    m_controls->setObjectName(QStringLiteral("controls"));
    m_controls->setStyleSheet(QStringLiteral(
        "QWidget#controls{background:#171717;color:#eee;}"
        "QPushButton{background:transparent;color:#eee;border:0;padding:8px 10px;}"
        "QPushButton:hover{background:#303030;border-radius:6px;}"
        "QSlider::groove:horizontal{height:4px;background:#555;border-radius:2px;}"
        "QSlider::handle:horizontal{width:12px;margin:-4px 0;border-radius:6px;background:#ddd;}"
        "QLabel{color:#ddd;}"));
    auto* controlsLayout = new QVBoxLayout(m_controls);
    controlsLayout->setContentsMargins(12, 8, 12, 10);
    controlsLayout->setSpacing(6);

    m_seekSlider = new QSlider(Qt::Horizontal, m_controls);
    m_seekSlider->setRange(0, 1000);
    m_seekSlider->setTracking(false);
    connect(m_seekSlider, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this] {
        m_seeking = false;
        seekTo(m_seekSlider->value());
    });
    controlsLayout->addWidget(m_seekSlider);

    auto* row = new QHBoxLayout();
    auto* open = new QPushButton(QStringLiteral("Open"), m_controls);
    connect(open, &QPushButton::clicked, this, &MainWindow::openFile);
    row->addWidget(open);

    m_previousButton = new QPushButton(QStringLiteral("⏮"), m_controls);
    m_previousButton->setToolTip(QStringLiteral("Previous item"));
    m_previousButton->setFixedWidth(48);
    connect(m_previousButton, &QPushButton::clicked, this, &MainWindow::playPrevious);
    row->addWidget(m_previousButton);

    auto* back = new QPushButton(QStringLiteral("−10s"), m_controls);
    connect(back, &QPushButton::clicked, this, &MainWindow::seekBackward);
    row->addWidget(back);

    m_playButton = new QPushButton(QStringLiteral("▶"), m_controls);
    m_playButton->setFixedWidth(52);
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::togglePause);
    row->addWidget(m_playButton);

    auto* forward = new QPushButton(QStringLiteral("+10s"), m_controls);
    connect(forward, &QPushButton::clicked, this, &MainWindow::seekForward);
    row->addWidget(forward);

    m_nextButton = new QPushButton(QStringLiteral("⏭"), m_controls);
    m_nextButton->setToolTip(QStringLiteral("Next item"));
    m_nextButton->setFixedWidth(48);
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::playNext);
    row->addWidget(m_nextButton);

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), m_controls);
    row->addWidget(m_timeLabel);
    row->addStretch();

    row->addWidget(new QLabel(QStringLiteral("Volume"), m_controls));
    m_volumeSlider = new QSlider(Qt::Horizontal, m_controls);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(130);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &MainWindow::setVolume);
    row->addWidget(m_volumeSlider);

    auto* tracks = new QPushButton(QStringLiteral("Tracks"), m_controls);
    tracks->setToolTip(QStringLiteral("Select audio and subtitle tracks"));
    connect(tracks, &QPushButton::clicked, this, &MainWindow::showTracksMenu);
    row->addWidget(tracks);

    auto* playlistButton = new QPushButton(QStringLiteral("Playlist"), m_controls);
    playlistButton->setToolTip(QStringLiteral("Show or hide playlist"));
    connect(playlistButton, &QPushButton::clicked, this, &MainWindow::togglePlaylist);
    row->addWidget(playlistButton);

    auto* menu = new QPushButton(QStringLiteral("☰"), m_controls);
    menu->setToolTip(QStringLiteral("Show or hide controls"));
    menu->setFixedWidth(42);
    connect(menu, &QPushButton::clicked, this, &MainWindow::toggleControls);
    row->addWidget(menu);

    controlsLayout->addLayout(row);
    m_titleLabel = new QLabel(QStringLiteral("No media loaded"), m_controls);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
    controlsLayout->addWidget(m_titleLabel);
    layout->addWidget(m_controls);
    setCentralWidget(root);

    m_playlistDock = new QDockWidget(QStringLiteral("Playlist"), this);
    m_playlistDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_playlistDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_playlistDock->setMinimumWidth(280);

    auto* playlistPanel = new QWidget(m_playlistDock);
    playlistPanel->setStyleSheet(QStringLiteral(
        "QWidget{background:#171717;color:#eee;}"
        "QPushButton{background:#242424;color:#eee;border:0;padding:7px;}"
        "QPushButton:hover{background:#303030;}"
        "QListWidget{background:#101010;color:#eee;border:0;}"
        "QListWidget::item{padding:7px;}"
        "QListWidget::item:selected{background:#3a3a3a;}"));
    auto* playlistLayout = new QVBoxLayout(playlistPanel);
    playlistLayout->setContentsMargins(8, 8, 8, 8);
    playlistLayout->setSpacing(6);

    m_playlist = new QListWidget(playlistPanel);
    m_playlist->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_playlist, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { playlistActivated(); });
    playlistLayout->addWidget(m_playlist, 1);

    auto* playlistButtons = new QHBoxLayout();
    auto* add = new QPushButton(QStringLiteral("+ Files"), playlistPanel);
    connect(add, &QPushButton::clicked, this, &MainWindow::addFiles);
    playlistButtons->addWidget(add);
    auto* folder = new QPushButton(QStringLiteral("+ Folder"), playlistPanel);
    connect(folder, &QPushButton::clicked, this, &MainWindow::addFolder);
    playlistButtons->addWidget(folder);
    auto* clear = new QPushButton(QStringLiteral("Clear"), playlistPanel);
    connect(clear, &QPushButton::clicked, this, &MainWindow::clearPlaylist);
    playlistButtons->addWidget(clear);
    playlistLayout->addLayout(playlistButtons);

    m_playlistDock->setWidget(playlistPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_playlistDock);
    m_playlistDock->hide();
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
    addToPlaylist(absolute);
    const int index = m_playlist ? m_playlist->currentRow() : -1;
    if (index >= 0) playPlaylistIndex(index);
    else {
        const QByteArray encoded = absolute.toUtf8();
        const char* args[] = {"loadfile", encoded.constData(), "replace", nullptr};
        mpv_command_async(m_mpv, 0, args);
    }
}

void MainWindow::addToPlaylist(const QString& path) {
    if (!m_playlist || path.isEmpty()) return;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_playlist->count(); ++i) {
        if (m_playlist->item(i)->data(Qt::UserRole).toString() == absolute) {
            m_playlist->setCurrentRow(i);
            return;
        }
    }
    auto* item = new QListWidgetItem(QFileInfo(absolute).fileName(), m_playlist);
    item->setToolTip(absolute);
    item->setData(Qt::UserRole, absolute);
    m_playlist->setCurrentItem(item);
}

void MainWindow::playPlaylistIndex(int index) {
    if (!m_playlist || index < 0 || index >= m_playlist->count()) return;
    auto* item = m_playlist->item(index);
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    m_currentPlaylistIndex = index;
    m_playlist->setCurrentRow(index);
    const QByteArray encoded = path.toUtf8();
    const char* args[] = {"loadfile", encoded.constData(), "replace", nullptr};
    if (m_mpv) mpv_command_async(m_mpv, 0, args);
    m_titleLabel->setText(QFileInfo(path).fileName());
    setWindowTitle(QStringLiteral("%1 — REX Player").arg(QFileInfo(path).fileName()));
}

void MainWindow::syncPlaylistSelection() {
    if (!m_playlist) return;
    if (m_currentPlaylistIndex >= 0 && m_currentPlaylistIndex < m_playlist->count())
        m_playlist->setCurrentRow(m_currentPlaylistIndex);
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

void MainWindow::showTracksMenu() {
    if (!m_mpv) return;

    mpv_node tracks{};
    if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &tracks) < 0 ||
        tracks.format != MPV_FORMAT_NODE_ARRAY || !tracks.u.list) {
        mpv_free_node_contents(&tracks);
        return;
    }

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* audioMenu = menu->addMenu(QStringLiteral("Audio"));
    auto* subtitleMenu = menu->addMenu(QStringLiteral("Subtitles"));

    const mpv_node_list* list = tracks.u.list;
    bool hasAudio = false;
    bool hasSubtitles = false;

    auto addTrack = [this](QMenu* target, const QString& label, int id, bool selected, const char* property) {
        auto* action = target->addAction(label);
        action->setCheckable(true);
        action->setChecked(selected);
        connect(action, &QAction::triggered, this, [this, id, property] {
            if (id < 0) {
                const char* value = "no";
                mpv_set_property_async(m_mpv, 0, property, MPV_FORMAT_STRING, &value);
            } else {
                const int64_t value = id;
                mpv_set_property_async(m_mpv, 0, property, MPV_FORMAT_INT64, &value);
            }
        });
    };

    addTrack(audioMenu, QStringLiteral("Auto"), -1, false, "aid");
    {
        const mpv_node* aidNode = nullptr;
        mpv_node aid{};
        if (mpv_get_property(m_mpv, "aid", MPV_FORMAT_NODE, &aid) >= 0) {
            aidNode = &aid;
            if (aidNode->format == MPV_FORMAT_INT64) {
                const int currentAid = static_cast<int>(aidNode->u.int64);
                audioMenu->actions().first()->setChecked(currentAid < 0);
            }
        }
        mpv_free_node_contents(&aid);
    }

    auto* autoSubtitle = subtitleMenu->addAction(QStringLiteral("Off"));
    autoSubtitle->setCheckable(true);
    {
        mpv_node sid{};
        if (mpv_get_property(m_mpv, "sid", MPV_FORMAT_NODE, &sid) >= 0 && sid.format == MPV_FORMAT_INT64)
            autoSubtitle->setChecked(sid.u.int64 < 0);
        mpv_free_node_contents(&sid);
    }
    connect(autoSubtitle, &QAction::triggered, this, [this] {
        const char* value = "no";
        mpv_set_property_async(m_mpv, 0, "sid", MPV_FORMAT_STRING, &value);
    });

    for (int i = 0; i < list->num; ++i) {
        const mpv_node& track = list->values[i];
        if (track.format != MPV_FORMAT_NODE_MAP || !track.u.list) continue;

        const QString type = nodeString(mapValue(track.u.list, "type"));
        const int id = nodeInt(mapValue(track.u.list, "id"));
        if (id < 0) continue;
        const QString lang = nodeString(mapValue(track.u.list, "lang"));
        const QString title = nodeString(mapValue(track.u.list, "title"));
        const QString external = nodeString(mapValue(track.u.list, "external-filename"));
        const bool selected = nodeFlag(mapValue(track.u.list, "selected"));

        QString label = title;
        if (label.isEmpty()) label = lang;
        if (label.isEmpty() && !external.isEmpty()) label = QFileInfo(external).fileName();
        if (label.isEmpty()) label = QStringLiteral("Track %1").arg(id);
        if (!lang.isEmpty() && title != lang) label += QStringLiteral(" (%1)").arg(lang);

        if (type == QStringLiteral("audio")) {
            addTrack(audioMenu, label, id, selected, "aid");
            hasAudio = true;
        } else if (type == QStringLiteral("sub")) {
            auto* action = subtitleMenu->addAction(label);
            action->setCheckable(true);
            action->setChecked(selected);
            connect(action, &QAction::triggered, this, [this, id] {
                const int64_t value = id;
                mpv_set_property_async(m_mpv, 0, "sid", MPV_FORMAT_INT64, &value);
            });
            hasSubtitles = true;
        }
    }

    audioMenu->setEnabled(hasAudio);
    subtitleMenu->setEnabled(hasSubtitles || subtitleMenu->actions().size() > 0);

    if (auto* button = qobject_cast<QPushButton*>(sender()))
        menu->popup(button->mapToGlobal(QPoint(0, button->height())));
    else
        menu->popup(QCursor::pos());

    mpv_free_node_contents(&tracks);
}

void MainWindow::pumpMpvEvents() {
    if (!m_mpv) return;
    while (true) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE) break;
        if (event->event_id == MPV_EVENT_END_FILE) {
            auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end && end->reason == MPV_END_FILE_REASON_EOF) playNext();
        } else if (event->event_id == MPV_EVENT_SHUTDOWN) {
            close();
            break;
        }
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
    syncPlaylistSelection();
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

void MainWindow::addFiles() {
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Add media files"));
    if (paths.isEmpty()) return;
    for (const QString& path : paths) addToPlaylist(path);
    if (m_playlist && m_playlist->currentItem()) playlistActivated();
}

void MainWindow::addFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Add media folder"));
    if (path.isEmpty() || !m_playlist) return;
    QDir dir(path);
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& info : files) if (isMediaFile(info)) addToPlaylist(info.absoluteFilePath());
    if (m_playlist->currentItem()) playlistActivated();
}

void MainWindow::clearPlaylist() {
    if (m_playlist) m_playlist->clear();
    m_currentPlaylistIndex = -1;
}

void MainWindow::playlistActivated() {
    if (!m_playlist || !m_playlist->currentItem()) return;
    playPlaylistIndex(m_playlist->currentRow());
}

void MainWindow::playPrevious() {
    if (!m_playlist || m_playlist->count() == 0) return;
    int index = m_currentPlaylistIndex >= 0 ? m_currentPlaylistIndex : m_playlist->currentRow();
    if (index > 0) playPlaylistIndex(index - 1);
}

void MainWindow::playNext() {
    if (!m_playlist || m_playlist->count() == 0) return;
    int index = m_currentPlaylistIndex >= 0 ? m_currentPlaylistIndex : m_playlist->currentRow();
    if (index + 1 < m_playlist->count()) playPlaylistIndex(index + 1);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    for (const auto& url : urls) {
        if (url.isLocalFile()) addToPlaylist(url.toLocalFile());
    }
    if (m_playlist && m_playlist->currentItem()) playlistActivated();
    if (!urls.isEmpty()) event->acceptProposedAction();
}
void MainWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Space: togglePause(); break;
    case Qt::Key_Left: seekBackward(); break;
    case Qt::Key_Right: seekForward(); break;
    case Qt::Key_Up: playPrevious(); break;
    case Qt::Key_Down: playNext(); break;
    case Qt::Key_F11: isFullScreen() ? showNormal() : showFullScreen(); break;
    case Qt::Key_Escape: if (isFullScreen()) showNormal(); break;
    default: QMainWindow::keyPressEvent(event); break;
    }
}
void MainWindow::toggleControls() { setControlsVisible(m_controls && !m_controls->isVisible()); }
void MainWindow::setControlsVisible(bool visible) { if (m_controls) m_controls->setVisible(visible); }
void MainWindow::togglePlaylist() { if (m_playlistDock) m_playlistDock->setVisible(!m_playlistDock->isVisible()); }
void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_mpv) { const char* args[] = {"quit", nullptr}; mpv_command(m_mpv, args); }
    QMainWindow::closeEvent(event);
}
void MainWindow::showError(const QString& message) { setWindowTitle(QStringLiteral("REX Player — %1").arg(message)); }
