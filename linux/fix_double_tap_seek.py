from pathlib import Path

p = Path("linux/src/MainWindow.cpp")
s = p.read_text()
old = '''            if (x < width / 3.0) seekBackward();
            else if (x > width * 2.0 / 3.0) seekForward();
            else togglePause();'''
new = '''            if (x < width / 3.0) {
                const char* args[] = {"seek", "-10", "relative", "exact", nullptr};
                command(args);
            } else if (x > width * 2.0 / 3.0) {
                const char* args[] = {"seek", "10", "relative", "exact", nullptr};
                command(args);
            } else {
                togglePause();
            }'''
assert old in s
p.write_text(s.replace(old, new, 1))
