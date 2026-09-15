#include "MainWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QDateTime>
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
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QStyle>
#include <QTextStream>
#include <QStyleOptionSlider>
#include <QUrl>
#include <QSettings>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
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
    m_seekSlider->installEventFilter(this);
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
    m_seekBackButton = new QPushButton(QStringLiteral("−10s"), m_controls);
    m_seekBackButton->setFixedWidth(60);
    m_seekBackButton->setToolTip(QStringLiteral("Seek backward 10 seconds"));
    connect(m_seekBackButton, &QPushButton::clicked, this, [this] {
        const char* args[] = {"seek", "-10", "relative", "exact", nullptr};
        command(args);
    });
    row->addWidget(m_seekBackButton);
    m_playButton = new QPushButton(QStringLiteral("▶"), m_controls);
    m_playButton->setFixedWidth(52);
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::togglePause);
    row->addWidget(m_playButton);
    m_seekForwardButton = new QPushButton(QStringLiteral("+10s"), m_controls);
    m_seekForwardButton->setFixedWidth(60);
    m_seekForwardButton->setToolTip(QStringLiteral("Seek forward 10 seconds"));
    connect(m_seekForwardButton, &QPushButton::clicked, this, [this] {
        const char* args[] = {"seek", "10", "relative", "exact", nullptr};
        command(args);
    });
    row->addWidget(m_seekForwardButton);
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
    m_cutAbButton = new QPushButton(QStringLiteral("Cut AB"), m_controls);
    m_cutAbButton->setFixedWidth(62);
    m_cutAbButton->setToolTip(QStringLiteral("Cut the current A-B selection with FFmpeg without re-encoding"));
    connect(m_cutAbButton, &QPushButton::clicked, this, &MainWindow::cutAbSelection);
    row->addWidget(m_cutAbButton);
    m_logButton = new QPushButton(QStringLiteral("Save Log"), m_controls);
    m_logButton->setFixedWidth(72);
    m_logButton->setToolTip(QStringLiteral("Save a diagnostic log report"));
    connect(m_logButton, &QPushButton::clicked, this, &MainWindow::saveLogReport);
    row->addWidget(m_logButton);
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
    updateSeekButtonLabels();
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
    m_seekDurationMinutes = std::clamp(settings.value(QStringLiteral("controls/seekDurationMinutes"), m_seekDurationMinutes).toInt(), 1, 120);
    m_seekBackwardKey = QKeySequence(settings.value(QStringLiteral("controls/seekBackward"), m_seekBackwardKey.toString()).toString());
    m_seekForwardKey = QKeySequence(settings.value(QStringLiteral("controls/seekForward"), m_seekForwardKey.toString()).toString());
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
    dialog.resize(520, 650);

    auto* mainLayout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* seekDuration = new QSpinBox(&dialog);
    seekDuration->setRange(1, 120);
    seekDuration->setSingleStep(1);
    seekDuration->setSuffix(QStringLiteral(" min"));
    seekDuration->setValue(m_seekDurationMinutes);
    form->addRow(QStringLiteral("Seek duration"), seekDuration);

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
    auto* seekBack = new QKeySequenceEdit(m_seekBackwardKey, &dialog);
    auto* seekForward = new QKeySequenceEdit(m_seekForwardKey, &dialog);
    auto* loopA = new QKeySequenceEdit(m_loopAKey, &dialog);
    auto* loopB = new QKeySequenceEdit(m_loopBKey, &dialog);
    auto* loopClear = new QKeySequenceEdit(m_loopClearKey, &dialog);
    auto* zoomIn = new QKeySequenceEdit(m_zoomInKey, &dialog);
    auto* zoomOut = new QKeySequenceEdit(m_zoomOutKey, &dialog);
    auto* zoomReset = new QKeySequenceEdit(m_zoomResetKey, &dialog);
    auto* frameBack = new QKeySequenceEdit(m_frameBackKey, &dialog);
    auto* frameForward = new QKeySequenceEdit(m_frameForwardKey, &dialog);
    const QList<QKeySequenceEdit*> edits = {seekBack, seekForward, loopA, loopB, loopClear, zoomIn, zoomOut, zoomReset, frameBack, frameForward};
    for (auto* edit : edits) edit->setClearButtonEnabled(true);
    keyForm->addRow(QStringLiteral("Left Arrow → Seek backward"), seekBack);
    keyForm->addRow(QStringLiteral("Right Arrow → Seek forward"), seekForward);
    keyForm->addRow(QStringLiteral("A → Loop start"), loopA);
    keyForm->addRow(QStringLiteral("B → Loop end"), loopB);
    keyForm->addRow(QStringLiteral("L → Clear loop"), loopClear);
    keyForm->addRow(QStringLiteral("+ → Zoom in"), zoomIn);
    keyForm->addRow(QStringLiteral("− → Zoom out"), zoomOut);
    keyForm->addRow(QStringLiteral("Z → Reset zoom / pan"), zoomReset);
    keyForm->addRow(QStringLiteral(", → Previous frame"), frameBack);
    keyForm->addRow(QStringLiteral(". → Next frame"), frameForward);
    mainLayout->addLayout(keyForm);

    auto* note = new QLabel(QStringLiteral("Seek duration applies to the arrow keys, wheel seek and double-click seek zones. The −10s and +10s buttons always seek exactly 10 seconds. Changes are saved for the next launch. Clear a shortcut to disable it."), &dialog);
    note->setWordWrap(true);
    mainLayout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* reset = buttons->addButton(QStringLiteral("Reset defaults"), QDialogButtonBox::ResetRole);
    mainLayout->addWidget(buttons);

    connect(reset, &QPushButton::clicked, &dialog, [&] {
        seekDuration->setValue(1);
        selectData(seekWheel, QStringLiteral("wheel"));
        selectData(zoomWheel, QStringLiteral("alt-wheel"));
        selectData(volumeWheel, QStringLiteral("ctrl-wheel"));
        selectData(panButton, static_cast<int>(Qt::MiddleButton));
        selectData(doubleClickButton, static_cast<int>(Qt::LeftButton));
        seekBack->setKeySequence(QKeySequence(Qt::Key_Left));
        seekForward->setKeySequence(QKeySequence(Qt::Key_Right));
        loopA->setKeySequence(QKeySequence(Qt::Key_A));
        loopB->setKeySequence(QKeySequence(Qt::Key_B));
        loopClear->setKeySequence(QKeySequence(Qt::Key_L));
        zoomIn->setKeySequence(QKeySequence(Qt::SHIFT | Qt::Key_Equal));
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
        m_seekDurationMinutes = seekDuration->value();
        m_seekWheelMode = seekWheel->currentData().toString();
        m_zoomWheelMode = zoomWheel->currentData().toString();
        m_volumeWheelMode = volumeWheel->currentData().toString();
        m_panButton = static_cast<Qt::MouseButton>(panButton->currentData().toInt());
        m_doubleClickButton = static_cast<Qt::MouseButton>(doubleClickButton->currentData().toInt());
        m_seekBackwardKey = seekBack->keySequence();
        m_seekForwardKey = seekForward->keySequence();
        m_loopAKey = loopA->keySequence();
        m_loopBKey = loopB->keySequence();
        m_loopClearKey = loopClear->keySequence();
        m_zoomInKey = zoomIn->keySequence();
        m_zoomOutKey = zoomOut->keySequence();
        m_zoomResetKey = zoomReset->keySequence();
        m_frameBackKey = frameBack->keySequence();
        m_frameForwardKey = frameForward->keySequence();

        QSettings settings(QStringLiteral("REX Player"), QStringLiteral("REX Player"));
        settings.setValue(QStringLiteral("controls/seekDurationMinutes"), m_seekDurationMinutes);
        settings.setValue(QStringLiteral("controls/seekWheel"), m_seekWheelMode);
        settings.setValue(QStringLiteral("controls/zoomWheel"), m_zoomWheelMode);
        settings.setValue(QStringLiteral("controls/volumeWheel"), m_volumeWheelMode);
        settings.setValue(QStringLiteral("controls/panButton"), static_cast<int>(m_panButton));
        settings.setValue(QStringLiteral("controls/doubleClickButton"), static_cast<int>(m_doubleClickButton));
        settings.setValue(QStringLiteral("controls/seekBackward"), m_seekBackwardKey.toString());
        settings.setValue(QStringLiteral("controls/seekForward"), m_seekForwardKey.toString());
        settings.setValue(QStringLiteral("controls/loopA"), m_loopAKey.toString());
        settings.setValue(QStringLiteral("controls/loopB"), m_loopBKey.toString());
        settings.setValue(QStringLiteral("controls/loopClear"), m_loopClearKey.toString());
        settings.setValue(QStringLiteral("controls/zoomIn"), m_zoomInKey.toString());
        settings.setValue(QStringLiteral("controls/zoomOut"), m_zoomOutKey.toString());
        settings.setValue(QStringLiteral("controls/zoomReset"), m_zoomResetKey.toString());
        settings.setValue(QStringLiteral("controls/frameBack"), m_frameBackKey.toString());
        settings.setValue(QStringLiteral("controls/frameForward"), m_frameForwardKey.toString());
        settings.sync();
        updateSeekButtonLabels();
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
void MainWindow::seekBackward() { const QByteArray seconds = QByteArray::number(-m_seekDurationMinutes * 60); const char* args[] = {"seek", seconds.constData(), "relative", "exact", nullptr}; command(args); }
void MainWindow::seekForward() { const QByteArray seconds = QByteArray::number(m_seekDurationMinutes * 60); const char* args[] = {"seek", seconds.constData(), "relative", "exact", nullptr}; command(args); }
void MainWindow::seekTo(int value) { const double duration = getPropertyDouble("duration"); if (duration > 0) setPropertyDouble("time-pos", duration * value / 1000.0); }
void MainWindow::setVolume(int value) { setPropertyDouble("volume", value); }
double MainWindow::getPropertyDouble(const char* name) const { if (!m_mpv) return 0.0; double value = 0.0; return mpv_get_property(m_mpv, name, MPV_FORMAT_DOUBLE, &value) >= 0 ? value : 0.0; }
QString MainWindow::getPropertyString(const char* name) const { if (!m_mpv) return {}; char* value = nullptr; if (mpv_get_property(m_mpv, name, MPV_FORMAT_STRING, &value) < 0 || !value) return {}; const QString result = QString::fromUtf8(value); mpv_free(value); return result; }
void MainWindow::setPropertyDouble(const char* name, double value) { if (m_mpv) mpv_set_property_async(m_mpv, 0, name, MPV_FORMAT_DOUBLE, &value); }
void MainWindow::updateSeekButtonLabels() { if (!m_seekBackButton || !m_seekForwardButton) return; m_seekBackButton->setText(QStringLiteral("−10s")); m_seekForwardButton->setText(QStringLiteral("+10s")); }
void MainWindow::adjustVideoZoom(double amount) { setPropertyDouble("video-zoom", std::clamp(getPropertyDouble("video-zoom") + amount, -2.0, 3.0)); }
void MainWindow::resetVideoTransform() { setPropertyDouble("video-zoom", 0.0); setPropertyDouble("video-pan-x", 0.0); setPropertyDouble("video-pan-y", 0.0); m_videoPanX = 0.0; m_videoPanY = 0.0; }
void MainWindow::setAbLoopStart() { if (getPropertyDouble("duration") <= 0.0) return; const double position = getPropertyDouble("time-pos"); clearAbLoop(); m_abLoopStart = position; setPropertyDouble("ab-loop-a", position); updateAbLoopLabel(); }
void MainWindow::setAbLoopEnd() { const double position = getPropertyDouble("time-pos"); if (m_abLoopStart < 0.0 || position <= m_abLoopStart) return; m_abLoopEnd = position; setPropertyDouble("ab-loop-a", m_abLoopStart); setPropertyDouble("ab-loop-b", m_abLoopEnd); updateAbLoopLabel(); }
void MainWindow::clearAbLoop() { static char noLoop[] = "no"; char* value = noLoop; if (m_mpv) { mpv_set_property_async(m_mpv, 0, "ab-loop-a", MPV_FORMAT_STRING, &value); mpv_set_property_async(m_mpv, 0, "ab-loop-b", MPV_FORMAT_STRING, &value); } m_abLoopStart = -1.0; m_abLoopEnd = -1.0; updateAbLoopLabel(); }
void MainWindow::updateAbLoopLabel() { if (!m_abLoopLabel) return; if (m_abLoopStart < 0.0) m_abLoopLabel->setText(QStringLiteral("A-B: Off")); else if (m_abLoopEnd < 0.0) m_abLoopLabel->setText(QStringLiteral("A-B: %1 — …").arg(formatTime(m_abLoopStart))); else m_abLoopLabel->setText(QStringLiteral("A-B: %1 — %2").arg(formatTime(m_abLoopStart), formatTime(m_abLoopEnd))); }
void MainWindow::stepFrame(bool forward) { const char* args[] = {forward ? "frame-step" : "frame-back-step", nullptr}; command(args); }
void MainWindow::toggleHardwareDecoding() { if (!m_mpv) return; const char* args[] = {"cycle-values", "hwdec", "auto", "no", nullptr}; command(args); }

void MainWindow::cutAbSelection() {
    if (!m_mpv) return;
    if (m_abLoopStart < 0.0 || m_abLoopEnd <= m_abLoopStart) {
        QMessageBox::information(this, QStringLiteral("Cut A-B"), QStringLiteral("Set both A and B points first."));
        return;
    }
    if (m_cutProcess && m_cutProcess->state() != QProcess::NotRunning) {
        QMessageBox::information(this, QStringLiteral("Cut A-B"), QStringLiteral("An A-B cut is already in progress."));
        return;
    }

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("FFmpeg not found"),
                             QStringLiteral("FFmpeg is required for A-B cutting. Install the ffmpeg package and try again."));
        return;
    }

    QString inputPath = getPropertyString("path").trimmed();
    const QUrl inputUrl(inputPath);
    if (inputUrl.isLocalFile()) inputPath = inputUrl.toLocalFile();
    const QFileInfo inputInfo(inputPath);
    if (!inputInfo.isFile()) {
        QMessageBox::warning(this, QStringLiteral("Cut A-B"), QStringLiteral("The current media is not a local file."));
        return;
    }

    const double duration = m_abLoopEnd - m_abLoopStart;
    const QString start = QString::number(m_abLoopStart, 'f', 6);
    const QString length = QString::number(duration, 'f', 6);
    const QString suffix = inputInfo.suffix();
    const QString defaultName = inputInfo.dir().filePath(
        inputInfo.completeBaseName() + QStringLiteral("_AB_cut") +
        (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix));
    const QString filter = suffix.isEmpty()
        ? QStringLiteral("All files (*)")
        : QStringLiteral("%1 (*.%1);;All files (*)").arg(suffix);
    const QString outputPath = QFileDialog::getSaveFileName(this, QStringLiteral("Save A-B cut"), defaultName, filter);
    if (outputPath.isEmpty()) return;

    const QFileInfo outputInfo(outputPath);
    if (outputInfo.absoluteFilePath() == inputInfo.absoluteFilePath()) {
        QMessageBox::warning(this, QStringLiteral("Cut A-B"), QStringLiteral("The output file must be different from the input file."));
        return;
    }
    if (outputInfo.exists()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Overwrite file?"),
            QStringLiteral("%1 already exists. Replace it?").arg(outputInfo.fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    m_cutOutputPath = outputPath;
    m_cutProcess = new QProcess(this);
    m_cutProcess->setProcessChannelMode(QProcess::SeparateChannels);
    m_cutAbButton->setEnabled(false);

    connect(m_cutProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString error = QString::fromLocal8Bit(m_cutProcess->readAllStandardError()).trimmed();
        const QString output = m_cutOutputPath;
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0 && QFileInfo::exists(output);
        if (success) {
            QMessageBox::information(
                this, QStringLiteral("A-B cut complete"),
                QStringLiteral("Saved:\n%1\n\nStreams were copied without re-encoding. Because this is stream-copy cutting, the start may align to a nearby keyframe.").arg(output));
        } else {
            if (QFileInfo::exists(output)) QFile::remove(output);
            const QString detail = error.isEmpty() ? QStringLiteral("FFmpeg exited with code %1.").arg(exitCode) : error;
            QMessageBox::warning(this, QStringLiteral("A-B cut failed"), detail);
        }
        m_cutAbButton->setEnabled(true);
        m_cutProcess->deleteLater();
        m_cutProcess = nullptr;
        m_cutOutputPath.clear();
    });

    const QStringList args = {
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-ss"), start,
        QStringLiteral("-i"), inputPath,
        QStringLiteral("-t"), length,
        QStringLiteral("-map"), QStringLiteral("0"),
        QStringLiteral("-c"), QStringLiteral("copy"),
        QStringLiteral("-avoid_negative_ts"), QStringLiteral("make_zero"),
        QStringLiteral("-y"), outputPath
    };
    m_cutProcess->start(ffmpeg, args);
}

void MainWindow::saveLogReport() {
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString defaultName = QDir::home().filePath(QStringLiteral("REX_Player_Log_%1.txt").arg(timestamp));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save REX Player log report"), defaultName, QStringLiteral("Text files (*.txt);;All files (*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Save Log"), QStringLiteral("Could not write the log report:\n%1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
    out << "REX Player - Diagnostic Log Report\n";
    out << "=================================\n\n";
    out << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    out << "Application: " << QCoreApplication::applicationName() << " " << QCoreApplication::applicationVersion() << "\n";
    out << "Qt: " << QT_VERSION_STR << "\n";
    out << "OS: " << QSysInfo::prettyProductName() << "\n";
    out << "Kernel: " << QSysInfo::kernelType() << " " << QSysInfo::kernelVersion() << "\n";
    out << "CPU architecture: " << QSysInfo::currentCpuArchitecture() << "\n";
    out << "Build ABI: " << QSysInfo::buildAbi() << "\n";
    out << "Host name: " << QSysInfo::machineHostName() << "\n";
    out << "LC_NUMERIC: " << qgetenv("LC_NUMERIC") << "\n";
    out << "QT_QPA_PLATFORM: " << qgetenv("QT_QPA_PLATFORM") << "\n";
    out << "WAYLAND_DISPLAY: " << qgetenv("WAYLAND_DISPLAY") << "\n";
    out << "DISPLAY: " << qgetenv("DISPLAY") << "\n\n";

    auto writeString = [&out, this](const char* key, const char* label) {
        const QString value = getPropertyString(key);
        if (!value.isEmpty()) out << label << ": " << value << "\n";
    };
    auto writeDouble = [&out, this](const char* key, const char* label) {
        const double value = getPropertyDouble(key);
        if (std::isfinite(value)) out << label << ": " << QString::number(value, 'g', 12) << "\n";
    };

    out << "Playback / Media\n----------------\n";
    writeString("path", "Path");
    writeString("filename", "Filename");
    writeString("media-title", "Media title");
    writeString("file-format", "Container");
    writeDouble("duration", "Duration (s)");
    writeDouble("bitrate", "Overall bitrate");
    writeString("video-codec", "Video codec");
    writeString("video-format", "Video format");
    writeDouble("video-params/w", "Video width");
    writeDouble("video-params/h", "Video height");
    writeDouble("container-fps", "Container FPS");
    writeDouble("video-bitrate", "Video bitrate");
    writeString("video-params/pixelformat", "Pixel format");
    writeString("video-params/chroma-location", "Chroma location");
    writeString("video-params/colormatrix", "Color matrix");
    writeString("video-params/primaries", "Color primaries");
    writeString("video-params/transfer", "Color transfer");
    writeDouble("video-params/rotate", "Rotation");
    writeString("audio-codec", "Audio codec");
    writeString("audio-format", "Audio format");
    writeDouble("audio-samplerate", "Audio sample rate");
    writeString("audio-channels", "Audio channels");
    writeString("audio-channel-layout", "Audio channel layout");
    writeDouble("audio-bitrate", "Audio bitrate");
    writeString("hwdec", "HW decoder setting");
    writeString("hwdec-current", "Active HW decoder");
    writeString("vo", "Video output");
    writeString("gpu-api", "GPU API");
    writeDouble("time-pos", "Position (s)");
    writeDouble("speed", "Speed");
    writeDouble("video-zoom", "Video zoom");
    writeDouble("video-pan-x", "Video pan X");
    writeDouble("video-pan-y", "Video pan Y");
    writeString("pause", "Paused");

    out << "\nTracks\n------\n";
    mpv_node tracks{};
    if (m_mpv && mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &tracks) >= 0 && tracks.format == MPV_FORMAT_NODE_ARRAY && tracks.u.list) {
        for (int i = 0; i < tracks.u.list->num; ++i) {
            const mpv_node* track = &tracks.u.list->values[i];
            out << "Track " << (i + 1)
                << ": type=" << nodeString(mapValue(track->u.list, "type"))
                << ", id=" << nodeInt(mapValue(track->u.list, "id"))
                << ", lang=" << nodeString(mapValue(track->u.list, "lang"))
                << ", title=" << nodeString(mapValue(track->u.list, "title"))
                << ", codec=" << nodeString(mapValue(track->u.list, "codec"))
                << ", external=" << nodeString(mapValue(track->u.list, "external-filename"))
                << ", selected=" << (nodeFlag(mapValue(track->u.list, "selected")) ? "yes" : "no") << "\n";
        }
        mpv_free_node_contents(&tracks);
    } else {
        out << "Unable to read track-list.\n";
    }

    out << "\nA-B / Controls\n--------------\n";
    out << "A-B start: " << m_abLoopStart << "\n";
    out << "A-B end: " << m_abLoopEnd << "\n";
    out << "Seek duration (minutes): " << m_seekDurationMinutes << "\n";
    out << "Seek wheel: " << m_seekWheelMode << "\n";
    out << "Zoom wheel: " << m_zoomWheelMode << "\n";
    out << "Volume wheel: " << m_volumeWheelMode << "\n";
    out << "Pan button: " << static_cast<int>(m_panButton) << "\n";
    out << "Double-click button: " << static_cast<int>(m_doubleClickButton) << "\n";
    out << "Double-click zones: " << (m_doubleClickZones ? "enabled" : "disabled") << "\n";
    out << "Seek backward shortcut: " << m_seekBackwardKey.toString() << "\n";
    out << "Seek forward shortcut: " << m_seekForwardKey.toString() << "\n";
    out << "Loop A shortcut: " << m_loopAKey.toString() << "\n";
    out << "Loop B shortcut: " << m_loopBKey.toString() << "\n";
    out << "Loop clear shortcut: " << m_loopClearKey.toString() << "\n";
    out << "Zoom in shortcut: " << m_zoomInKey.toString() << "\n";
    out << "Zoom out shortcut: " << m_zoomOutKey.toString() << "\n";
    out << "Zoom reset shortcut: " << m_zoomResetKey.toString() << "\n";
    out << "Frame back shortcut: " << m_frameBackKey.toString() << "\n";
    out << "Frame forward shortcut: " << m_frameForwardKey.toString() << "\n";

    out << "\nPlaylist\n--------\n";
    if (m_playlist) {
        out << "Count: " << m_playlist->count() << "\n";
        out << "Current index: " << m_currentPlaylistIndex << "\n";
        for (int i = 0; i < m_playlist->count(); ++i)
            out << (i + 1) << ": " << m_playlist->item(i)->data(Qt::UserRole).toString() << "\n";
    }

    out << "\nEnd of report\n";
    file.close();
    QMessageBox::information(this, QStringLiteral("Log saved"), QStringLiteral("Diagnostic report saved to:\n%1").arg(path));
}

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
    if (keyMatches(event, m_seekBackwardKey)) { seekBackward(); event->accept(); return; }
    if (keyMatches(event, m_seekForwardKey)) { seekForward(); event->accept(); return; }
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
    case Qt::Key_Up: playPrevious(); break;
    case Qt::Key_Down: playNext(); break;
    case Qt::Key_F11: isFullScreen() ? showNormal() : showFullScreen(); break;
    case Qt::Key_Escape: if (isFullScreen()) showNormal(); break;
    default: QMainWindow::keyPressEvent(event); break;
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_seekSlider && event->type() == QEvent::MouseButtonPress) {
        const auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() == Qt::LeftButton) {
            QStyleOptionSlider opt;
            opt.initFrom(m_seekSlider);
            opt.orientation = Qt::Horizontal;
            opt.minimum = m_seekSlider->minimum();
            opt.maximum = m_seekSlider->maximum();
            opt.sliderPosition = m_seekSlider->sliderPosition();
            const QRect handle = m_seekSlider->style()->subControlRect(
                QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, m_seekSlider);
            if (!handle.contains(e->position().toPoint())) {
                const QRect groove = m_seekSlider->style()->subControlRect(
                    QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, m_seekSlider);
                const int span = std::max(1, groove.width());
                const int x = static_cast<int>(e->position().x());
                const int position = std::clamp(x - groove.left(), 0, span);
                const int value = QStyle::sliderValueFromPosition(
                    opt.minimum, opt.maximum, position, span, opt.upsideDown);
                m_seekSlider->setValue(value);
                m_seeking = false;
                seekTo(value);
                return true;
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }

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
            if (delta > 0) seekForward();
            else seekBackward();
            return true;
        }
        return false;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleControls() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Video Information"));
    dialog.setModal(true);
    dialog.resize(720, 700);

    auto* mainLayout = new QVBoxLayout(&dialog);
    auto* scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    auto* content = new QWidget(scrollArea);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto valueOrDash = [this](const char* property) {
        const QString value = getPropertyString(property).trimmed();
        return value.isEmpty() ? QStringLiteral("—") : value;
    };
    auto numberOrDash = [this](const char* property, int decimals = 2) {
        const double value = getPropertyDouble(property);
        return value > 0.0 ? QString::number(value, 'f', decimals) : QStringLiteral("—");
    };
    auto addSection = [&layout](const QString& title) {
        auto* label = new QLabel(title, layout->parentWidget());
        label->setStyleSheet(QStringLiteral("font-weight:600; font-size:14px; margin-top:6px;"));
        layout->addWidget(label);
    };
    auto addRow = [&layout](const QString& name, const QString& value) {
        auto* row = new QFormLayout();
        row->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto* label = new QLabel(value, layout->parentWidget());
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        row->addRow(name, label);
        layout->addLayout(row);
    };

    if (!m_mpv || getPropertyString("filename").isEmpty()) {
        layout->addWidget(new QLabel(QStringLiteral("No media is currently loaded."), content));
    } else {
        addSection(QStringLiteral("File"));
        addRow(QStringLiteral("File name"), valueOrDash("filename"));
        addRow(QStringLiteral("Title"), valueOrDash("media-title"));
        addRow(QStringLiteral("Path"), valueOrDash("path"));
        addRow(QStringLiteral("Container"), valueOrDash("file-format"));
        addRow(QStringLiteral("File size"), valueOrDash("file-size"));
        addRow(QStringLiteral("Duration"), formatTime(getPropertyDouble("duration")));
        addRow(QStringLiteral("Overall bitrate"), valueOrDash("bitrate"));

        addSection(QStringLiteral("Video"));
        addRow(QStringLiteral("Codec"), valueOrDash("video-codec"));
        addRow(QStringLiteral("Format"), valueOrDash("video-format"));
        addRow(QStringLiteral("Resolution"), QStringLiteral("%1 × %2").arg(numberOrDash("width", 0), numberOrDash("height", 0)));
        addRow(QStringLiteral("FPS"), valueOrDash("container-fps"));
        addRow(QStringLiteral("Bitrate"), valueOrDash("video-bitrate"));
        addRow(QStringLiteral("Pixel format"), valueOrDash("video-params/pixelformat"));
        addRow(QStringLiteral("Chroma location"), valueOrDash("video-params/chroma-location"));
        addRow(QStringLiteral("Color matrix"), valueOrDash("video-params/colormatrix"));
        addRow(QStringLiteral("Color primaries"), valueOrDash("video-params/primaries"));
        addRow(QStringLiteral("Transfer"), valueOrDash("video-params/transfer"));
        addRow(QStringLiteral("Rotation"), valueOrDash("video-params/rotate"));
        addRow(QStringLiteral("Aspect ratio"), valueOrDash("video-params/aspect"));
        addRow(QStringLiteral("HW decoder"), valueOrDash("hwdec-current"));

        addSection(QStringLiteral("Audio"));
        addRow(QStringLiteral("Codec"), valueOrDash("audio-codec-name"));
        addRow(QStringLiteral("Format"), valueOrDash("audio-format"));
        addRow(QStringLiteral("Sample rate"), valueOrDash("audio-params/samplerate"));
        addRow(QStringLiteral("Channels"), valueOrDash("audio-params/channel-count"));
        addRow(QStringLiteral("Channel layout"), valueOrDash("audio-params/channel-layout"));
        addRow(QStringLiteral("Bitrate"), valueOrDash("audio-bitrate"));

        addSection(QStringLiteral("Tracks"));
        mpv_node tracks{};
        bool haveTracks = false;
        if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &tracks) >= 0 &&
            tracks.format == MPV_FORMAT_NODE_ARRAY && tracks.u.list) {
            for (int i = 0; i < tracks.u.list->num; ++i) {
                const mpv_node& track = tracks.u.list->values[i];
                if (track.format != MPV_FORMAT_NODE_MAP || !track.u.list) continue;
                const QString type = nodeString(mapValue(track.u.list, "type"));
                const int id = nodeInt(mapValue(track.u.list, "id"));
                if (id < 0) continue;
                QString label = nodeString(mapValue(track.u.list, "title"));
                const QString lang = nodeString(mapValue(track.u.list, "lang"));
                const QString codec = nodeString(mapValue(track.u.list, "codec"));
                const QString external = nodeString(mapValue(track.u.list, "external-filename"));
                const bool selected = nodeFlag(mapValue(track.u.list, "selected"));
                if (label.isEmpty()) label = lang;
                if (label.isEmpty()) label = external.isEmpty() ? QStringLiteral("Track") : QFileInfo(external).fileName();
                if (label.isEmpty()) label = QStringLiteral("Track");
                QString details = QStringLiteral("#%1 — %2").arg(id).arg(label);
                if (!lang.isEmpty() && label != lang) details += QStringLiteral(" [%1]").arg(lang);
                if (!codec.isEmpty()) details += QStringLiteral(" • %1").arg(codec);
                if (!external.isEmpty()) details += QStringLiteral(" • %1").arg(QFileInfo(external).fileName());
                if (selected) details += QStringLiteral("  ✓ Active");
                const QString section = type == QStringLiteral("video") ? QStringLiteral("Video track")
                    : type == QStringLiteral("audio") ? QStringLiteral("Audio track")
                    : type == QStringLiteral("sub") ? QStringLiteral("Subtitle track")
                    : QStringLiteral("Other track");
                addRow(section, details);
                haveTracks = true;
            }
        }
        mpv_free_node_contents(&tracks);
        if (!haveTracks) addRow(QStringLiteral("Available"), QStringLiteral("No track information available."));

        addSection(QStringLiteral("Playback / Output"));
        addRow(QStringLiteral("Position"), QStringLiteral("%1 / %2").arg(formatTime(getPropertyDouble("time-pos")), formatTime(getPropertyDouble("duration"))));
        addRow(QStringLiteral("Speed"), numberOrDash("speed"));
        addRow(QStringLiteral("Pause"), valueOrDash("pause"));
        addRow(QStringLiteral("Video output"), valueOrDash("vo"));
        addRow(QStringLiteral("GPU API"), valueOrDash("gpu-api"));
        addRow(QStringLiteral("Hardware decoding"), valueOrDash("hwdec-current"));
    }

    scrollArea->setWidget(content);
    mainLayout->addWidget(scrollArea, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    mainLayout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::setControlsVisible(bool visible) { if (m_controls) m_controls->setVisible(visible); }
void MainWindow::togglePlaylist() { if (m_playlistDock) m_playlistDock->setVisible(!m_playlistDock->isVisible()); }
void MainWindow::closeEvent(QCloseEvent* event) { if (m_mpv) { const char* args[] = {"quit", nullptr}; mpv_command(m_mpv, args); } QMainWindow::closeEvent(event); }
void MainWindow::showError(const QString& message) { setWindowTitle(QStringLiteral("REX Player — %1").arg(message)); }
