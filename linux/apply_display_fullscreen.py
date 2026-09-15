from pathlib import Path

h = Path('linux/src/MainWindow.h')
s = h.read_text()

if 'void toggleFullscreen();' not in s:
    marker = '    void setControlsVisible(bool visible);\n'
    if marker not in s:
        raise SystemExit('fullscreen declaration anchor not found')
    s = s.replace(marker, marker + '    void toggleFullscreen();\n    void showDisplayDialog();\n', 1)

if 'int m_saturation = 0;' not in s:
    marker = '    QKeySequence m_frameForwardKey = QKeySequence(Qt::Key_Period);\n'
    if marker not in s:
        raise SystemExit('display member anchor not found')
    s = s.replace(marker, marker + '''    int m_saturation = 0;
    int m_brightness = 0;
    int m_contrast = 0;
    QTimer m_fullscreenHideTimer;
    bool m_playlistWasVisibleBeforeFullscreen = false;
''', 1)
h.write_text(s)

cpp = Path('linux/src/MainWindow.cpp')
s = cpp.read_text()

if 'display/saturation' not in s:
    old = '    m_frameForwardKey = QKeySequence(settings.value(QStringLiteral("controls/frameForward"), m_frameForwardKey.toString()).toString());\n}'
    new = '''    m_frameForwardKey = QKeySequence(settings.value(QStringLiteral("controls/frameForward"), m_frameForwardKey.toString()).toString());
    m_saturation = std::clamp(settings.value(QStringLiteral("display/saturation"), m_saturation).toInt(), -100, 100);
    m_brightness = std::clamp(settings.value(QStringLiteral("display/brightness"), m_brightness).toInt(), -100, 100);
    m_contrast = std::clamp(settings.value(QStringLiteral("display/contrast"), m_contrast).toInt(), -100, 100);
}'''
    if old not in s:
        raise SystemExit('control settings anchor not found')
    s = s.replace(old, new, 1)

if 'm_fullscreenHideTimer.setSingleShot' not in s:
    old = '    buildUi();\n\n    if (!initializeMpv()) return;'
    new = '''    buildUi();

    m_fullscreenHideTimer.setSingleShot(true);
    m_fullscreenHideTimer.setInterval(2500);
    connect(&m_fullscreenHideTimer, &QTimer::timeout, this, [this] {
        if (isFullScreen()) setControlsVisible(false);
    });

    if (!initializeMpv()) return;'''
    if old not in s:
        raise SystemExit('constructor anchor not found')
    s = s.replace(old, new, 1)

if 'setPropertyDouble("saturation", m_saturation);' not in s:
    old = '''    if (mpv_initialize(m_mpv) < 0) {
        showError(QStringLiteral("Could not initialize libmpv.")); return false;
    }
    return true;'''
    new = '''    if (mpv_initialize(m_mpv) < 0) {
        showError(QStringLiteral("Could not initialize libmpv.")); return false;
    }
    setPropertyDouble("saturation", m_saturation);
    setPropertyDouble("brightness", m_brightness);
    setPropertyDouble("contrast", m_contrast);
    return true;'''
    if old not in s:
        raise SystemExit('mpv initialization anchor not found')
    s = s.replace(old, new, 1)

if 'QStringLiteral("Display")' not in s:
    old = '''    connect(controlsButton, &QPushButton::clicked, this, &MainWindow::showControlsDialog);
    row->addWidget(controlsButton);
    row->addStretch();'''
    new = '''    connect(controlsButton, &QPushButton::clicked, this, &MainWindow::showControlsDialog);
    row->addWidget(controlsButton);
    auto* displayButton = new QPushButton(QStringLiteral("Display"), m_controls);
    displayButton->setToolTip(QStringLiteral("Adjust brightness, contrast and saturation"));
    connect(displayButton, &QPushButton::clicked, this, &MainWindow::showDisplayDialog);
    row->addWidget(displayButton);
    row->addStretch();'''
    if old not in s:
        raise SystemExit('display button anchor not found')
    s = s.replace(old, new, 1)

if 'writeDouble("saturation", "Saturation")' not in s:
    old = '        writeDouble("video-zoom", "Video zoom");\n'
    new = '''        writeDouble("video-zoom", "Video zoom");
        writeDouble("saturation", "Saturation");
        writeDouble("brightness", "Brightness");
        writeDouble("contrast", "Contrast");
'''
    if old not in s:
        raise SystemExit('log display anchor not found')
    s = s.replace(old, new, 1)

if 'case Qt::Key_Return:' not in s:
    old = '''    case Qt::Key_F11: isFullScreen() ? showNormal() : showFullScreen(); break;
    case Qt::Key_Escape: if (isFullScreen()) showNormal(); break;'''
    new = '''    case Qt::Key_F11: toggleFullscreen(); break;
    case Qt::Key_Escape: if (isFullScreen()) toggleFullscreen(); break;
    case Qt::Key_Return:
    case Qt::Key_Enter: toggleFullscreen(); break;'''
    if old not in s:
        raise SystemExit('fullscreen key anchor not found')
    s = s.replace(old, new, 1)

if 'm_fullscreenHideTimer.start();' not in s:
    old = 'bool MainWindow::eventFilter(QObject* watched, QEvent* event) {\n    if (watched == m_seekSlider && event->type() == QEvent::MouseButtonPress) {'
    new = '''bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_videoWidget && event->type() == QEvent::MouseMove && isFullScreen()) {
        setControlsVisible(true);
        m_fullscreenHideTimer.start();
    }

    if (watched == m_seekSlider && event->type() == QEvent::MouseButtonPress) {'''
    if old not in s:
        raise SystemExit('event filter anchor not found')
    s = s.replace(old, new, 1)

if 'void MainWindow::toggleFullscreen()' not in s:
    old = 'void MainWindow::setControlsVisible(bool visible) { if (m_controls) m_controls->setVisible(visible); }'
    new = '''void MainWindow::toggleFullscreen() {
    if (isFullScreen()) {
        m_fullscreenHideTimer.stop();
        setControlsVisible(true);
        if (m_playlistDock) m_playlistDock->setVisible(m_playlistWasVisibleBeforeFullscreen);
        showNormal();
        return;
    }

    m_playlistWasVisibleBeforeFullscreen = m_playlistDock && m_playlistDock->isVisible();
    if (m_playlistDock) m_playlistDock->hide();
    setControlsVisible(false);
    showFullScreen();
}

void MainWindow::showDisplayDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Display"));
    dialog.setModal(true);
    dialog.resize(460, 280);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto makeSlider = [&](const QString& name, int value, const char* property) {
        auto* row = new QWidget(&dialog);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(-100, 100);
        slider->setValue(value);
        auto* valueLabel = new QLabel(QString::number(value), row);
        valueLabel->setMinimumWidth(42);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(valueLabel);
        connect(slider, &QSlider::valueChanged, &dialog, [this, property, valueLabel](int v) {
            valueLabel->setText(QString::number(v));
            setPropertyDouble(property, v);
        });
        form->addRow(name, row);
        return slider;
    };

    const int originalSaturation = m_saturation;
    const int originalBrightness = m_brightness;
    const int originalContrast = m_contrast;
    auto* saturation = makeSlider(QStringLiteral("Saturation"), m_saturation, "saturation");
    auto* brightness = makeSlider(QStringLiteral("Brightness"), m_brightness, "brightness");
    auto* contrast = makeSlider(QStringLiteral("Contrast"), m_contrast, "contrast");
    layout->addLayout(form);

    auto* note = new QLabel(QStringLiteral("Range: −100 to +100. Changes are previewed immediately and saved when you press OK."), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* reset = buttons->addButton(QStringLiteral("Reset defaults"), QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);

    connect(reset, &QPushButton::clicked, &dialog, [&] {
        saturation->setValue(0);
        brightness->setValue(0);
        contrast->setValue(0);
    });

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        m_saturation = saturation->value();
        m_brightness = brightness->value();
        m_contrast = contrast->value();
        QSettings settings(QStringLiteral("REX Player"), QStringLiteral("REX Player"));
        settings.setValue(QStringLiteral("display/saturation"), m_saturation);
        settings.setValue(QStringLiteral("display/brightness"), m_brightness);
        settings.setValue(QStringLiteral("display/contrast"), m_contrast);
        dialog.accept();
    });

    connect(buttons, &QDialogButtonBox::rejected, &dialog, [&] {
        m_saturation = originalSaturation;
        m_brightness = originalBrightness;
        m_contrast = originalContrast;
        setPropertyDouble("saturation", m_saturation);
        setPropertyDouble("brightness", m_brightness);
        setPropertyDouble("contrast", m_contrast);
        dialog.reject();
    });

    dialog.exec();
}

void MainWindow::setControlsVisible(bool visible) { if (m_controls) m_controls->setVisible(visible); }'''
    if old not in s:
        raise SystemExit('visibility helper anchor not found')
    s = s.replace(old, new, 1)

cpp.write_text(s)
print('Display/fullscreen changes applied')
