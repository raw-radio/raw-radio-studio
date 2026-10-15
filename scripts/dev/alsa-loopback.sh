#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# alsa-loopback.sh — ALSA Loopback (snd-aloop) dev helper for raw-radio-studio.
#
# Bootstraps a kernel-internal "loopback" sound card so the studio can be tested
# with a *real* audio stream on Linux (record → meters → waveform → latency),
# without a physical audio interface and without stopping PipeWire.
#
# Background — see docs/DEV_AUDIO_LINUX.md and src/studio/DeviceError.h:
#   * The studio opens ALSA hardware PCMs directly and exclusively ("hw", no
#     fallback). If PipeWire/WirePlumber claims the loopback card, the studio's
#     open() fails with EBUSY. So we first add a WirePlumber rule that keeps the
#     Loopback card out of PipeWire (no root needed for this).
#   * Loading the `snd-aloop` kernel module needs root. That is the ONLY
#     privileged step and this script never escalates — it prints the exact
#     one-liner for the owner to run instead.
#
# The script is idempotent: re-running changes nothing if already set up.
# It never modifies the studio sources.
#
# Verified on the RAW Radio build box (Linux Mint 22 / Ubuntu 24.04 base,
# kernel 6.8.0-147-generic, WirePlumber 0.4.17): with this rule applied the
# Loopback card no longer appears in `wpctl status`, and the round-trip below is
# non-silent (RMS ≈ 0.32 for a 0.5-amplitude 1 kHz tone).
#
# Usage:
#   scripts/dev/alsa-loopback.sh              install the WirePlumber rule + report
#   scripts/dev/alsa-loopback.sh --check      report only (change nothing)
#   scripts/dev/alsa-loopback.sh --test       round-trip a 1 kHz tone, print RMS
#   scripts/dev/alsa-loopback.sh --scan       try all 0/1 device pairs, report
#   scripts/dev/alsa-loopback.sh --tone [SEC] play a continuous 1 kHz tone into
#                                             the loopback for SEC seconds
#                                             (default 30) so the studio can
#                                             record it; Ctrl-C to stop early
#   scripts/dev/alsa-loopback.sh --help
#
# Exit status: 0 = loopback card present and usable; 1 = not yet (the owner must
# load the module, see the printed command). Nothing is destroyed on failure.
# -----------------------------------------------------------------------------
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mode="install"
arg2=""
case "${1:-}" in
    --check) mode="check" ;;
    --test)  mode="test" ;;
    --scan)  mode="scan" ;;
    --tone)  mode="tone" ;;
    "")      mode="install" ;;
    -h|--help)
        sed -n '2,39p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        exit 0 ;;
    *)
        echo "alsa-loopback.sh: unknown argument: $1" >&2
        echo "usage: $0 [--check|--test|--scan|--tone [SEC]]" >&2
        exit 2 ;;
esac
[[ -n "${2:-}" ]] && arg2="${2}"

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "alsa-loopback.sh: Linux only (ALSA). This host is $(uname -s)." >&2
    exit 2
fi

SUDO_HINT_MODPROBE='sudo modprobe snd-aloop'
SUDO_HINT_PERSIST='echo snd-aloop | sudo tee /etc/modules-load.d/snd-aloop.conf'

# Confirmed orientation on the build box: playback device 0 feeds capture device
# 1 (and vice versa); a device paired with itself is silent.
PLAY_DEV="0" ; REC_DEV="1" ; SUB="0"
RATE="48000" ; CH="2" ; FREQ="1000"
FMT="S32_LE"

# --- WirePlumber rule (keeps the Loopback card out of PipeWire) --------------
# WirePlumber 0.4.x uses Lua rules in ~/.config/wireplumber/main.lua.d/*.lua;
# WirePlumber 0.5+ uses SPA-JSON in ~/.config/wireplumber/wireplumber.conf.d/*.conf.
# Same intent, version-appropriate syntax (mirrors what this repo's dev box runs).
#
# IMPORTANT (0.4.x): the `matches` verb is GPatternSpec *glob*, NOT regexp and
# NOT POSIX ERE. "." is a literal dot, "*" = any run, "?" = one char; character
# classes like "[Ll]" and a leading ".*" do NOT match the way they do in regex.
# Also the ALSA card is named "snd_aloop" (device.name
# "alsa_card.platform-snd_aloop.0"), which never contains the substring
# "loopback". Hence the globs below (verified working).
WP_LUA='-- raw-radio-studio — keep ALSA Loopback (snd-aloop) out of PipeWire.
-- WirePlumber 0.4.x (Lua). For 0.5+ use wireplumber.conf.d/*.conf instead.
-- Why: the studio opens ALSA hardware PCMs exclusively ("hw", no fallback);
-- if PipeWire claims the Loopback card the studio open() fails with EBUSY.
-- NOTE: the 0.4.x "matches" verb is GPatternSpec GLOB (not regexp): "." is a
-- literal dot and "[..]" classes do not work; ALSA names the card "snd_aloop".
if alsa_monitor and alsa_monitor.rules then
  table.insert(alsa_monitor.rules, {
    matches = {
      { { "device.name", "matches", "alsa_card*snd_aloop*" } },
      { { "device.name", "matches", "alsa_card*snd-aloop*" } },
      { { "device.nick", "matches", "*Loopback*" } },
      { { "device.description", "matches", "*Loopback*" } },
    },
    apply_properties = {
      ["device.disabled"] = true,
    },
  })
end'

# 0.5+ SPA-JSON: here "~" means a *regexp* match (different engine than 0.4.x).
WP_CONF='# raw-radio-studio — keep ALSA Loopback (snd-aloop) out of PipeWire.
# WirePlumber 0.5+ (SPA-JSON). Disables the Loopback card so the studio can open
# it exclusively ("hw", no fallback) instead of hitting EBUSY.
# NOTE: in SPA-JSON the "~" prefix means REGEXP (unlike 0.4.x globs); snd-aloop
# registers the card as "snd_aloop"/"snd-aloop", so match both spellings.
monitor.alsa.rules = [
  {
    matches = [
      { device.name = "~alsa_card.*(snd[_-]?aloop|[Ll]oopback).*" }
      { device.nick = "~.*[Ll]oopback.*" }
      { device.description = "~.*[Ll]oopback.*" }
    ]
    actions = {
      update-properties = {
        "device.disabled" = true
      }
    }
  }
]'

write_if_changed() {
    local dest="$1" content="$2" current=""
    mkdir -p "$(dirname "${dest}")"
    if [[ -f "${dest}" ]]; then
        current="$(cat "${dest}")"
    fi
    if [[ "${current}" == "${content}" ]]; then
        echo "  unchanged: ${dest}"
    else
        printf '%s\n' "${content}" > "${dest}"
        echo "  wrote:     ${dest}"
    fi
}

install_wireplumber_rule() {
    if ! command -v wireplumber >/dev/null 2>&1; then
        echo "WirePlumber: not installed — skipping (PipeWire cannot claim the card without it)."
        return 0
    fi

    # "Compiled with libwireplumber 0.4.17" -> "0.4"
    local ver
    ver="$(wireplumber --version 2>/dev/null | sed -n 's/.*libwireplumber \([0-9]\+\.[0-9]\+\).*/\1/p' | head -1)"
    if [[ -z "${ver}" ]]; then
        echo "WirePlumber: could not parse version — writing the 0.4.x Lua rule anyway."
        ver="0.4"
    fi

    local ge05
    ge05="$(awk -v v="${ver}" 'BEGIN { split(v,a,"."); print (a[1] > 0 || a[2] >= 5) ? 1 : 0 }')"

    echo "WirePlumber: detected ${ver} ($([[ "${ge05}" == 1 ]] && echo '0.5+ SPA-JSON' || echo '0.4.x Lua'))"
    if [[ "${ge05}" == 1 ]]; then
        write_if_changed "${HOME}/.config/wireplumber/wireplumber.conf.d/51-disable-aloop.conf" "${WP_CONF}"
    else
        write_if_changed "${HOME}/.config/wireplumber/main.lua.d/51-disable-aloop.lua" "${WP_LUA}"
    fi
    echo "  apply with: systemctl --user restart wireplumber"
}

module_loaded() { lsmod | grep -q '^snd_aloop'; }
card_present()  { grep -qi 'loopback' /proc/asound/cards 2>/dev/null; }

report_hardware() {
    echo
    echo "Kernel module 'snd-aloop': $(module_loaded && echo 'loaded' || echo 'NOT loaded')"
    echo "Loopback ALSA card:        $(card_present && echo 'present' || echo 'absent')"
    if card_present; then
        echo
        echo "--- /proc/asound/cards ---"
        grep -i loopback /proc/asound/cards || true
        echo
        echo "--- aplay -l (playback ends) ---"
        aplay -l 2>/dev/null | grep -iA1 loopback || true
        echo
        echo "--- arecord -l (capture ends) ---"
        arecord -l 2>/dev/null | grep -iA1 loopback || true
        echo
        echo "snd-aloop wiring (verified): playback hw:Loopback,0,<N> feeds capture"
        echo "hw:Loopback,1,<N>, and playback hw:Loopback,1,<N> feeds capture"
        echo "hw:Loopback,0,<N> — the two ends are crossed; same-index is silent."
    fi
    if command -v wpctl >/dev/null 2>&1; then
        echo
        if wpctl status 2>/dev/null | sed -n '/Devices:/,/Sinks:/p' | grep -qi loopback; then
            echo "PipeWire: Loopback IS still claimed by WirePlumber — apply the rule:"
            echo "  scripts/dev/alsa-loopback.sh && systemctl --user restart wireplumber"
        else
            echo "PipeWire: Loopback is NOT among WirePlumber devices (good)."
        fi
    fi
}

# generate_tone <path> <secs> — stdlib-only WAV writer at $RATE/$CH/$FMT.
generate_tone() {
    local path="$1" secs="$2"
    python3 - "${path}" "${RATE}" "${CH}" "${secs}" "${FREQ}" <<'PY'
import sys, wave, math, struct
path, rate, ch, secs, freq = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
amp = int(0.5 * (2 ** 31 - 1))
try:
    from array import array
    buf = array('i')
    for i in range(int(rate * secs)):
        s = int(amp * math.sin(2 * math.pi * freq * i / rate))
        buf.extend([s] * ch)
    frames = buf.tobytes()
except Exception:
    frames = bytearray()
    for i in range(int(rate * secs)):
        s = int(amp * math.sin(2 * math.pi * freq * i / rate))
        frames += struct.pack('<i', s) * ch
    frames = bytes(frames)
with wave.open(path, 'wb') as w:
    w.setnchannels(ch); w.setsampwidth(4); w.setframerate(rate)
    w.writeframes(frames)
print('generated', path, int(rate * secs), 'frames', rate, 'Hz', '%dch' % ch, secs, 's')
PY
}

# rms_of <path> — print frames/format + normalised RMS (stdlib only, no audioop).
rms_of() {
    python3 - "${1}" <<'PY'
import sys, wave, array, math
try:
    w = wave.open(sys.argv[1], 'rb')
except Exception as e:
    print('RESULT: cannot open capture: %s (card busy / wrong device?)' % e); sys.exit(0)
n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
raw = w.readframes(n)
if not raw:
    print('RESULT: capture is EMPTY (wrong device pair?)'); sys.exit(0)
if sw == 4:
    a = array.array('i'); a.frombytes(raw); scale = float(2 ** 31)
elif sw == 2:
    a = array.array('h'); a.frombytes(raw); scale = float(2 ** 15)
else:
    print('RESULT: unsupported sample width %d' % sw); sys.exit(0)
if sys.byteorder == 'big':
    a.byteswap()
rms = math.sqrt(sum(float(x) * float(x) for x in a) / len(a)) / scale
verdict = 'non-silent OK' if rms > 0.01 else 'near-silent — check the device pair'
print('RESULT: %d frames %dch %d-bit @%dHz, RMS=%.4f (%s)' % (n, ch, sw * 8, sr, rms, verdict))
PY
}

# One round-trip: play <pdev> sub <SUB>, capture <rdev> sub <SUB>. Prints RMS.
# Returns 0 if non-silent.
one_roundtrip() {
    local pdev="$1" rdev="$2" out="$3"
    rm -f "${out}"
    arecord -D "hw:Loopback,${rdev},${SUB}" -f "${FMT}" -r "${RATE}" -c "${CH}" -d 3 "${out}" >/dev/null 2>&1 &
    local recpid=$!
    sleep 0.5
    aplay -D "hw:Loopback,${pdev},${SUB}" -f "${FMT}" -r "${RATE}" -c "${CH}" "${TONE}" >/dev/null 2>&1 || true
    wait "${recpid}" 2>/dev/null || true
    rms_of "${out}"
}

run_tone_test() {
    if ! card_present; then
        echo "Cannot test: the Loopback card is absent. Load the module first (see below)." >&2
        return 1
    fi
    local tmp
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp}"' RETURN
    TONE="${tmp}/tone.wav"

    echo "Tone: ${FREQ} Hz, ${RATE} Hz, ${CH}ch ${FMT}, 3s"
    echo "Pair (verified): play hw:Loopback,${PLAY_DEV},${SUB} ⇒ rec hw:Loopback,${REC_DEV},${SUB}"
    generate_tone "${TONE}" 3
    one_roundtrip "${PLAY_DEV}" "${REC_DEV}" "${tmp}/rec.wav"
}

run_scan() {
    if ! card_present; then
        echo "Cannot scan: the Loopback card is absent. Load the module first (see below)." >&2
        return 1
    fi
    local tmp
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp}"' RETURN
    TONE="${tmp}/tone.wav"
    echo "Scanning all Loopback device pairs (subdevice ${SUB}) …"
    generate_tone "${TONE}" 3 >/dev/null
    local p r
    for p in 0 1; do
        for r in 0 1; do
            local line out="${tmp}/rec_${p}_${r}.wav"
            line="$(one_roundtrip "${p}" "${r}" "${out}")"
            printf 'play hw:Loopback,%s,%s  ->  rec hw:Loopback,%s,%s : %s\n' "${p}" "${SUB}" "${r}" "${SUB}" "${line}"
        done
    done
    echo "(working pairs are the crossed ones: 0->1 and 1->0)"
}

run_tone_play() {
    if ! card_present; then
        echo "Cannot play tone: the Loopback card is absent. Load the module first (see below)." >&2
        return 1
    fi
    local secs="${arg2:-30}"
    case "${secs}" in ''|*[!0-9]*) echo "alsa-loopback.sh: --tone SEC must be a positive integer" >&2; return 2 ;; esac
    local tmp
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp}"' RETURN
    local tone="${tmp}/tone.wav"

    echo "Continuous ${FREQ} Hz tone for ${secs}s into the Loopback …"
    echo "  aplay  → hw:Loopback,${PLAY_DEV},${SUB}   (inject)"
    echo "  studio → record from hw:Loopback,${REC_DEV},${SUB}"
    generate_tone "${tone}" "${secs}"
    echo "Playing (Ctrl-C to stop) …"
    aplay -D "hw:Loopback,${PLAY_DEV},${SUB}" -f "${FMT}" -r "${RATE}" -c "${CH}" "${tone}" || true
}

echo "raw-radio-studio — ALSA Loopback dev helper"
echo "host: $(uname -n)  kernel: $(uname -r)"

if [[ "${mode}" != "check" && "${mode}" != "test" && "${mode}" != "scan" && "${mode}" != "tone" ]]; then
    install_wireplumber_rule
fi

report_hardware

if ! card_present; then
    cat <<EOF

==============================================================================
Loopback card not available yet. The remaining step needs root (the only one).
Run this one-liner (and, to persist across reboots, the second line):

    ${SUDO_HINT_MODPROBE}
    ${SUDO_HINT_PERSIST}

Why here: loading a kernel module is privileged; there is no unprivileged path.
After loading, re-run this script. If the card is present but the studio still
fails with EBUSY, confirm PipeWire dropped it (it must NOT be listed):

    wpctl status

Then feed a signal and record it (crossed ends, subdevice N):

    aplay   -D hw:Loopback,0,0 -f S32_LE -r 48000 -c 2 tone.wav
    arecord -D hw:Loopback,1,0 -f S32_LE -r 48000 -c 2 -d 3 out.wav

or let this script do the round-trip / scan / continuous tone:

    $0 --test          # single verified pair, prints RMS
    $0 --scan          # try all 0/1 pairs
    $0 --tone 60       # play a continuous tone for 60s for the studio to record

Finally confirm the studio sees it as a real device (Loopback must appear):

    build/raw_radio_studio_artefacts/Release/raw-radio-studio --list-devices
==============================================================================
EOF
    exit 1
fi

if [[ "${mode}" == "test" ]]; then
    run_tone_test
elif [[ "${mode}" == "scan" ]]; then
    run_scan
elif [[ "${mode}" == "tone" ]]; then
    run_tone_play
fi

echo
echo "Loopback is ready. See docs/DEV_AUDIO_LINUX.md for the full workflow."
exit 0
