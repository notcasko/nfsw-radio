# NFS World Custom Radio & Music Player Mod

An in-game music and radio station modification system for **Need for Speed: World**.

This project provides two distinct ASI mods:
1. **Radio Mod (`Radio.asi`)** — The full mod. An external radio player powered by FMOD with support for custom stations, custom audio tracks, album cover art, draggable layered HUD overlays, live hot-reloading of music files, and real-time custom skins.
2. **Music Player Mod (`MusicPlayer.asi`)** — A lightweight, zero-dependency controller for the game's built-in soundtrack (EA Trax).

---

## Which Mod Should You Use

### 1. `Radio.asi` (Full Mod)
If you want to listen to your own personal music library while cruising or racing, this is the mod you want.
- **Plays any audio format** (MP3, FLAC, OGG, WAV, AAC, M4A, Opus, etc.) using the FMOD sound engine.
- **Multiple custom radio stations**: organize music into folders and flip between them in-game just like a real car radio.
- **Rich on-screen HUD**: Displays station name, track title, artist, playback status, playback order, and extracted album artwork.
- **Draggable UI**: Click and drag the radio HUD anywhere across your screen; coordinates are automatically remembered.
- **Built-in EA Trax control**: When you pause custom radio playback, `Radio.asi` seamlessly hands audio control back to the game's original soundtrack.

### 2. `MusicPlayer.asi` (Lightweight In-Game Soundtrack Controller)
If you only want to control the official in-game music (EA Trax) without setting up external music folders or extra DLLs:
- Lets you skip tracks, jump between game playlists, cycle repeat/shuffle modes, pause/resume, and adjust game music volume with hotkeys.
- Shows a clean, minimalist popup banner at the top of your screen.
- Has zero external dependencies (no FMOD required).

---

## How They Work Together (Auto-Bailout)

You don't have to worry about audio conflicts or double-hooking:
- **`MusicPlayer.asi` is the simple version of `Radio.asi`.**
- Because `Radio.asi` already includes full in-game music control as a fallback when custom radio is paused, having both active at once would cause conflicting hotkeys and duplicate memory hooks.
- **Automatic Safety**: Both at startup and on every tick, `MusicPlayer.asi` checks if `Radio.asi` is loaded (via named synchronization events, environment variables, loaded module checks, and window handles). **If `Radio.asi` is present, `MusicPlayer.asi` gracefully steps aside, shuts down its threads, and unhooks completely.**

> **Recommendation:** If you install `Radio.asi`, you do not need `MusicPlayer.asi`.

---

## Features Overview

| Feature | `Radio.asi` | `MusicPlayer.asi` |
| :--- | :---: | :---: |
| Play external music files (MP3, FLAC, OGG, etc.) | Yes | No |
| Multiple custom radio station folders | Yes | No |
| Dynamic live music rescan (add songs while playing) | Yes | No |
| Skip, cycle, and control in-game soundtrack (EA Trax) | Yes (when radio paused) | Yes |
| Album cover art display (ID3v2, FLAC/Vorbis, folder images) | Yes | No |
| Custom GDI+ skins with live switching (`G + Tab`) | Yes | No |
| Interactive draggable on-screen HUD | Yes | No |
| Auto-pause when Alt-Tabbed / game loses focus | Yes | Built-in game behavior |
| Mouse wheel volume control (`X` + scroll) | Yes | Yes |
| Foreign character support (CJK: Japanese, Chinese, Korean) | Yes (auto font fallback) | Yes (auto font fallback) |

---

## Controls & Default Keybindings

All hotkeys can be customized in `Settings/Radio/RadioMod.ini`.

| Action | Default Shortcut | Description |
| :--- | :--- | :--- |
| **Next Track** | `Tab` | Skips to the next track in the current station (or next EA Trax track if paused). |
| **Next Station / Playlist** | `Caps Lock` + `Tab` | Switches to the next radio station folder (or next in-game playlist if paused). |
| **Toggle Playback** | `Q` + `Tab` | Pauses custom radio and restores in-game EA Trax (or resumes custom radio). |
| **Playlist Order Mode** | `F` + `Tab` | Cycles playback order: **Default** -> **Repeat Playlist** -> **Repeat Track** -> **Shuffle**. |
| **Switch Skin** | `G` + `Tab` | Cycles through installed skins on the fly without restarting the game (`Radio.asi` only). |
| **Adjust Volume** | Hold `X` + **Mouse Wheel** | Smoothly increases or decreases volume. |
| **Move HUD Overlay** | **Left Click & Drag** | Click directly on the radio popup and drag it to your preferred position. |

---

## Installation Guide

### Prerequisites
- Need for Speed: World with an **ASI Loader** installed (most game installations or community launchers already have `dinput8.dll` or `scripts/` folder support enabled).

### Step-by-Step Installation

1. **Place the ASI file:**
   - Copy `Radio.asi` (or `MusicPlayer.asi`) into your game directory's `scripts/` folder:
     ```text
     GameFolder/
     └── scripts/
         ├── Radio.asi
     ```
2. **Optionally copy Skins:**
   - Copy the `skins/` folder into:
     ```text
     GameFolder/
     └── Settings/
         └── Radio/
             ├── RadioMod.ini
             └── skins/
                 ├── FH6/
                 ├── miku/
                 ├── mw05_black/
                 ├── mw05_blue/
                 └── mw05_yellow/
     ```

---

## Organizing Your Music

By default, the mod looks for music in your game directory under `Sound/Radio/`:

```text
GameFolder/
└── Sound/
    └── Radio/
        ├── Rock Station/
        │   ├── track1.mp3
        │   ├── track2.flac
        │   └── cover.jpg        <-- (Optional station/folder artwork)
        ├── Synthwave FM/
        │   ├── song1.ogg
        │   └── song2.m4a
        └── Eurobeat Classics/
            ├── running_in_the_90s.mp3
            └── deja_vu.mp3
```

- **Folders become stations**: Each subfolder inside `Sound/Radio/` is recognized as its own station with the folder's name.
- **Custom directory**: Edit `Settings/Radio/RadioMod.ini` if you want your music on another drive.
  ```ini
  [General]
  MusicPath = "D:\Music\My Playlists"
  ```
- **Hot-reloading**: You can drop new songs or new station folders into the directory while playing the game—the mod automatically scans in the background every 2.5 seconds without interrupting the currently playing track.

---

## Custom Skins & Skin Builder

`Radio.asi` comes with a powerful UI skinning engine. You can fully customize every aspect of the HUD or create your own from scratch.

### Included Skins
The repository includes several pre-configured skins located in `RadioMod/skins/`:
- **`Default`**: A procedural card if no image skin is loaded.
- **`Transparent`**: Default skin but without the card background.
- **`FH6`**: Modern, sleek layout inspired by Forza Horizon.
- **`miku`**: Stylized Hatsune Miku vocaloid theme.
- **`mw05_black`**: Clean dark Need for Speed: Most Wanted (2005) HUD style.
- **`mw05_blue`**: Blue accent HUD.
- **`mw05_yellow`**: Yellow accent HUD.

### Using the Skin Builder
In `RadioMod/SkinBuilder/`, you will find `skin_builder.html`.
1. Double-click **`skin_builder.html`** to open it in any web browser (Chrome, Edge, Firefox).
2. **Upload a background PNG**: Click *Choose File* to select your skin artwork (optional).
3. **Visually design the HUD**:
   - Drag and resize widgets on screen: Station Name, Artist, Track Title, Playback Status, Order Mode, Volume, and Album Art Cover.
   - Adjust font sizes, text colors, text alignments, and custom prefixes.
   - Test different states (*Playing*, *Paused*, *Station Change*, *Order Change*, *Volume Scroll*) using the preview dropdown.
   - Choose whether album art sits **above** or **below** your skin image (useful if your artwork has a transparent window cutout).
4. **Export**: Click **Download .json**.
5. **Install into the game**:
   - Create a folder named after your skin: `Settings/Radio/skins/<YourSkinName>/`.
   - Put your exported `<YourSkinName>.json` and `<YourSkinName>.png` inside.
   - Press `G + Tab` in-game to switch to it.

---

## How It Works Under the Hood

### 1. Game Memory Hooking
The mod interfaces directly with Need for Speed: World's internal audio engine. It resolves the game's executable base at runtime and hooks the virtual method table of the game's `MusicLogic` class (`MusicLogic::Update`).
- When custom radio is active, it zeroes the game music volume bus and silences EA Trax to prevent songs from playing over each other.
- When custom radio is paused or stopped, it cleanly restores the game audio bus and triggers in-game tracks.

### 2. Audio Pipeline (FMOD Ex)
- Music playback is offloaded to the industry-standard FMOD audio library.
- Audio is streamed asynchronously in chunks, guaranteeing zero stutter, hitching, or game frame drops during track loading.

### 3. Layered Windows & High-DPI GDI+ Rendering
- The HUD is rendered on an independent 32-bit alpha-blended transparent layered window (`WS_EX_LAYERED`).
- It draws directly into a double-buffered 32bpp DIB section using hardware anti-aliasing and ClearType font smoothing.
- **Zero in-game DirectX hook intrusion**: Because the popup is an OS-level layered window, it never conflicts with DirectX injectors, ReShade, or graphics wrappers.

### 4. International Font & CJK Fallback
- The metadata engine inspects track strings for CJK glyphs (Japanese Kanji/Hiragana/Katakana, Korean Hangul, Chinese Hanzi).
- If your skin uses a font that lacks CJK characters (such as Arial or Segoe UI), the mod automatically substitutes a compatible system font (e.g. *Meiryo*, *Yu Gothic*, *MS Gothic*, *Malgun Gothic*, or *Microsoft YaHei*) so titles never render as broken question marks or empty boxes.

### 5. Tag & Cover Art Extraction
- Native streaming parsers for ID3v1, ID3v2.2, ID3v2.3, ID3v2.4, and Vorbis comments.
- Supports UTF-8, UTF-16, and Japanese Shift-JIS encoding.
- Extracts embedded APIC image frames on the fly for album art. If no embedded cover is found, it automatically checks the folder for `cover.jpg`, `folder.png`, etc.

---

## Building from Source Code

Both mods are written in modern, standard C++ (Win32 API) without heavy external framework dependencies. They compile as 32-bit x86 DLLs (`.asi`).

### Requirements
- **Microsoft Visual C++ Build Tools** (Visual Studio 2019, 2022, or newer Build Tools).
- Standard Windows SDK (User32, Gdi32, Gdiplus, Ole32).

### Compiling `Radio.asi`
1. Open the Developer Command Prompt for VS (or run `vcvars32.bat`).
2. Navigate to `RadioMod/src/Radio/`.
3. Run the build batch file:
   ```cmd
   build_radio.bat
   ```
4. Output: `Radio.asi` compiled with static CRT runtime (`/MT`).

### Compiling `MusicPlayer.asi`
1. Open the Developer Command Prompt for VS (`vcvars32.bat`).
2. Navigate to `RadioMod/src/MusicPlayer/`.
3. Run the build batch file:
   ```cmd
   build_musicplayer.bat
   ```
4. Output: `MusicPlayer.asi`.

---

## Configuration Reference (`RadioMod.ini`)

A default configuration file is automatically created at `Settings/Radio/RadioMod.ini` on first launch.