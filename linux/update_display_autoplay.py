from pathlib import Path

h = Path('linux/src/MainWindow.h')
s = h.read_text()
if 'class QCheckBox;' not in s:
    s = s.replace('class QCloseEvent;\n', 'class QCloseEvent;\nclass QCheckBox;\n', 1)
if 'QCheckBox* m_autoplayCheck' not in s:
    s = s.replace('    QPushButton* m_logButton = nullptr;\n', '    QPushButton* m_logButton = nullptr;\n    QCheckBox* m_autoplayCheck = nullptr;\n', 1)
if 'bool m_autoplayPlaylist' not in s:
    s = s.replace('    int m_currentPlaylistIndex = -1;\n', '    int m_currentPlaylistIndex = -1;\n    bool m_autoplayPlaylist = true;\n', 1)
h.write_text(s)

cpp = Path('linux/src/MainWindow.cpp')
s = cpp.read_text()
if '#include <QCheckBox>' not in s:
    s = s.replace('#include <QCloseEvent>\n', '#include <QCloseEvent>\n#include <QCheckBox>\n', 1)
if 'playlist/autoplay' not in s:
    s = s.replace(
        '    m_contrast = std::clamp(settings.value(QStringLiteral("display/contrast"), m_contrast).toInt(), -100, 100);\n',
        '    m_contrast = std::clamp(settings.value(QStringLiteral("display/contrast"), m_contrast).toInt(), -100, 100);\n'
        '    m_autoplayPlaylist = settings.value(QStringLiteral("playlist/autoplay"), m_autoplayPlaylist).toBool();\n', 1)

if 'm_autoplayCheck = new QCheckBox' not in s:
    old = '''    auto* clear = new QPushButton(QStringLiteral("Clear"), playlistPanel);
    connect(clear, &QPushButton::clicked, this, &MainWindow::clearPlaylist);
    playlistButtons->addWidget(clear);
    playlistLayout->addLayout(playlistButtons);'''
    new = '''    auto* clear = new QPushButton(QStringLiteral("Clear"), playlistPanel);
    connect(clear, &QPushButton::clicked, this, &MainWindow::clearPlaylist);
    playlistButtons->addWidget(clear);
    playlistLayout->addLayout(playlistButtons);
    m_autoplayCheck = new QCheckBox(QStringLiteral("Autoplay next item"), playlistPanel);
    m_autoplayCheck->setChecked(m_autoplayPlaylist);
    m_autoplayCheck->setToolTip(QStringLiteral("Automatically play the next playlist item when the current item reaches the end."));
    connect(m_autoplayCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_autoplayPlaylist = checked;
        QSettings settings(QStringLiteral("REX Player"), QStringLiteral("REX Player"));
        settings.setValue(QStringLiteral("playlist/autoplay"), checked);
        settings.sync();
    });
    playlistLayout->addWidget(m_autoplayCheck);'''
    if old not in s: raise SystemExit('playlist anchor not found')
    s = s.replace(old, new, 1)

s = s.replace(
    '            if (end && end->reason == MPV_END_FILE_REASON_EOF) playNext();\n',
    '            if (end && end->reason == MPV_END_FILE_REASON_EOF && m_autoplayPlaylist) playNext();\n', 1)

if 'Autoplay next item:' not in s:
    s = s.replace(
        '    out << "Frame forward shortcut: " << m_frameForwardKey.toString() << "\\n";\n',
        '    out << "Frame forward shortcut: " << m_frameForwardKey.toString() << "\\n";\n'
        '    out << "Autoplay next item: " << (m_autoplayPlaylist ? "enabled" : "disabled") << "\\n";\n', 1)

start = s.index('void MainWindow::showDisplayDialog() {')
end = s.index('\nvoid MainWindow::setControlsVisible(bool visible)', start)
method = '''void MainWindow::showDisplayDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Display"));
    dialog.setModal(true);
    dialog.resize(460, 280);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    const int currentSaturation = std::clamp(static_cast<int>(std::lround(getPropertyDouble("saturation"))), -100, 100);
    const int currentBrightness = std::clamp(static_cast<int>(std::lround(getPropertyDouble("brightness"))), -100, 100);
    const int currentContrast = std::clamp(static_cast<int>(std::lround(getPropertyDouble("contrast"))), -100, 100);

    auto makeSlider = [&](const QString& name, int value, const char* property, int* storedValue, const char* settingKey) {
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
        connect(slider, &QSlider::valueChanged, &dialog, [this, property, storedValue, settingKey, valueLabel](int v) {
            valueLabel->setText(QString::number(v));
            *storedValue = v;
            setPropertyDouble(property, v);
            QSettings settings(QStringLiteral("REX Player"), QStringLiteral("REX Player"));
            settings.setValue(QString::fromUtf8(settingKey), v);
            settings.sync();
        });
        form->addRow(name, row);
        return slider;
    };

    auto* saturation = makeSlider(QStringLiteral("Saturation"), currentSaturation, "saturation", &m_saturation, "display/saturation");
    auto* brightness = makeSlider(QStringLiteral("Brightness"), currentBrightness, "brightness", &m_brightness, "display/brightness");
    auto* contrast = makeSlider(QStringLiteral("Contrast"), currentContrast, "contrast", &m_contrast, "display/contrast");
    layout->addLayout(form);

    auto* note = new QLabel(QStringLiteral("Range: −100 to +100. Changes are applied and remembered immediately."), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* reset = buttons->addButton(QStringLiteral("Reset defaults"), QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);
    connect(reset, &QPushButton::clicked, &dialog, [&] {
        saturation->setValue(0);
        brightness->setValue(0);
        contrast->setValue(0);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);

    dialog.adjustSize();
    const int margin = 16;
    const QSize size = dialog.size();
    const QPoint global = mapToGlobal(QPoint(
        std::max(margin, width() - size.width() - margin),
        std::max(margin, height() - size.height() - margin)));
    dialog.move(global);
    dialog.exec();
}'''
s = s[:start] + method + s[end:]
cpp.write_text(s)
print('Update applied')
