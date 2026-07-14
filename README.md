# foo_input_msu1 — MSU-1 PCM Input for foobar2000 v2 x64

A fork of [qwertymodo/foo_input_msu](https://github.com/qwertymodo/foo_input_msu) rebuilt for the foobar2000 v2 SDK — with disk streaming, configurable looping, and fade-out.

MSU-1 PCM files use an 8-byte header (`"MSU1"` magic + 32-bit LE loop point in samples) followed by raw 16-bit signed stereo PCM at 44100 Hz. The original plugin loads the full file into RAM and loops forever with no fade. This fork streams from disk, lets you control how many times a track loops, and fades cleanly to silence.

---

## What's new

| Feature | Original (qwertymodo) | This fork |
|---|---|---|
| foobar2000 version | v1.x only (broken on v2) | **v2.x x64 — full SDK rewrite** |
| File loading | Entire file into RAM | Disk streaming, 2048-sample chunks |
| Loop count | Infinite, no fade | Configurable (0 = infinite, default 2) |
| Fade-out | None — hard cut | Linear fade, 1–60 s (default 8 s) |
| Duration display | Raw file length | Intro + N loops + fade time |
| Seek accuracy | Basic, ignores loops | Correct across intro / loop / fade |
| Metadata | tracknumber | codec · loop_start · tracknumber |
| Settings UI | None | Preferences → Advanced → Decoding |

---

## Requirements

foobar2000 v2.x (64-bit). Profile folder at `%AppData%\foobar2000-v2`. Does not work with foobar2000 v1 or 32-bit builds.

---

## Installation

1. Download `foo_input_msu1.dll` from the [Releases page](../../releases).
2. Create a folder named `foo_input_msu1` inside your user components directory:
   ```
   %AppData%\foobar2000-v2\user-components-x64\foo_input_msu1\
   ```
3. Place `foo_input_msu1.dll` inside that folder.
4. Restart foobar2000.

> **Note:** If the older `foo_input_msu` component is installed, remove it first — both plugins register for `.pcm` files and will conflict.

---

## Settings

**File → Preferences → Advanced → Decoding**

| Setting | Default | Range | Description |
|---|---|---|---|
| MSU-1 PCM: Loop count (0 = infinite) | 2 | 0 – 100 | Times the loop section plays before fading out. 0 = loop forever. |
| MSU-1 PCM: Fade-out duration (seconds) | 8 | 1 – 60 | Length of linear fade after the final loop. Only applies when loop count > 0. |

Changes take effect on the next track.

---

## Building from source

Requires Visual Studio 2022. The SDK path is set via `<FB2K_SDK>` in the `.vcxproj` — update it if you place the SDK elsewhere.

```
MSBuild foo_input_msu.vcxproj /p:Configuration=Release /p:Platform=x64
```

The post-build step copies the DLL to `%AppData%\foobar2000-v2\user-components-x64\foo_input_msu1\` automatically if the folder exists.

**SDK notes:** Built against foobar2000 SDK 20250307, target version 81 (v2.0). Set `FOOBAR2000_TARGET_VERSION 81` in `foobar2000-versions.h` to enable the v2 advconfig API.

---

## Technical notes

MSU-1 PCM format: 4-byte `MSU1` magic, 4-byte little-endian loop point in samples, then raw 16-bit signed stereo at 44100 Hz. A loop point at or past end-of-file means the track plays once with no looping.

Built using `input_stubs`, `input_singletrack_factory_t`, `input_decoder_v4`, and `advconfig_integer_factory_cached`.

---

## Credits

- Original plugin: [qwertymodo](https://github.com/qwertymodo/foo_input_msu) (2017)
- v2 x64 rewrite: Echo-Storm (2026)
- foobar2000 SDK: Peter Pawlowski / hydrogenaud.io
