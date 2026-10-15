# Dev audio on Linux — the ALSA Loopback workflow

> Status: **working runbook** for testing `raw-radio-studio` with a *real* audio
> stream on Linux dev/build machines. Written and **verified** against the RAW
> Radio build box (Linux Mint 22 / Ubuntu 24.04 base, kernel
> `6.8.0-147-generic` / `6.8.0-147`, WirePlumber **0.4.17**, PipeWire 1.0.5).
>
> On that box the `snd-aloop` module is **already loaded and configured for
> autoload** by the owner, so the privileged step (§3) is done — the unprivileged
> helper (`scripts/dev/alsa-loopback.sh`) is all that remains.

## 1. Why a loopback device at all

The studio's I/O policy on Linux is **direct ALSA hardware, exclusive, no
fallback** (NFR-IO-4). It opens the raw `hw:` PCM, never the PipeWire/PulseAudio
plugin path, and it refuses to silently take a software-routed device
(`src/studio/DeviceError.h:100-124`, `isAlsaPluginPseudoDeviceName`).

That policy makes a physical interface awkward for CI / headless / hand-off
testing: you cannot drive it with a synthetic signal, and opening it locks the
real hardware. `snd-aloop` (kernel module) provides a **virtual sound card whose
output is wired to its own input**, entirely inside the kernel:

```
producer (aplay / synth / test) ──► hw:Loopback,0,N  ┐
                                                      ├─ (in-kernel ring buffer)
studio capture  ◄─── record ◄── meters ◄── hw:Loopback,1,N ┘
```

So you get a real PCM to open (record → meters → waveform → latency) that no
other daemon holds, without touching the audio interface and **without stopping
PipeWire**.

`snd-dummy` is *not* a substitute: it can be played to or recorded from, but you
cannot inject your own signal into the capture side.

## 2. Why PipeWire has to be told to let go  — and how (0.4.x glob trap)

`hw` is exclusive: if WirePlumber/PipeWire claims the Loopback card, the studio's
`open()` fails with **`EBUSY`** (`classifyDeviceError`, `DeviceError.h:33`). We
therefore add a WirePlumber rule that disables the Loopback card so it never
becomes a PipeWire node. The rule only matches the Loopback card — every other
device keeps its normal behaviour.

`scripts/dev/alsa-loopback.sh` installs the correct rule for the installed
WirePlumber version (idempotent, no root):

* **WirePlumber 0.5+** → `~/.config/wireplumber/wireplumber.conf.d/51-disable-aloop.conf`
  (SPA-JSON, `monitor.alsa.rules` + `actions.update-properties."device.disabled"`).
* **WirePlumber 0.4.x** (this box, 0.4.17) → `~/.config/wireplumber/main.lua.d/51-disable-aloop.lua`
  (Lua, `alsa_monitor.rules`). 0.4.x does **not** read `wireplumber.conf.d/`.

> **⚠️ 0.4.x gotcha — `matches` is GPatternSpec _glob_, not regexp.** In WP 0.4.x
> Lua rules the `matches` verb is matched with GLib `GPatternSpec`, not POSIX ERE:
> `"."` is a **literal** dot, `"*"` = any run, `"?"` = one char, and character
> classes like `"[Ll]"` and a leading `".*"` do **not** behave like regex. The
> original rule used `alsa_card.*[Ll]oopback.*` / `.*[Ll]oopback.*`, which never
> matched anything (verified on this box). Two independent bugs:
> 1. glob semantics (`[..]` / leading `.*` don't work); and
> 2. the card's ALSA name is **`snd_aloop`** (`device.name` =
>    `alsa_card.platform-snd_aloop.0`), which never contained the substring
>    `loopback` anyway.
>
> The working rule uses globs: `alsa_card*snd_aloop*` (plus `*Loopback*` on
> `device.nick`/`device.description` as fallbacks). `device.disabled = true` in
> `apply_properties` then makes `prepareDevice()` return before the card becomes a
> PipeWire node. **Result on this box: `Loopback` is absent from `wpctl status`
> and `lsmod` shows `snd_aloop … 0` (nothing holding it).**

After changing the rule:

```bash
systemctl --user restart wireplumber
wpctl status            # Loopback must NOT appear among Devices/Sinks/Sources
lsmod | grep snd_aloop  # refcount on the right (last) column should be 0
```

## 3. Loading the module (the only sudo step)

The module ships with the kernel but is not autoloaded by default. The owner runs:

```bash
sudo modprobe snd-aloop
# persist across reboots (optional but recommended on a build box):
echo snd-aloop | sudo tee /etc/modules-load.d/snd-aloop.conf
```

> `scripts/dev/alsa-loopback.sh` never calls `sudo`; it prints exactly these
> two lines and exits `1` until the card appears.

**Done on the build box** (verified `2026-10-15`):

```
$ lsmod | grep aloop
snd_aloop              45056  0
$ cat /etc/modules-load.d/snd-aloop.conf
snd-aloop
$ cat /proc/asound/cards
 0 [Loopback       ]: Loopback - Loopback
                      Loopback 1
 1 [PCH            ]: HDA-Intel - HDA Intel PCH
 2 [NVidia         ]: HDA-Intel - HDA NVidia
```

`snd-aloop` registers a card named **`Loopback`**, exposing **two full-duplex PCM
devices (index 0 and 1), each with 8 subdevices** — both appear under `aplay -l`
*and* `arecord -l`. Up to 8 cards can be registered (`Loopback`, `Loopback_1`, …)
via `modprobe snd-aloop enable=1,1,1,1,1,1,1,1`; the default is fine for dev.

The user must be able to open `/dev/snd/*`. On the build box `il` is in the
`audio` group **and** has a per-session logind ACL:

```
$ id -nG
il adm cdrom sudo audio dip plugdev users lpadmin sambashare ollama
$ getfacl /dev/snd/pcmC0D0p
user:il:rw-      # ACL from the active graphical session
```

A headless/SSH session that has no seat ACL will fail with `Permission denied`
unless the user is in the `audio` group (see §7).

## 4. Endpoint orientation  — measured, not guessed

The two loopback PCM devices are **crossed**, and each is full-duplex:

```
aplay   -D hw:Loopback,0,N   →   arecord -D hw:Loopback,1,N     # works
aplay   -D hw:Loopback,1,N   →   arecord -D hw:Loopback,0,N     # works
aplay   -D hw:Loopback,0,N   →   arecord -D hw:Loopback,0,N     # SILENT
aplay   -D hw:Loopback,1,N   →   arecord -D hw:Loopback,1,N     # SILENT
```

Measured on the build box with a 0.5-amplitude 1 kHz tone, `S32_LE`, 48 kHz,
stereo, subdevice 0 (normalised RMS of the captured WAV):

| play | record | RMS |
|---|---|---|
| `hw:Loopback,0,0` | `hw:Loopback,1,0` | **0.3223** |
| `hw:Loopback,1,0` | `hw:Loopback,0,0` | **0.3224** |
| `hw:Loopback,0,0` | `hw:Loopback,0,0` | 0.0000 |
| `hw:Loopback,1,0` | `hw:Loopback,1,0` | 0.0000 |

So the **verified working pair is play device 0 → capture device 1** (and the
mirror image). The helper script's `--test` mode uses `0,0 → 1,0` and reports the
RMS; `--scan` tries all four combinations:

```bash
scripts/dev/alsa-loopback.sh --test
scripts/dev/alsa-loopback.sh --scan
```

Manual equivalent (Python stdlib only, no extra packages):

```bash
python3 - tone.wav <<'PY'
import wave, math, struct
sr, secs, amp = 48000, 3, int(0.5 * (2**31 - 1))
with wave.open("tone.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(4); w.setframerate(sr)   # S32_LE
    frames = bytearray()
    for i in range(sr * secs):
        s = int(amp * math.sin(2 * math.pi * 1000 * i / sr))
        frames += struct.pack("<i", s) * 2
    w.writeframes(bytes(frames))
PY

arecord -D hw:Loopback,1,0 -f S32_LE -r 48000 -c 2 -d 3 out.wav &   # capture first
sleep 0.5
aplay   -D hw:Loopback,0,0 -f S32_LE -r 48000 -c 2 tone.wav
wait
```

A non-silent `out.wav` (RMS ≈ 0.32 for the tone above) confirms the pair. Silence
means you are on a same-index pair.

### Continuous tone for a live studio session

To keep a signal flowing while the studio is open, play a long tone into the
injection end and record from the other:

```bash
scripts/dev/alsa-loopback.sh --tone 60      # 1 kHz for 60 s; Ctrl-C to stop
```

## 5. Verifying the studio accepts it

`--list-devices` prints every backend and its input/output names headlessly:

```bash
build/raw_radio_studio_artefacts/Release/raw-radio-studio --list-devices
```

**Verified on the build box (`2026-10-15`): exit code `0`, returns immediately
(no hang), and the Loopback card _is_ listed** under `[ALSA]` for both inputs and
outputs, e.g.:

```
* [ALSA]
    inputs:
      - "Loopback, Loopback PCM; Direct hardware device without any conversions (1)"
      - "Loopback, Loopback PCM; Direct hardware device without any conversions (2)"
      - "HDA Intel PCH, ALC897 Analog; Direct hardware device without any conversions"
      …
```

It is not a "pseudo device": the filter in `src/studio/DeviceError.h:100-124`
only rejects `Default ALSA*`, `Pulseaudio*`, and names containing `pipewire` /
`pulseaudio` / `jack` / `sound server` / `dmix` / `dsnoop` / `direct sample
mixing|snooping`. Note the loopback card also exposes `…; Direct sample
snooping device (N)` (capture) and `…; Direct sample mixing device (N)` (playback)
entries — those *are* correctly filtered out; the plain `…; Direct hardware
device without any conversions (N)` entries are **accepted**, so
`isAcceptableInputDeviceName` / `isAcceptableOutputDeviceName` return `true` for
the loopback card.

A working device selection was already persisted on the build box in
`~/.config/raw-radio-studio/Settings.xml`:

```xml
<VALUE name="audio_device_setup">
  <DEVICESETUP deviceType="ALSA"
    audioOutputDeviceName="Loopback, Loopback PCM; Direct hardware device without any conversions (1)"
    audioInputDeviceName="Loopback, Loopback PCM; Direct hardware device without any conversions (1)"
    audioDeviceRate="48000.0" audioDeviceBufferSize="512"/>
```

> Caveat to watch: this saved setup names the **same** descriptor `(1)` for both
> input and output. If Tracktion opens a single `hw:Loopback,0` full-duplex PCM,
> output and capture end up on the **same** loopback index — which §4 shows is
> silent. For a real round-trip, select the **crossed** ends (output `0`, input
> `1`) in the device panel. (Not blocking device enumeration; noted for testing.)

## 6. Limitations

* The Loopback card is **not a physical output** — it has no speakers/monitors.
  Use it for capture/injection testing, not for listening.
* Loopback **latency is essentially zero** (in-kernel); do **not** use it to
  measure real round-trip latency. For RT-latency acceptance you still need a
  real interface (Epic 7 hardware acceptance).
* One clock / one device at a time still holds: the studio opens a single input
  and a single output.
* `snd-aloop` is a software device: xrun behaviour under load is not
  representative of real hardware.

## 7. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `EBUSY` opening the Loopback card | PipeWire claimed it → WirePlumber rule missing/**not matching**. Check `wpctl status` (Loopback must be absent) and `lsmod \| grep snd_aloop` (refcount 0). Re-apply + `systemctl --user restart wireplumber`. |
| Rule written but Loopback still in `wpctl status` | On **0.4.x** the `matches` verb is **glob**, not regex: `[Ll]` and a leading `.*` never match, and the card is `snd_aloop` (no `loopback` substring). Use `alsa_card*snd_aloop*`. Confirm the file is loaded: `journalctl --user -u wireplumber \| grep 'loading config file'`. |
| A malformed Lua rule makes `wireplumber` exit and systemd give up | A runtime error in `main.lua.d/*.lua` is **fatal** (`status=70`). Fix the file, then `systemctl --user reset-failed wireplumber && systemctl --user restart wireplumber`. Note: in the config context `Log` is **not** defined — debug-print with `print(...)` (visible in the unit's journal), not `Log.*`. |
| `aplay -l` prints "no sound cards" / `open /dev/snd/...: Permission denied` | The user is not in the `audio` group and has no logind ACL (e.g. a headless/SSH session where the seat's active session belongs to another user). Either log into the graphical desktop as the same user, or `sudo usermod -aG audio $USER` and re-login. |
| `--list-devices` hangs forever | Tracktion Engine's startup opens the **default** device; if ALSA `default` resolves to the PipeWire plugin and PipeWire can't start the stream, `snd_pcm_prepare` blocks in `libasound_module_pcm_pipewire.so`. A saved `audio_device_setup` pointing at a `hw:` device avoids it. (Seen on this box in an earlier headless session; **not** reproducible after the Loopback setup.) |
| Wrong pair sounds silent | The ends are **crossed**: use play `0,/N` → record `1,/N` (or vice-versa). `scripts/dev/alsa-loopback.sh --scan` prints RMS for all four pairs. |

## 8. Quick reference

```bash
# one-time (owner, root) — already done on the build box
sudo modprobe snd-aloop
echo snd-aloop | sudo tee /etc/modules-load.d/snd-aloop.conf

# everything unprivileged
scripts/dev/alsa-loopback.sh --check     # report state
scripts/dev/alsa-loopback.sh             # install WirePlumber rule + report
scripts/dev/alsa-loopback.sh --test      # 1 kHz round-trip (0,0 -> 1,0), prints RMS
scripts/dev/alsa-loopback.sh --scan      # try all 0/1 pairs
scripts/dev/alsa-loopback.sh --tone 60   # continuous 1 kHz for 60 s (studio records it)
```
