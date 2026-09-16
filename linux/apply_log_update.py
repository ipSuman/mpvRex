from pathlib import Path

h = Path("linux/src/MainWindow.h")
s = h.read_text()
if "void saveLogReport();" not in s:
    marker = "    void cutAbSelection();\n"
    if marker not in s:
        raise SystemExit("MainWindow.h insertion point not found")
    s = s.replace(marker, marker + "    void saveLogReport();\n", 1)
if "QPushButton* m_logButton" not in s:
    marker = "    QPushButton* m_cutAbButton = nullptr;\n"
    if marker not in s:
        raise SystemExit("MainWindow.h button insertion point not found")
    s = s.replace(marker, marker + "    QPushButton* m_logButton = nullptr;\n", 1)
h.write_text(s)

cpp = Path("linux/src/MainWindow.cpp")
s = cpp.read_text()
for inc, anchor in [
    ("#include <QCoreApplication>\n", "#include <QCloseEvent>\n"),
    ("#include <QDateTime>\n", "#include <QDir>\n"),
    ("#include <QSysInfo>\n", "#include <QStandardPaths>\n"),
    ("#include <QTextStream>\n", "#include <QStyle>\n"),
]:
    if inc not in s:
        if anchor not in s:
            raise SystemExit(f"include insertion point not found: {anchor}")
        s = s.replace(anchor, anchor + inc, 1)

button_old = '''    connect(m_cutAbButton, &QPushButton::clicked, this, &MainWindow::cutAbSelection);\n    row->addWidget(m_cutAbButton);\n    m_hwButton = new QPushButton(QStringLiteral("SW"), m_controls);'''
button_new = '''    connect(m_cutAbButton, &QPushButton::clicked, this, &MainWindow::cutAbSelection);\n    row->addWidget(m_cutAbButton);\n    m_logButton = new QPushButton(QStringLiteral("Save Log"), m_controls);\n    m_logButton->setFixedWidth(72);\n    m_logButton->setToolTip(QStringLiteral("Save a diagnostic log report"));\n    connect(m_logButton, &QPushButton::clicked, this, &MainWindow::saveLogReport);\n    row->addWidget(m_logButton);\n    m_hwButton = new QPushButton(QStringLiteral("SW"), m_controls);'''
if "m_logButton = new QPushButton" not in s:
    if button_old not in s:
        raise SystemExit("Save Log button insertion point not found")
    s = s.replace(button_old, button_new, 1)

if "void MainWindow::saveLogReport()" not in s:
    marker = "void MainWindow::updateHardwareButton() {"
    if marker not in s:
        raise SystemExit("updateHardwareButton insertion point not found")
    method = r'''void MainWindow::saveLogReport() {
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

'''
    s = s.replace(marker, method + marker, 1)

cpp.write_text(s)
print("Save Log update prepared")
