#include "MainWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <clocale>

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

bool wheelModeMatches(const QString& mode, Qt::KeyboardModifiers modifiers) {
    if (mode == QStringLiteral("wheel")) return modifiers == Qt::NoModifier;
    if (mode == QStringLiteral("shift-wheel")) return modifiers == Qt::ShiftModifier;
    if (mode == QStringLiteral("alt-wheel")) return modifiers == Qt::AltModifier;
    if (mode == QStringLiteral("ctrl-wheel")) return modifiers == Qt::ControlModifier;
    return false;
}

void addWheelModes(QComboBox* combo) {
    combo->addItem(QStringLiteral("Mouse wheel"), QStringLiteral("wheel"));
    combo->addItem(QStringLiteral("Shift + wheel"), QStringLiteral("shift-wheel"));
    combo->addItem(QStringLiteral("Alt + wheel"), QStringLiteral("alt-wheel"));
    combo->addItem(QStringLiteral("Ctrl + wheel"), QStringLiteral("ctrl-wheel"));
    combo->addItem(QStringLiteral("Disabled"), QStringLiteral("off"));
}

void selectData(QComboBox* combo, const QVariant& value) {
    const int index = combo->findData(value);
    if (index >= 0) combo->setCurrentIndex(index);
}
}

MainWindow::MainWindow(const QString& mediaPath, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("REX Player"));
    resize(1200, 760);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    loadControlSettings();
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
    m_videoWidget->setAttribute(Qt::WA_AcceptTouchEvents);
    m_videoWidget->setStyleSheet(QStringLiteral("background:#000;"));
    m_videoWidget->setMinimumSize(320, 180);
    m_videoWidget->installEventFilter(this);
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
    m_abLoopLabel = new QLabel(QStringLiteral("A-B: Off"), m_controls);
    m_abLoopLabel->setToolTip(QStringLiteral("A: set loop start, B: set loop end, L: clear loop"));
    row->addWidget(m_abLoopLabel);
    m_hwButton = new QPushButton(QStringLiteral("SW"), m_controls);
    m_hwButton->setFixedWidth(48);
    m_hwButton->setToolTip(QStringLiteral("Software decoding. Click to enable hardware decoding when supported."));
    connect(m_hwButton, &QPushButton::clicked, this, &MainWindow::toggleHardwareDecoding);
    row->addWidget(m_hwButton);
    auto* controlsButton = new QPushButton(QStringLiteral("Controls"), m_controls);
    controlsButton->setToolTip(QStringLiteral("Customize mouse and keyboard controls"));
    connect(controlsButton, &QPushButton::clicked, this, &MainWindow::showControlsDialog);
    row->addWidget(controlsButton);
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
    menu->setToolTip(QStringLiteral("Show video information"));
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
    connect(m_playlist, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { playlistActivated(); });
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

void MainWindow::loadControlSettings() {
    QSettings settings(QStringLiteral("REX Player"), QStringLiteral("REX Player"));
    m_seekWheelMode = settings.value(QStringLiteral("controls/seekWheel"), m_seekWheelMode).toString();
    m_zoomWheelMode = settings.value(QStringLiteral("controls/zoomWheel"), m_zoomWheelMode).toString();
    m_volumeWheelMode = settings.value(QStringLiteral("controls/volumeWheel"), m_volumeWheelMode).toString();
    m_panButton = static_cast<Qt::MouseButton>(settings.value(QStringLiteral("controls/panButton"), static_cast<int>(m_panButton)).toInt());
    m_doubleClickButton = static_cast<Qt::MouseButton>(settings.value(QStringLiteral("controls/doubleClickButton"), static_cast<int>(m_doubleClickButton)).toInt());
    m_loopAKey = QKeySequence(settings.value(QStringLiteral("controls/loopA"), m_loopAKey.toString()).toString());
    m_loopBKey = QKeySequence(settings.value(QStringLiteral("controls/loopB"), m_loopBKey.toString()).toString());
    m_loopClearKey = QKeySequence(settings.value(QStringLiteral("controls/loopClear"), m_loopClearKey.toString()).toString());
    m_zoomInKey = QKeySequence(settings.value(QStringLiteral("controls/zoomIn"), m_zoomInKey.toString()).toString());
    m_zoomOutKey = QKeySequence(settings.value(QStringLiteral("controls/zoomOut"), m_zoomOutKey.toString()).toString());
    m_zoomResetKey = QKeySequence(settings.value(QStringLiteral("controls/zoomReset"), m_zoomResetKey.toString()).toString());
    m_frameBackKey = QKeySequence(settings.value(QStringLiteral("controls/frameBack"), m_frameBackKey.toString()).toString());
    m_frameForwardKey = QKeySequence(settings.value(QStringLiteral("controls/frameForward"), m_frameForwardKey.toString()).toString());
}

void MainWindow::showControlsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Controls"));
    dialog.setModal(true);
    dialog.resize(520, 560);

    auto* mainLayout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* seekWheel = new QComboBox(&dialog);
    addWheelModes(seekWheel);
    selectData(seekWheel, m_seekWheelMode);
    form->addRow(QStringLiteral("Touchpad / wheel → Seek"), seekWheel);

    auto* zoomWheel = new QComboBox(&dialog);
    addWheelModes(zoomWheel);
    selectData(zoomWheel, m_zoomWheelMode);
    form->addRow(QStringLiteral("Wheel → Zoom"), zoomWheel);

    auto* volumeWheel = new QComboBox(&dialog);
    addWheelModes(volumeWheel);
    selectData(volumeWheel, m_volumeWheelMode);
    form->addRow(QStringLiteral("Wheel → Volume"), volumeWheel);

    auto* panButton = new QComboBox(&dialog);
    panButton->addItem(QStringLiteral("Left button"), static_cast<int>(Qt::LeftButton));
    panButton->addItem(QStringLiteral("Middle button"), static_cast<int>(Qt::MiddleButton));
    panButton->addItem(QStringLiteral("Right button"), static_cast<int>(Qt::RightButton));
    panButton->addItem(QStringLiteral("Disabled"), static_cast<int>(Qt::NoButton));
    selectData(panButton, static_cast<int>(m_panButton));
    form->addRow(QStringLiteral("Drag → Pan"), panButton);

    auto* doubleClickButton = new QComboBox(&dialog);
    doubleClickButton->addItem(QStringLiteral("Left button"), static_cast<int>(Qt::LeftButton));
    doubleClickButton->addItem(QStringLiteral("Middle button"), static_cast<int>(Qt::MiddleButton));
    doubleClickButton->addItem(QStringLiteral("Right button"), static_cast<int>(Qt::RightButton));
    doubleClickButton->addItem(QStringLiteral("Disabled"), static_cast<int>(Qt::NoButton));
    selectData(doubleClickButton, static_cast<int>(m_doubleClickButton));
    form->addRow(QStringLiteral("Double-click zones"), doubleClickButton);

    mainLayout->addWidget(new QLabel(QStringLiteral("Mouse / touchpad"), &dialog));
    mainLayout->addLayout(form);
    mainLayout->addWidget(new QLabel(QStringLiteral("Keyboard shortcuts"), &dialog));

    auto* keyForm = new QFormLayout();
    auto* loopA = new QKeySequenceEdit(m_loopAKey, &dialog);
    auto* loopB = new QKeySequenceEdit(m_loopBKey, &dialog);
    auto* loopClear = new QKeySequenceEdit(m_loopClearKey, &dialog);
    auto* zoomIn = new QKeySequenceEdit(m_zoomInKey, &dialog);
    auto* zoomOut = new QKeySequenceEdit(m_zoomOutKey, &dialog);
    auto* zoomReset = new QKeySequenceEdit(m_zoomResetKey, &dialog);
    auto* frameBack = new QKeySequenceEdit(m_frameBackKey, &dialog);
    auto* frameForward = new QKeySequenceEdit(m_frameForwardKey, &dialog);
    const QList<QKeySequenceEdit*> edits = {loopA, loopB, loopClear, zoomIn, zoomOut, zoomReset, frameBack, frameForward};
    for (auto* edit : edits) edit->setClearButtonEnabled(true);
    keyForm->addRow(QStringLiteral("A → Loop start"), loopA);
    keyForm->addRow(QStringLiteral("B → Loop end"), loopB);
    keyForm->addRow(QStringLiteral("L → Clear loop"), loopClear);
    keyForm->addRow(QStringLiteral("+ → Zoom in"), zoomIn);
    keyForm->addRow(QStringLiteral("− → Zoom out"), zoomOut);
    keyForm->addRow(QStringLiteral("Z → Reset zoom / pan"), zoomReset);
    keyForm->addRow(QStringLiteral(", → Previous frame"), frameBack);
    keyForm->addRow(QStringLiteral(". → Next frame"), frameForward);
    mainLayout->addLayout(keyForm);

    auto* note = new QLabel(QStringLiteral("Changes are saved for the next launch. Clear a shortcut to disable it."), &dialog);
    note->setWordWrap(true);
    mainLayout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* reset = buttons->addButton(QStringLiteral("Reset defaults"), QDialogButtonBox::ResetRole);
    mainLayout->addWidget(buttons);

    connect(reset, &QPushButton::clicked, &dialog, [&] {
        selectData(seekWheel, QStringLiteral("wheel"));
        selectData(zoomWheel, QStringLiteral("alt-wheel"));
        selectData(volumeWheel, QStringLiteral("ctrl-wheel"));
        selectData(panButton, static_cast<int>(Qt::MiddleButton));
        selectData(doubleClickButton, static_cast<int>(Qt::LeftButton));
        loopA->setKeySequence(QKeySequence(Qt::Key_A));
        loopB->setKeySequence(QKeySequence(Qt::Key_B));
        loopClear->setKeySequence(QKeySequence(Qt::Key_L));
        zoomIn->setKeySequence(QKeySequence(Qt::Key_Plus));
        zoomOut->setKeySequence(QKeySequence(Qt::Key_Minus));
        zoomReset->setKeySequence(QKeySequence(Qt::Key_Z));
        frameBack->setKeySequence(QKeySequence(Qt::Key_Comma));
        frameForward->setKeySequence(QKeySequence(Qt::Key_Period));
    });

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QStringList wheelModes = {seekWheel->currentData().toString(), zoomWheel->currentData().toString(), volumeWheel->currentData().toString()};
        for (int i = 0; i < wheelModes.size(); ++i) {
            if (wheelModes[i] == QStringLiteral("off")) continue;
            for (int j = i + 1; j < wheelModes.size(); ++j) {
                if (wheelModes[i] == wheelModes[j]) {
                    QMessageBox::warning(&dialog, QStringLiteral("Controls"), QStringLiteral("The same wheel gesture is assigned to more than one action. Please choose different gestures."));
                    return;
                }
            }
        }
        m_seekWheelMode = seekWheel->currentData().toString();
        m_zoomWheelMode = zoomWheel->currentData().toString();
        m_volumeWheelMode = volumeWheel->currentData().toString();
        m_panButton = static_cast<Qt::MouseButton>(panButton->currentData().toInt());
        m_doubleClickButton = static_cast<Qt::MouseButton>(doubleClickButton->currentData().toInt());
        m_loopAKey = loopA->keySequence();
        m_loopBKey = loopB->keySequence();
        m_loopClearKey = loopClear->keySequence();
        m_zoomInKey = zoomIn->keySequence();
        m_zoomOutKey = zoomOut->keySequence();
        m_zoomResetKey = zoomReset->keySequence();
        m_frameBackKey = frameBack->keySequence();
        m_frameForwardKey = frameForward->keySequence();

        QSettings settings(QStringLiteral("REX Player"), QStringLiteral("REX Player"));
        settings.setValue(QStringLiteral("controls/seekWheel"), m_seekWheelMode);
        settings.setValue(QStringLiteral("controls/zoomWheel"), m_zoomWheelMode);
        settings.setValue(QStringLiteral("controls/volumeWheel"), m_volumeWheelMode);
        settings.setValue(QStringLiteral("controls/panButton"), static_cast<int>(m_panButton));
        settings.setValue(QStringLiteral("controls/doubleClickButton"), static_cast<int>(m_doubleClickButton));
        settings.setValue(QStringLiteral("controls/loopA"), m_loopAKey.toString());
        settings.setValue(QStringLiteral("controls/loopB"), m_loopBKey.toString());
        settings.setValue(QStringLiteral("controls/loopClear"), m_loopClearKey.toString());
        settings.setValue(QStringLiteral("controls/zoomIn"), m_zoomInKey.toString());
        settings.setValue(QStringLiteral("controls/zoomOut"), m_zoomOutKey.toString());
        settings.setValue(QStringLiteral("controls/zoomReset"), m_zoomResetKey.toString());
        settings.setValue(QStringLiteral("controls/frameBack"), m_frameBackKey.toString());
        settings.setValue(QStringLiteral("controls/frameForward"), m_frameForwardKey.toString());
        settings.sync();
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

bool MainWindow::initializeMpv() {
    std::setlocale(LC_NUMERIC, "C");
    m_mpv = mpv_create();
    if (!m_mpv) { showError(QStringLiteral("Could not create libmpv instance.")); return false; }
    const QByteArray wid = QByteArray::number(static_cast<qulonglong>(m_videoWidget->winId()));
    if (mpv_set_option_string(m_mpv, "wid", wid.constData()) < 0 ||
        mpv_set_option_string(m_mpv, "terminal", "no") < 0 ||
        mpv_set_option_string(m_mpv, "osc", "no") < 0 ||
        mpv_set_option_string(m_mpv, "keep-open", "yes") < 0 ||
        mpv_set_option_string(m_mpv, "hwdec", "auto") < 0 ||
        mpv_set_option_string(m_mpv, "input-vo-keyboard", "no") < 0 ||
        mpv_set_option_string(m_mpv, "input-cursor-passthrough", "yes") < 0) {
        showError(QStringLiteral("Could not configure libmpv.")); return false;
    }
    if (mpv_initialize(m_mpv) < 0) {
        showError(QStringLiteral("Could not initialize libmpv.")); return false;
    }
    return true;
}

void MainWindow::loadFile(const QString& path) {
    if (!m_mpv || path.isEmpty()) return;
    clearAbLoop();
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
    if (m_playlist && m_currentPlaylistIndex >= 0 && m_currentPlaylistIndex < m_playlist->count()) m_playlist->setCurrentRow(m_currentPlaylistIndex);
}

void MainWindow::command(const char** args) { if (m_mpv) mpv_command_async(m_mpv, 0, args); }
void MainWindow::togglePause() { const char* args[] = {"cycle", "pause", nullptr}; command(args); }
void MainWindow::seekBackward() { const char* args[] = {"seek", "-10", "relative", "exact", nullptr}; command(args); }
void MainWindow::seekForward() { const char* args[] = {"seek", "10", "relative", "exact", nullptr}; command(args); }
void MainWindow::seekTo(int value) { const double duration = getPropertyDouble("duration"); if (duration > 0) setPropertyDouble("time-pos", duration * value / 1000.0); }
void MainWindow::setVolume(int value) { setPropertyDouble("volume", value); }
double MainWindow::getPropertyDouble(const char* name) const { if (!m_mpv) return 0.0; double value = 0.0; return mpv_get_property(m_mpv, name, MPV_FORMAT_DOUBLE, &value) >= 0 ? value : 0.0; }
QString MainWindow::getPropertyString(const char* name) const { if (!m_mpv) return {}; char* value = nullptr; if (mpv_get_property(m_mpv, name, MPV_FORMAT_STRING, &value) < 0 || !value) return {}; const QString result = QString::fromUtf8(value); mpv_free(value); return result; }
void MainWindow::setPropertyDouble(const char* name, double value) { if (m_mpv) mpv_set_property_async(m_mpv, 0, name, MPV_FORMAT_DOUBLE, &value); }
void MainWindow::adjustVideoZoom(double amount) { setPropertyDouble("video-zoom", std::clamp(getPropertyDouble("video-zoom") + amount, -2.0, 3.0)); }
void MainWindow::resetVideoTransform() { setPropertyDouble("video-zoom", 0.0); setPropertyDouble("video-pan-x", 0.0); setPropertyDouble("video-pan-y", 0.0); m_videoPanX = 0.0; m_videoPanY = 0.0; }
void MainWindow::setAbLoopStart() { if (getPropertyDouble("duration") <= 0.0) return; const double position = getPropertyDouble("time-pos"); clearAbLoop(); m_abLoopStart = position; setPropertyDouble("ab-loop-a", position); updateAbLoopLabel(); }
void MainWindow::setAbLoopEnd() { const double position = getPropertyDouble("time-pos"); if (m_abLoopStart < 0.0 || position <= m_abLoopStart) return; m_abLoopEnd = position; setPropertyDouble("ab-loop-a", m_abLoopStart); setPropertyDouble("ab-loop-b", m_abLoopEnd); updateAbLoopLabel(); }
void MainWindow::clearAbLoop() { static char noLoop[] = "no"; char* value = noLoop; if (m_mpv) { mpv_set_property_async(m_mpv, 0, "ab-loop-a", MPV_FORMAT_STRING, &value); mpv_set_property_async(m_mpv, 0, "ab-loop-b", MPV_FORMAT_STRING, &value); } m_abLoopStart = -1.0; m_abLoopEnd = -1.0; updateAbLoopLabel(); }
void MainWindow::updateAbLoopLabel() { if (!m_abLoopLabel) return; if (m_abLoopStart < 0.0) m_abLoopLabel->setText(QStringLiteral("A-B: Off")); else if (m_abLoopEnd < 0.0) m_abLoopLabel->setText(QStringLiteral("A-B: %1 — …").arg(formatTime(m_abLoopStart))); else m_abLoopLabel->setText(QStringLiteral("A-B: %1 — %2").arg(formatTime(m_abLoopStart), formatTime(m_abLoopEnd))); }
void MainWindow::stepFrame(bool forward) { const char* args[] = {forward ? "frame-step" : "frame-back-step", nullptr}; command(args); }
void MainWindow::toggleHardwareDecoding() { if (!m_mpv) return; const char* args[] = {"cycle-values", "hwdec", "auto", "no", nullptr}; command(args); }
void MainWindow::updateHardwareButton() {
    if (!m_hwButton || !m_mpv) return;
    const QString current = getPropertyString("hwdec-current").trimmed().toLower();
    const bool hardwareActive = !current.isEmpty() && current != QStringLiteral("no");
    m_hwButton->setText(hardwareActive ? QStringLiteral("HW") : QStringLiteral("SW"));
    m_hwButton->setToolTip(hardwareActive
        ? QStringLiteral("Hardware decoding active (%1). Click to switch to software decoding.").arg(current)
        : QStringLiteral("Software decoding active. Click to enable hardware decoding when supported."));
}

void MainWindow::showTracksMenu() {
    if (!m_mpv) return;
    mpv_node tracks{};
    if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &tracks) < 0 || tracks.format != MPV_FORMAT_NODE_ARRAY || !tracks.u.list) { mpv_free_node_contents(&tracks); return; }
    auto* menu = new QMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose);
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
                static char noTrack[] = "no";
                char* value = noTrack;
                mpv_set_property_async(m_mpv, 0, property, MPV_FORMAT_STRING, &value);
            } else {
                int64_t value = id;
                mpv_set_property_async(m_mpv, 0, property, MPV_FORMAT_INT64, &value);
            }
        });
    };
    addTrack(audioMenu, QStringLiteral("Auto"), -1, false, "aid");
    mpv_node aid{};
    if (mpv_get_property(m_mpv, "aid", MPV_FORMAT_NODE, &aid) >= 0 && aid.format == MPV_FORMAT_INT64) audioMenu->actions().first()->setChecked(aid.u.int64 < 0);
    mpv_free_node_contents(&aid);
    auto* autoSubtitle = subtitleMenu->addAction(QStringLiteral("Off"));
    autoSubtitle->setCheckable(true);
    mpv_node sid{};
    if (mpv_get_property(m_mpv, "sid", MPV_FORMAT_NODE, &sid) >= 0 && sid.format == MPV_FORMAT_INT64) autoSubtitle->setChecked(sid.u.int64 < 0);
    mpv_free_node_contents(&sid);
    connect(autoSubtitle, &QAction::triggered, this, [this] {
        static char noSubtitle[] = "no";
        char* value = noSubtitle;
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
                int64_t value = id;
                mpv_set_property_async(m_mpv, 0, "sid", MPV_FORMAT_INT64, &value);
            });
            hasSubtitles = true;
        }
    }
    audioMenu->setEnabled(hasAudio);
    subtitleMenu->setEnabled(hasSubtitles || subtitleMenu->actions().size() > 0);
    if (auto* button = qobject_cast<QPushButton*>(sender())) menu->popup(button->mapToGlobal(QPoint(0, button->height())));
    else menu->popup(QCursor::pos());
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
    if (!m_seeking) m_seekSlider->setValue(duration > 0 ? static_cast<int>(std::clamp(pos / duration, 0.0, 1.0) * 1000.0) : 0);
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(pos), formatTime(duration)));
    updatePlayButton(paused != 0);
    updateHardwareButton();
    syncPlaylistSelection();
}

void MainWindow::updatePlayButton(bool paused) { m_playButton->setText(paused ? QStringLiteral("▶") : QStringLiteral("Ⅱ")); }
QString MainWindow::formatTime(double seconds) const { if (!std::isfinite(seconds) || seconds < 0) seconds = 0; const int total = static_cast<int>(seconds); const int h = total / 3600, m = (total % 3600) / 60, s = total % 60; return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m,2,10,QLatin1Char('0')).arg(s,2,10,QLatin1Char('0')) : QStringLiteral("%1:%2").arg(m).arg(s,2,10,QLatin1Char('0')); }
void MainWindow::openFile() { const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open video")); if (!path.isEmpty()) loadFile(path); }
void MainWindow::addFiles() { const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Add media files")); if (paths.isEmpty()) return; for (const QString& path : paths) addToPlaylist(path); if (m_playlist && m_playlist->currentItem()) playlistActivated(); }
void MainWindow::addFolder() { const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Add media folder")); if (path.isEmpty() || !m_playlist) return; QDir dir(path); const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase); for (const QFileInfo& info : files) if (isMediaFile(info)) addToPlaylist(info.absoluteFilePath()); if (m_playlist->currentItem()) playlistActivated(); }
void MainWindow::clearPlaylist() { if (m_playlist) m_playlist->clear(); m_currentPlaylistIndex = -1; }
void MainWindow::playlistActivated() { if (m_playlist && m_playlist->currentItem()) playPlaylistIndex(m_playlist->currentRow()); }
void MainWindow::playPrevious() { if (!m_playlist || m_playlist->count() == 0) return; int index = m_currentPlaylistIndex >= 0 ? m_currentPlaylistIndex : m_playlist->currentRow(); if (index > 0) playPlaylistIndex(index - 1); }
void MainWindow::playNext() { if (!m_playlist || m_playlist->count() == 0) return; int index = m_currentPlaylistIndex >= 0 ? m_currentPlaylistIndex : m_playlist->currentRow(); if (index + 1 < m_playlist->count()) playPlaylistIndex(index + 1); }
void MainWindow::dragEnterEvent(QDragEnterEvent* event) { if (event->mimeData()->hasUrls()) event->acceptProposedAction(); }
void MainWindow::dropEvent(QDropEvent* event) { const auto urls = event->mimeData()->urls(); for (const auto& url : urls) if (url.isLocalFile()) addToPlaylist(url.toLocalFile()); if (m_playlist && m_playlist->currentItem()) playlistActivated(); if (!urls.isEmpty()) event->acceptProposedAction(); }

bool MainWindow::keyMatches(QKeyEvent* event, const QKeySequence& sequence) const {
    if (sequence.isEmpty()) return false;
    const int key = event->key();
    if (key == Qt::Key_unknown) return false;
    const int combined = key | static_cast<int>(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    return QKeySequence(combined).matches(sequence) == QKeySequence::ExactMatch;
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (keyMatches(event, m_loopAKey)) { setAbLoopStart(); event->accept(); return; }
    if (keyMatches(event, m_loopBKey)) { setAbLoopEnd(); event->accept(); return; }
    if (keyMatches(event, m_loopClearKey)) { clearAbLoop(); event->accept(); return; }
    if (keyMatches(event, m_zoomInKey)) { adjustVideoZoom(0.1); event->accept(); return; }
    if (keyMatches(event, m_zoomOutKey)) { adjustVideoZoom(-0.1); event->accept(); return; }
    if (keyMatches(event, m_zoomResetKey)) { resetVideoTransform(); event->accept(); return; }
    if (keyMatches(event, m_frameBackKey)) { stepFrame(false); event->accept(); return; }
    if (keyMatches(event, m_frameForwardKey)) { stepFrame(true); event->accept(); return; }

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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_videoWidget) return QMainWindow::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonDblClick) {
        const auto* e = static_cast<QMouseEvent*>(event);
        if (m_doubleClickZones && m_doubleClickButton != Qt::NoButton && e->button() == m_doubleClickButton) {
            const qreal x = e->position().x();
            const qreal width = m_videoWidget->width();
            if (x < width / 3.0) seekBackward();
            else if (x > width * 2.0 / 3.0) seekForward();
            else togglePause();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        const auto* e = static_cast<QMouseEvent*>(event);
        if (m_panButton != Qt::NoButton && e->button() == m_panButton) {
            m_panningVideo = true;
            m_panStart = e->position();
            m_videoPanX = getPropertyDouble("video-pan-x");
            m_videoPanY = getPropertyDouble("video-pan-y");
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove && m_panningVideo) {
        const auto* e = static_cast<QMouseEvent*>(event);
        const QPointF delta = e->position() - m_panStart;
        const double width = std::max(1, m_videoWidget->width());
        const double height = std::max(1, m_videoWidget->height());
        m_videoPanX = std::clamp(m_videoPanX + delta.x() / width, -1.0, 1.0);
        m_videoPanY = std::clamp(m_videoPanY + delta.y() / height, -1.0, 1.0);
        setPropertyDouble("video-pan-x", m_videoPanX);
        setPropertyDouble("video-pan-y", m_videoPanY);
        m_panStart = e->position();
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        const auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() == m_panButton && m_panningVideo) {
            m_panningVideo = false;
            return true;
        }
    }

    if (event->type() == QEvent::Wheel) {
        const auto* e = static_cast<QWheelEvent*>(event);
        const int delta = !e->angleDelta().isNull() ? e->angleDelta().y() : e->pixelDelta().y();
        if (delta == 0) return false;
        const Qt::KeyboardModifiers modifiers = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (wheelModeMatches(m_zoomWheelMode, modifiers)) {
            adjustVideoZoom(delta > 0 ? 0.1 : -0.1);
            return true;
        }
        if (wheelModeMatches(m_volumeWheelMode, modifiers)) {
            m_volumeSlider->setValue(std::clamp(m_volumeSlider->value() + (delta > 0 ? 5 : -5), 0, 100));
            return true;
        }
        if (wheelModeMatches(m_seekWheelMode, modifiers)) {
            const char* args[] = {"seek", delta > 0 ? "5" : "-5", "relative", "exact", nullptr};
            command(args);
            return true;
        }
        return false;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleControls() {
    if (!m_mpv) return;
    const char* args[] = {
        "show-text",
        "File: ${filename}\nFormat: ${file-format}\nSize: ${width} × ${height}   FPS: ${container-fps}\nVideo: ${video-codec} (${video-format})\nVideo bitrate: ${video-bitrate}\nAudio: ${audio-codec-name}   ${audio-params/samplerate} Hz   ${audio-params/channel-count} ch\nAudio bitrate: ${audio-bitrate}\nDuration: ${duration}   Bitrate: ${bitrate}\nFile size: ${file-size}\nPixel format: ${video-params/pixelformat}\nColor: ${video-params/colormatrix} / ${video-params/primaries} / ${video-params/transfer}\nHW decode: ${hwdec-current}",
        "10000",
        nullptr
    };
    command(args);
}

void MainWindow::setControlsVisible(bool visible) { if (m_controls) m_controls->setVisible(visible); }
void MainWindow::togglePlaylist() { if (m_playlistDock) m_playlistDock->setVisible(!m_playlistDock->isVisible()); }
void MainWindow::closeEvent(QCloseEvent* event) { if (m_mpv) { const char* args[] = {"quit", nullptr}; mpv_command(m_mpv, args); } QMainWindow::closeEvent(event); }
void MainWindow::showError(const QString& message) { setWindowTitle(QStringLiteral("REX Player — %1").arg(message)); }
