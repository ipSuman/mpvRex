from pathlib import Path

p = Path("linux/src/MainWindow.cpp")
s = p.read_text()
old = '            if (end && end->reason == MPV_END_FILE_REASON_EOF && m_autoplayPlaylist) {'
new = '            if (end && m_autoplayPlaylist) {'
assert old in s
p.write_text(s.replace(old, new, 1))
