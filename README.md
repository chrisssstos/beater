# beater

A small two-deck, loop-based DJ app that sits next to Resolume. Qt 6 / QML / C++, macOS first.

## Build

Open `CMakeLists.txt` in Qt Creator with the Qt 6.11 macOS kit and hit Run. Or from a terminal:

```sh
git submodule update --init   # the UI kit, external/highlighter-ui
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=~/Qt/6.11.2/macos
cmake --build build
open build/beater.app
```

The first configure clones miniaudio and Rubber Band from GitHub (FetchContent), so it needs internet once.

### Windows

Not built or tested on Windows yet; the CMake setup is written for it (MSVC).

1. Install [Git](https://git-scm.com/download/win), and Visual Studio 2022 (the free
   Community edition or the Build Tools) with the **Desktop development with C++** workload.
2. Install Qt with the [Qt Online Installer](https://www.qt.io/download-qt-installer):
   **Qt 6.11.2 → MSVC 2022 64-bit**, and under Developer and Designer Tools **CMake**,
   **Ninja** and **Qt Creator**. (Qt 6.9 or newer is needed for the glow.)
3. Clone with the UI kit (a submodule):
   `git clone --recurse-submodules https://github.com/chrisssstos/beater.git`
4. Easiest: open `CMakeLists.txt` in Qt Creator, pick the *Desktop Qt 6.11.2 MSVC2022 64bit*
   kit, and press Run. It downloads miniaudio and Rubber Band on the first configure.

   Or in an *x64 Native Tools Command Prompt for VS 2022*:
   ```bat
   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64
   cmake --build build
   C:\Qt\6.11.2\msvc2022_64\bin\windeployqt.exe --qmldir qml build\beater.exe
   build\beater.exe
   ```
   (`windeployqt` copies the Qt DLLs and QML modules next to the exe, so it runs outside
   Qt Creator.)

On Windows the files are decoded by miniaudio: MP3, WAV and FLAC work, M4A / AAC and AIFF
don't (on macOS Core Audio decodes those). The library, settings and audio output work the
same; the library lives in `%APPDATA%\BLOCK SYSTEM\beater\library.json`.

### macOS notes

If `cmake` / `ninja` aren't on your PATH, use the ones Qt ships:
`export PATH=~/Qt/Tools/CMake/CMake.app/Contents/bin:~/Qt/Tools/Ninja:$PATH`.

Headless engine checks: `./build/engine_test`.

Files or folders passed on the command line are added to the playlist. Debug hooks
(environment variables, see `src/main.cpp`): `BEATER_TEST=1` uses its own settings and
Qt's test data folders instead of the real library (and the window ignores real clicks), `BEATER_AUTOPLAY=1` loads rows 1 and 2 into
decks A and B, plays them and logs deck state, and `BEATER_SNAPSHOT=out.png` saves
a grab of the window after a few seconds and quits, `BEATER_MODE=perform|edit` starts in
that mode, `BEATER_FPS=1` logs frames per second and the time spent building waveform frames. `BEATER_SIZE=WxH` starts at that window size (never below the minimum).

## How it works

- **Edit and two Perform layouts** (Edit / Column / Strip at the top; ⌘E flips between Edit
  and the Perform layout used last). Column is tall and narrow, Strip wide and short, to see
  which sits better next to Resolume; each remembers its own window size. Edit has everything: the decks
  stacked full width (big waveforms), loops, grid / BPM / key editing, and beside them the
  mixer and the playlist. Perform is a narrow column
  to keep open next to Resolume: master tempo, per deck the overview, play / sync / loop,
  key, BPM and volume, then the master fader and the playlist. **Pin** keeps it above other
  windows. Each mode remembers its own window size.

- **Master tempo** is a fixed BPM you set (drag the number, scroll, ±, or double-click to type). Everything follows it.
- **Sync** is on by default for each deck. A synced deck is time-stretched to the master tempo and phase-locked to the master grid (Rubber Band R3 engine, so pitch is untouched). Pressing play starts the deck immediately, already in phase: it jumps to the nearest position whose beat (or bar, with PHASE = BAR) matches the master.
- **Key shift** in semitones, tempo stays locked. The key readout follows.
- **Nothing is ever cut off**: when a deck's row doesn't fit, its least needed pieces drop
  out (the playing BPM, the shift reset, the labels, key Auto, ×2 / ÷2, in that order), and
  the window can't be made smaller than what's left needs, in either mode.
- **A deck (Edit)**: the track, the big waveform, the timeline with the loop lane and its
  tags, then one row: Play, Sync, In / Out / Loop, the loop length, the track's BPM (and
  the BPM it plays at), ×2 / ÷2, key, key shift (and the key it plays in when shifted), and
  the pencil. Beneath it is space kept
  for effects.
- **Grid edit** (the pencil): the row becomes BPM, ×2 / ÷2, Tap, =M, grid nudge, Set 1,
  Auto, and key. Drag the big waveform to slide the grid under the music; double-click a beat
  to make it the 1. Done (or the pencil again) goes back.
- **Beat squares** per deck (Edit and Perform): four squares walk 1-2-3-4 through the bar,
  the 1 brightest. A playing deck shows its own beats; a stopped one shows the master's,
  dimmed, which it locks to when you press play, so you can start it on the 1.
- **Play / Pause**: two icon buttons (hover for what they do), the state you're in solid. PLAY while paused plays from
  there; PLAY again while playing starts the track over from its first bar line. Jumps keep
  a synced deck in phase: it lands at the same place in the beat (in the bar, with bar phase
  lock), at or just after the target.
- **Moving around**: click or drag on the timeline (the overview under the big waveform) and
  the playhead follows; the deck jumps there when you let go, on the beat, or in phase with
  the master when it's synced and playing. The big waveform can be grabbed and dragged the
  same way.
- **Loading**: drag a track from the library onto a deck (in either mode), double-click it,
  or press A / B. Or drop files straight from the Finder onto a deck: they go into the library
  and the first one loads. Over a playing deck the drop target turns red: it replaces that
  track.
- **Library**: every track you've added, and your saved playlists (the dropdown at the top).
  NEW makes a playlist (type its name in the field); the bin then "Delete?" removes one;
  + by the Library title adds tracks or folders. With a
  playlist open, files you drop or add go into it too, and ⌫ takes one out; in All tracks, +
  puts a track in the playlist you opened last, and ⌫ removes it from the library. It all
  lives in library.json, the open view too.
- **Crossfader**: the XY pad standing up, next to the faders (and beside / between the decks
  in Perform). A at the top, B at the bottom; in the middle both play at their faders, and
  towards one end the other fades out. Only y counts for now (x is kept for four decks).
  Double-click recentres it.
- **Loops**: every track has three, and the deck loops one at a time:
  - **Intro**: its first N bars (8 by default). A loaded track starts with INTRO armed, so
    play loops the intro until you release it.
  - **In / Out**: a loop anywhere in between. IN marks the playhead (nearest beat); OUT
    marks the end and the loop starts at once, jumping back to the In in phase. IN while a
    closed In / Out loop runs plays it again from its In. LOOP turns it
    off and back on (on again with the playhead past it jumps back in). HERE makes one of
    the chosen length from the current bar.
  - **Outro** (the end loop): its last N bars. Armed by default, so a track that runs out keeps looping its
    ending until you bring in the next one.

  In use: In / Out if on, else the intro if armed, else the end. A loop the playhead has
  left behind (seeked past it) releases itself, so the end loop always catches the track.
  The bar buttons set the length of the loop in use. The **loop lane** under the timeline
  shows all three (in use solid, armed half lit, off dim), each with a tag under it (Intro,
  In / Out, Outro; click a tag to switch that loop on or off): drag an end to resize, the middle
  to move, the empty lane to draw an In / Out loop (Shift-drag on the timeline does too);
  double-click a loop to reset it. Snaps to bars, hold Option for beats.
- **Output**: OUTPUT at the top right (Edit mode) picks the audio device: System default or
  any output by name; remembered. Switching is live (~60 ms gap, decks keep playing; the
  engine keeps its rate and miniaudio converts). If the device goes away the app plays
  through the default meanwhile and goes back when it returns; a device whose clock stalls
  is reopened.
- **Look**: HighlighterUI's "hardware" skin: a warm grey faceplate, dark screens for the
  waveforms and values, and whatever is on glows. Orange is the main glow and deck A, a pink-red
  neon is deck B; everything in a deck lights up in its colour. The master tempo and the deck clocks
  are reflective LCDs.
- **Keys** show as names (F#m). BPMs show as whole numbers unless they have decimals. LOOP toggles looping on the deck.
- **Volume**: a fader per deck plus master. No automatic transitions.
- **Analysis**: BPM, beat grid and key are detected on import (tracks analyzed by an older
  build are analyzed again once). Everything is editable: click the BPM to type it, ×2 / ÷2,
  TAP, nudge the grid, SET 1 puts the downbeat at the playhead, pick the key from the list.
  **Auto** (next to the grid, and next to the key) drops your edit and detects it again. Edits are stored per track (keyed by file content, so renames don't lose them) in `~/Library/Application Support/BLOCK SYSTEM/beater/library.json` and are never overwritten by re-analysis.

## Layout

```
src/core       small shared types (beat grid, keys, lock-free queue, track buffer)
src/audio      decoder (ExtAudioFile on macOS, miniaudio fallback), deck engine, audio engine
src/analysis   tempo / grid estimator, key estimator
src/app        Qt side: library model, deck + app controllers, waveform item
qml/           UI, built on HighlighterUI (external/highlighter-ui, `import HighlighterUI`).
               Look.qml holds the deck colours (A orange, B violet; orange is the app accent).
               A panel that declares `property color accent` tints the kit controls inside it.
tests/         headless engine tests
```

Audio thread rules: no locks, no allocation. The UI talks to it through a lock-free command queue and atomics; decoded buffers are released back to the UI thread for freeing.

See `docs/ROADMAP.md` for what's next.
