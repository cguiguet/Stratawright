#!/usr/bin/env python3
"""Turn a recorded rhythm into a drum part.

Record the rhythm any way you like (beatbox, taps on a guitar body, on a table...). Each hit is
detected, sorted by how bright it sounds into up to three families (darkest = kick, middle =
snare, brightest = closed hi-hat, adapting to whatever made the sound), snapped to the grid,
and written as notes on a new MIDI track playing the built-in GM Drums.

    python3 scripts/rhythm_to_drums.py --track 2 --clip 5            # an audio clip of the open project
    python3 scripts/rhythm_to_drums.py --track 2 --clip 5 --dry-run  # only print the pattern
    python3 scripts/rhythm_to_drums.py --wav take.wav --dry-run      # a file, placed at the start

Standard library only. Talks to the running DAW through `daw-cli` ($DAW_CLI overrides the path).
"""

import argparse
import json
import math
import os
import struct
import subprocess
import sys

KICK, SNARE, HIHAT = 36, 38, 42
TICKS_PER_BEAT = 960
HOP = 256
MIN_GAP_SECONDS = 0.06
FEATURE_SECONDS = 0.04


# --- daw-cli -------------------------------------------------------------------------------

def daw_cli(*args):
    command = [os.environ.get("DAW_CLI", "daw-cli"), *args]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"daw-cli {' '.join(args)} failed: {(result.stderr or result.stdout).strip()}")
    return result.stdout


def daw_rows(*args):
    """Rows of a list command, parsed from its TSV output."""
    lines = [line for line in daw_cli(*args).splitlines() if line.strip()]
    if not lines or "\t" not in lines[0]:
        return []
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:] if "\t" in line]


def project_tempo():
    rows = daw_rows("status")
    if not rows:
        return 120.0, 4
    bpm = float(rows[0].get("TEMPO", "120"))
    beats_per_bar = int(rows[0].get("TIME_SIGNATURE", "4/4").split("/")[0])
    return bpm, beats_per_bar


# --- audio ---------------------------------------------------------------------------------

def read_wav(path):
    """Mono float samples and sample rate of a PCM (16/24/32-bit) or float32 WAV file."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{path} is not a WAV file")
    fmt, payload, position = None, None, 12
    while position + 8 <= len(data):
        chunk, size = data[position:position + 4], struct.unpack_from("<I", data, position + 4)[0]
        if chunk == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", data, position + 8)
            if fmt[0] == 0xFFFE:  # WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format GUID
                fmt = (struct.unpack_from("<H", data, position + 8 + 24)[0],) + fmt[1:]
        elif chunk == b"data":
            payload = data[position + 8:position + 8 + size]
        position += 8 + size + (size & 1)
    if not fmt or payload is None:
        raise SystemExit(f"{path}: missing fmt or data chunk")

    tag, channels, rate, _, _, bits = fmt
    width = bits // 8
    frames = len(payload) // (width * channels)
    if tag == 3 and bits == 32:
        values = struct.unpack(f"<{frames * channels}f", payload[:frames * channels * 4])
    elif tag == 1 and bits == 16:
        values = [v / 32768.0 for v in struct.unpack(f"<{frames * channels}h", payload[:frames * channels * 2])]
    elif tag == 1 and bits == 32:
        values = [v / 2147483648.0 for v in struct.unpack(f"<{frames * channels}i", payload[:frames * channels * 4])]
    elif tag == 1 and bits == 24:
        values = []
        for i in range(0, frames * channels * 3, 3):
            v = payload[i] | (payload[i + 1] << 8) | (payload[i + 2] << 16)
            values.append((v - (1 << 24) if v & 0x800000 else v) / 8388608.0)
    else:
        raise SystemExit(f"{path}: unsupported WAV format (tag {tag}, {bits} bits)")

    if channels == 1:
        return list(values), rate
    return [sum(values[f * channels:(f + 1) * channels]) / channels for f in range(frames)], rate


def analyse(samples, rate):
    """Per-hop energy, zero crossings and low-band energy."""
    alpha = 1.0 - math.exp(-2.0 * math.pi * 200.0 / rate)  # one-pole low-pass at 200 Hz
    low, previous = 0.0, 0.0
    energy, crossings, low_energy = [], [], []
    for start in range(0, len(samples) - HOP, HOP):
        e = z = l = 0.0
        for x in samples[start:start + HOP]:
            e += x * x
            low += alpha * (x - low)
            l += low * low
            if (x < 0.0) != (previous < 0.0):
                z += 1
            previous = x
        energy.append(e)
        crossings.append(z)
        low_energy.append(l)
    return energy, crossings, low_energy


def detect_hits(samples, rate):
    """Onsets as dicts with sample position, brightness and peak level."""
    energy, crossings, low_energy = analyse(samples, rate)
    if not energy:
        return []
    loudest = max(energy)
    gate = loudest * 10 ** (-40 / 10)  # ignore anything 40 dB under the loudest hop
    eps = loudest * 1e-9 + 1e-20
    flux = []
    for k in range(len(energy)):
        before = sum(energy[max(0, k - 3):k]) / max(1, k - max(0, k - 3))  # silence before the first hop
        flux.append(max(0.0, math.log10(energy[k] + eps) - math.log10(before + eps)) if energy[k] > gate else 0.0)

    min_gap = max(1, int(MIN_GAP_SECONDS * rate / HOP))
    span = max(1, int(FEATURE_SECONDS * rate / HOP))
    hits, last = [], -min_gap
    for k in range(len(flux) - 1):
        if flux[k] < 0.5 or (k > 0 and flux[k] < flux[k - 1]) or flux[k] < flux[k + 1] or k - last < min_gap:
            continue
        window = range(k, min(len(energy), k + span))
        total = sum(energy[j] for j in window) + eps
        zcr = sum(crossings[j] for j in window) / (len(window) * HOP)
        low_share = sum(low_energy[j] for j in window) / total
        start = k * HOP
        peak = max((abs(x) for x in samples[start:start + span * HOP]), default=0.0)
        hits.append({"sample": start, "brightness": math.log(zcr + 1e-4) - 2.0 * min(low_share, 1.0), "peak": peak})
        last = k
    return hits


def kmeans_1d(values, k, rounds=50):
    ordered = sorted(values)
    centers = [ordered[int((i + 0.5) * len(ordered) / k)] for i in range(k)]
    labels = [0] * len(values)
    for _ in range(rounds):
        labels = [min(range(k), key=lambda c: abs(v - centers[c])) for v in values]
        for c in range(k):
            members = [v for v, label in zip(values, labels) if label == c]
            if members:
                centers[c] = sum(members) / len(members)
    spread = sum((v - centers[label]) ** 2 for v, label in zip(values, labels))
    return labels, centers, spread


def classify(hits):
    """GM drum note per hit: 1 to 3 brightness families, darkest = kick, brightest = hi-hat."""
    values = [hit["brightness"] for hit in hits]
    if len(values) < 2:
        return [SNARE] * len(values)
    _, _, spread1 = kmeans_1d(values, 1)
    labels2, centers2, spread2 = kmeans_1d(values, 2)
    best = (labels2, centers2, (KICK, SNARE)) if spread2 < 0.5 * spread1 else None
    if len(values) >= 6:
        labels3, centers3, spread3 = kmeans_1d(values, 3)
        if spread3 < 0.6 * spread2 and spread2 < 0.5 * spread1:
            best = (labels3, centers3, (KICK, SNARE, HIHAT))
    if best is None:
        return [SNARE] * len(values)
    labels, centers, notes = best
    rank = {cluster: position for position, cluster in enumerate(sorted(range(len(centers)), key=lambda c: centers[c]))}
    return [notes[rank[label]] for label in labels]


# --- pattern -------------------------------------------------------------------------------

def to_pattern(hits, notes, rate, bpm, beats_per_bar, grid, start_frame, source_offset, length):
    step = 4.0 / grid  # beats per grid step (grid 16 = sixteenth notes)
    loudest = {note: max((h["peak"] for h, n in zip(hits, notes) if n == note), default=1.0) for note in set(notes)}
    pattern = {}
    for hit, note in zip(hits, notes):
        offset = hit["sample"] - source_offset
        if offset < 0 or (length and offset >= length):
            continue  # outside the part of the file the clip plays
        beats = (start_frame + offset) / rate * bpm / 60.0
        beats = round(beats / step) * step
        velocity = int(55 + 72 * min(1.0, hit["peak"] / (loudest[note] or 1.0)))
        key = (note, round(beats, 6))
        pattern[key] = max(velocity, pattern.get(key, 0))
    events = []
    for (note, beats), velocity in sorted(pattern.items(), key=lambda item: (item[0][1], item[0][0])):
        bar, rest = divmod(beats, beats_per_bar)
        beat, fraction = divmod(rest, 1.0)
        tick = int(round(fraction * TICKS_PER_BEAT))
        events.append({"note": note, "velocity": velocity, "beats": beats,
                       "position": f"{int(bar) + 1}.{int(beat) + 1}.{tick}"})
    return events


def write_track(events, beats_per_bar, name, kit):
    daw_cli("track", "create", "--name", name, "--type", "midi", "--color", "orange")
    track = next(row["ID"] for row in reversed(daw_rows("track", "list")) if row.get("NAME") == name)
    daw_cli("plugin", "add", "--track", track, "--name", "GM Drums")
    if kit:
        daw_cli("plugin", "set-param", "--track", track, "--plugin", "instrument", "--param", "0", "--val", f"{kit / 127:.4f}")
    first_bar = int(events[0]["beats"] // beats_per_bar) + 1
    last_bar = int(events[-1]["beats"] // beats_per_bar) + 1
    daw_cli("clip", "add-midi", "--track", track, "--start", f"{first_bar}.1.0", "--dur", f"{last_bar - first_bar + 1}.0.0")
    clip = next(row["CLIP_ID"] for row in daw_rows("clip", "list", "--track", track) if row.get("TYPE") == "MIDI")
    for event in events:
        daw_cli("midi", "add-note", "--track", track, "--clip", clip, "--pitch", str(event["note"]),
                "--velocity", str(event["velocity"]), "--channel", "10", "--start", event["position"], "--dur", "0.0.120")
    return track


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--track", help="track of the audio clip to transcribe")
    parser.add_argument("--clip", help="audio clip id (see `daw-cli clip list --track N`)")
    parser.add_argument("--wav", help="transcribe this file instead, placed at --start-frame")
    parser.add_argument("--start-frame", type=int, default=0, help="timeline frame where --wav starts")
    parser.add_argument("--bpm", type=float, help="tempo (default: the project's)")
    parser.add_argument("--grid", type=int, default=16, help="snap to 1/N notes (default 16)")
    parser.add_argument("--name", default="Drums", help="name of the new drum track")
    parser.add_argument("--kit", type=int, default=0, help="GM drum kit: 0 standard, 8 room, 16 power, 25 TR-808...")
    parser.add_argument("--dry-run", action="store_true", help="print the pattern, change nothing")
    args = parser.parse_args()

    start_frame, source_offset, length = args.start_frame, 0, 0
    if args.clip:
        if not args.track:
            parser.error("--clip needs --track")
        clip = next((row for row in daw_rows("clip", "list", "--track", args.track) if row.get("CLIP_ID") == args.clip), None)
        if not clip or not clip.get("PATH"):
            raise SystemExit(f"audio clip {args.clip} not found on track {args.track}")
        path = clip["PATH"]
        start_frame = int(clip["START_FRAME"])
        source_offset = int(clip.get("SOURCE_OFFSET_FRAMES", "0"))
        length = int(clip["DURATION_FRAMES"])
    elif args.wav:
        path = args.wav
    else:
        parser.error("give --track/--clip or --wav")

    if args.bpm:
        bpm, beats_per_bar = args.bpm, 4
    elif args.dry_run and args.wav:
        bpm, beats_per_bar = 120.0, 4
    else:
        bpm, beats_per_bar = project_tempo()

    samples, rate = read_wav(path)
    hits = detect_hits(samples, rate)
    if not hits:
        raise SystemExit("no hits found in the recording")
    notes = classify(hits)
    events = to_pattern(hits, notes, rate, bpm, beats_per_bar, args.grid, start_frame, source_offset, length)
    if not events:
        raise SystemExit("no hits inside the clip")

    names = {KICK: "kick", SNARE: "snare", HIHAT: "hi-hat"}
    counts = {names[n]: sum(1 for e in events if e["note"] == n) for n in names if any(e["note"] == n for e in events)}
    summary = {"tempo": bpm, "grid": f"1/{args.grid}", "hits": counts,
               "from": events[0]["position"], "to": events[-1]["position"]}
    if args.dry_run:
        print(json.dumps({**summary, "notes": [{k: e[k] for k in ("position", "note", "velocity")} for e in events]}, indent=1))
        return
    track = write_track(events, beats_per_bar, args.name, args.kit)
    print(json.dumps({**summary, "track": track}))


if __name__ == "__main__":
    sys.exit(main())
