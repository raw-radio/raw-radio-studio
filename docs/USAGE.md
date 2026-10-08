# Using raw-radio-studio

A short, task-oriented guide to the features that exist today. It uses the
**actual UI names** shown in the app (button labels, panel titles, tooltips).

All actions below are on the main window unless a panel is named. Sessions are
Tracktion Engine projects saved as `.tracktionedit` files; the default location
is the app's `Projects/` folder (see [Where things live](#where-things-live)).

> **Status: pre-alpha.** Menus and labels can shift. This guide tracks the
> current build.

Russian translation: [`USAGE_RU.md`](USAGE_RU.md) (the English guide above is
authoritative if they diverge).

---

## 1. Open, save, and recall a project

The session buttons are the first group in the action row (icon-only, hover for
the tooltip):

| Button | Icon | Action |
| --- | --- | --- |
| **New** | `file-plus` | New session (Save/Discard/Cancel prompt if the current one has unsaved changes). |
| **Open** | `folder-open` | Open a `.tracktionedit` session. |
| **Save** | `floppy-disk` | Save in place. |
| **Save As** | `floppy-disk-back` | Save to a new `.tracktionedit` file. |
| **Close** | `x` | Close the session (same unsaved-changes prompt). |

Sessions autosave periodically (interval in **Settings**); if the previous run
was interrupted, a **Session recovery** dialog offers **Restore** or **Discard**.
Recorded takes live in a `Recordings/` folder next to the session file.

---

## 2. Plugins

VST3 is always available; **LV2** is opt-in at build time; **AU** on macOS only.
VST2 is never shipped.

1. Click **Plugins** in the action row (`waveform` icon) to open the **Plugins**
   panel.
2. In the panel, choose the destination track in the track list at the top,
   then press **Scan** to scan plugin folders (scanning runs out of process, so a
   crashing plugin cannot take the app down). Use **Refresh** to re-read the list
   from the known plugins.
3. Type in the search field (**Search plugins (name, maker, format)**) to filter
   the **Available plugins** list. Select a plugin row.
4. Press **Insert** (or double-click the row) to insert it on the selected track.
   The plugin is part of the Edit and is saved with the project.
5. Select the plugin in the **Track plugin** drop-down and press **Open** to show
   its editor window. **Remove** deletes it from the track.
6. **Save** / **Save As** the project to keep the inserted plugin and its state.

Tooltips on every button describe the action; the status line at the bottom of
the list shows scan progress and the number of known plugins.

---

## 3. Presets (user presets for a hosted plugin)

The **Presets** section is in the lower half of the **Plugins** panel:

- **Preset** + drop-down + **Load preset** — pick a saved preset and load it into
  the selected track plugin.
- **New** + name field + **Save preset** — type a name and save the plugin's
  current state.

Steps to save: open the **Plugins** panel, insert/select the track plugin, type a
name in the **New** field, press **Save preset**. The name must be non-empty and
must not contain path separators.

Steps to load: select a name in the **Preset** drop-down, press **Load preset**.

**Where files live.** Each hosted plugin gets its own folder, and each preset is
one file:

```
<app data>/raw-radio-studio/Presets/<plugin-key>/<preset-name>.rrspreset
```

On macOS `<app data>` is `~/Library/Application Support`; on Linux it is
`~/.config`. The `<plugin-key>` is a filesystem-safe key derived from the
plugin's name/format/identifier. Presets are raw plugin state, so they are only
meaningful for the same plugin.

---

## 4. Routing

Click **Routing** in the action row (`headphones` icon) to open the
**Routing & Cue Mixes** panel. Pick the track in the track drop-down at the top;
its controls update to match.

### Assign a track's output to a device

Use the output drop-down on the track row (tooltip **Hardware output pair this
track plays to**). It lists **Default output** plus each available hardware
output pair. Choosing a device routes that track to it; **Default output**
returns it to the main out. The **Send** toggle/slider on this row is the
selected track's send into the selected cue (see §5).

### Submix / folder track (bus or group)

The **Submix folder** section:

1. Press **New bus** to create a submix folder (named by the app).
2. Select the track and the bus, then press **Assign** to move the track into the
   submix. Its audio now flows through the bus to the master.
3. Press **Unassign** to take the track back out to the top level.

### Aux send / return

Aux send/return is implemented as a **software cue mix** (see §5): **Add cue**
creates an aux-return bus, and each track's **Send** feeds it. The return track
appears in the routing track list marked **`(cue)`** and has its own output
drop-down so it can be routed to a separate hardware pair.

---

## 5. Cue mixes

A cue mix is a separate monitor mix for a performer (FR-MON-3/4), built as an
aux send/return.

1. Click **Routing**.
2. In the cue section press **Add cue** to create a cue. This adds an aux-return
   track that is fed by per-track sends and, when a spare output pair exists,
   routes the return to the next free hardware output (otherwise it falls back to
   the main output — the status line reports the counts).
3. Select a track, enable the **Send** toggle, and set the **Send** slider (dB).
   That track is now audible in the cue.
4. Use the cue's output drop-down (tooltip **Hardware output pair the cue is sent
   to**) to send the cue to a specific hardware pair.
5. **Remove cue** deletes the cue and its return.

> **Current limitation.** The UI sets **one send at a time** (the selected track
> into the selected cue). To give a performer a genuinely separate physical mix
> you need an audio interface with **multiple output pairs**; on a single stereo
> output the cue falls back to the main out and cannot be separated. Set the
> send levels for each track in turn.

---

## 6. Stems export

**Export stems** is in the action row (text label next to the `download-simple`
icon; tooltip **Export stems: one 24-bit WAV per track plus the master mix…**).

1. Press **Export stems**.
2. Choose a destination folder in the file chooser.
3. The app renders, into that folder:
   - one **`<Track name>.wav`** per track that has clips (the track's own output,
     pre-master), and
   - one **`<Session name> master.wav`** (the full mix through the master chain).

All files are **24-bit** at the session sample rate (the open device's rate, or
the 48 kHz default when no device is open). Rendering runs offline/faster than
real time; the app returns to normal playback and monitoring when it finishes.

---

## 7. Region export

1. **Shift-drag on the timeline ruler** at the top of the arrangement to select a
   region. The readout under the ruler shows the start/end and length, and the
   region is shaded across the lanes.
2. Press **Export region** in the action row (`download-simple` icon; tooltip
   **Export the selected region to 24-bit WAV (Shift-drag on the ruler to
   select)**).
3. Choose the destination file. The exported WAV contains exactly the selected
   span, 24-bit at the session rate.
4. Press **Clear region** to drop the selection.

For a whole-session export, press **Export WAV** instead (no selection needed).

---

## Where things live

| Thing | Location |
| --- | --- |
| Sessions (`.tracktionedit`) | `<app data>/raw-radio-studio/Projects/` |
| Recorded takes | `Recordings/` next to the session file |
| Autosave temp version | `.tmp_<session name>` next to the session file |
| Plugin presets | `<app data>/raw-radio-studio/Presets/<plugin-key>/<name>.rrspreset` |

`<app data>` is macOS `~/Library/Application Support`, Linux `~/.config`.
