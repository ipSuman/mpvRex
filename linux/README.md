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
- HW/SW decoding toggle with live status
- Video information dialog with file, codec, resolution, audio, subtitle and track details
- A-B stream-copy cutting through FFmpeg, preserving all mapped streams without re-encoding

The Linux player is intentionally being built alongside the Android project rather than attempting to port the Android UI directly.

## Build on Ubuntu/Debian

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config qt6-base-dev libmpv-dev ffmpeg

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
7. Network shares and streaming
8. yt-dlp integration
9. Clipping/export
10. Jellyfin integration
11. Linux packaging (AppImage, Flatpak, distro packages)

## Mouse and touchpad controls

On the video surface:

- Double-click the left or right third to seek backward or forward 10 seconds.
- Double-click the center to pause or resume.
- Click the **−10s** and **+10s** buttons to seek exactly 10 seconds.
- Click anywhere on the seek bar to jump directly to that position.
- Scroll to seek by the configurable seek duration.
- Hold `Ctrl` while scrolling to adjust volume.
- Hold `Alt` while scrolling to zoom the video.
- Drag with the middle mouse button to pan a zoomed video.
- Press `+` or `-` to zoom, and `Z` to reset zoom and pan.

## A-B cutting

- Set A and B with the configured A/B shortcuts, then click **Cut AB**.
- The cut uses FFmpeg stream copy (`-c copy`) and maps all streams, so video, every audio track and every subtitle track are copied without re-encoding.
- FFmpeg must be installed and the output container must support the copied streams.
- Stream-copy cutting is keyframe-limited, so the beginning can be slightly before the requested A point. Exact frame-accurate cutting requires re-encoding.

## Precision playback

- Press `A` to set the A–B loop start and `B` to set its end; press `L` to clear it.
- Press `.` to step forward one frame or `,` to step backward one frame.

## Hardware decoding

- The **HW/SW** button sits beside the A-B loop status.
- `HW` means a hardware decoder is actively being used for the current video.
- `SW` means software decoding is active, including when the current video or hardware setup cannot use a hardware decoder.
- Clicking the button toggles mpv between `hwdec=auto` and `hwdec=no`.

## Video information

- The **☰** button opens a scrollable information dialog instead of an on-screen mpv overlay.
- It shows details for the currently loaded media, including the file/container, title and path, size, duration and bitrate.
- Video details include codec, format, resolution, FPS, bitrate, pixel/color information, rotation, aspect ratio and active hardware decoder.
- Audio details include codec, format, sample rate, channels, channel layout and bitrate.
- The tracks section lists video, audio and subtitle tracks, languages/titles, codecs, external subtitle files and the active track.
- Playback/output details include current position, speed, pause state, video output and GPU API.
