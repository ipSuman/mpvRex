from pathlib import Path
import subprocess

cpp = Path("linux/src/MainWindow.cpp")
s = cpp.read_text()
if "void MainWindow::saveLogReport()" not in s:
    known_good = subprocess.check_output([
        "git", "show", "6c9bbcaf79229b68c6e6afe56c7a9ed9a9923c06:linux/src/MainWindow.cpp"
    ], text=True)
    start = known_good.index("void MainWindow::saveLogReport()")
    end = known_good.index("void MainWindow::updateHardwareButton()", start)
    method = known_good[start:end]
    marker = "void MainWindow::updatePlaybackUi() {"
    if marker not in s:
        raise SystemExit("updatePlaybackUi insertion point not found")
    s = s.replace(marker, method + "\n" + marker, 1)
    cpp.write_text(s)
