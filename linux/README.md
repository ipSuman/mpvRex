# REX Player for Linux

The Linux edition of REX Player is being developed as a native desktop frontend around **libmpv**, using **Qt 6** and **CMake**.

## Current status

This is the first foundation milestone:

- Native Qt 6 desktop window
- libmpv embedded into the application window
- Open a media file from the command line
- Drag and drop a local media file
- Basic mpv event pumping
- CMake-based build

The Linux player is intentionally being built alongside the Android project rather than attempting to port the Android UI directly.

## Build on Ubuntu/Debian

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config qt6-base-dev libmpv-dev

cmake -S linux -B linux/build
cmake --build linux/build -j$(nproc)

./linux/build/rex-player
```

Open a file directly:

```bash
./linux/build/rex-player /path/to/video.mkv
```

## Roadmap

1. Stable libmpv embedding and rendering
2. REX desktop UI
3. File browser and playlist system
4. Track/subtitle controls
5. Gesture equivalents for mouse/touchpad
6. Zoom, pan, A-B loop and frame navigation
7. Resume/history database
8. Network shares and streaming
9. yt-dlp integration
10. Clipping/export
11. Jellyfin integration
12. Linux packaging (AppImage, Flatpak, distro packages)

## Mouse and touchpad controls

On the video surface:

- Double-click the left or right third to seek backward or forward 10 seconds.
- Double-click the center to pause or resume.
- Scroll to seek 5 seconds at a time.
- Hold `Ctrl` while scrolling to adjust volume.
- Hold `Alt` while scrolling to zoom the video.
- Drag with the middle mouse button to pan a zoomed video.
- Press `+` or `-` to zoom, and `Z` to reset zoom and pan.

## Precision playback

- Press `A` to set the A–B loop start and `B` to set its end; press `L` to clear it.
- Press `.` to step forward one frame or `,` to step backward one frame.
