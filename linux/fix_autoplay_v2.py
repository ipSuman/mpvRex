from pathlib import Path

path = Path("linux/src/MainWindow.cpp")
text = path.read_text()

old = '''        if (event->event_id == MPV_EVENT_END_FILE) {
            auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end && end->reason == MPV_END_FILE_REASON_EOF && m_autoplayPlaylist) playNext();
        } else if (event->event_id == MPV_EVENT_SHUTDOWN) {'''
new = '''        if (event->event_id == MPV_EVENT_END_FILE) {
            auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end && end->reason == MPV_END_FILE_REASON_EOF && m_autoplayPlaylist) {
                const int finishedIndex = m_currentPlaylistIndex;
                QTimer::singleShot(0, this, [this, finishedIndex] {
                    if (m_autoplayPlaylist && m_currentPlaylistIndex == finishedIndex)
                        playNext();
                });
            }
        } else if (event->event_id == MPV_EVENT_SHUTDOWN) {'''
if old not in text:
    raise SystemExit("END_FILE block not found")
text = text.replace(old, new, 1)

old = '''void MainWindow::addToPlaylist(const QString& path) {
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
}'''
new = '''void MainWindow::addToPlaylist(const QString& path) {
    if (!m_playlist || path.isEmpty()) return;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_playlist->count(); ++i) {
        if (m_playlist->item(i)->data(Qt::UserRole).toString() == absolute) {
            m_playlist->setCurrentRow(i);
            return;
        }
    }
    const int currentRow = m_playlist->currentRow();
    auto* item = new QListWidgetItem(QFileInfo(absolute).fileName(), m_playlist);
    item->setToolTip(absolute);
    item->setData(Qt::UserRole, absolute);
    if (currentRow >= 0)
        m_playlist->setCurrentRow(currentRow);
    else
        m_playlist->setCurrentItem(item);
}'''
if old not in text:
    raise SystemExit("addToPlaylist block not found")
text = text.replace(old, new, 1)

old = '''void MainWindow::addFiles() { const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Add media files")); if (paths.isEmpty()) return; for (const QString& path : paths) addToPlaylist(path); if (m_playlist && m_playlist->currentItem()) playlistActivated(); }
void MainWindow::addFolder() { const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Add media folder")); if (path.isEmpty() || !m_playlist) return; QDir dir(path); const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase); for (const QFileInfo& info : files) if (isMediaFile(info)) addToPlaylist(info.absoluteFilePath()); if (m_playlist->currentItem()) playlistActivated(); }'''
new = '''void MainWindow::addFiles() {
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Add media files"));
    if (paths.isEmpty() || !m_playlist) return;
    const bool wasEmpty = m_playlist->count() == 0;
    for (const QString& path : paths) addToPlaylist(path);
    if (wasEmpty && m_playlist->currentItem()) playlistActivated();
}
void MainWindow::addFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Add media folder"));
    if (path.isEmpty() || !m_playlist) return;
    const bool wasEmpty = m_playlist->count() == 0;
    QDir dir(path);
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& info : files) if (isMediaFile(info)) addToPlaylist(info.absoluteFilePath());
    if (wasEmpty && m_playlist->currentItem()) playlistActivated();
}'''
if old not in text:
    raise SystemExit("addFiles/addFolder block not found")
text = text.replace(old, new, 1)

old = '''void MainWindow::dropEvent(QDropEvent* event) { const auto urls = event->mimeData()->urls(); for (const auto& url : urls) if (url.isLocalFile()) addToPlaylist(url.toLocalFile()); if (m_playlist && m_playlist->currentItem()) playlistActivated(); if (!urls.isEmpty()) event->acceptProposedAction(); }'''
new = '''void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (!m_playlist) return;
    const bool wasEmpty = m_playlist->count() == 0;
    for (const auto& url : urls) if (url.isLocalFile()) addToPlaylist(url.toLocalFile());
    if (wasEmpty && m_playlist->currentItem()) playlistActivated();
    if (!urls.isEmpty()) event->acceptProposedAction();
}'''
if old not in text:
    raise SystemExit("dropEvent block not found")
text = text.replace(old, new, 1)

path.write_text(text)
