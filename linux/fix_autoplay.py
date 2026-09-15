from pathlib import Path

path = Path("linux/src/MainWindow.cpp")
text = path.read_text()
old = '        mpv_set_option_string(m_mpv, "keep-open", "yes") < 0 ||\n'
new = ('        mpv_set_option_string(m_mpv, "idle", "yes") < 0 ||\n'
       '        mpv_set_option_string(m_mpv, "keep-open", "no") < 0 ||\n')
if old not in text:
    raise SystemExit("Expected keep-open option not found")
path.write_text(text.replace(old, new, 1))
print("Autoplay fix applied: libmpv now emits EOF/end-file instead of pausing on keep-open.")
