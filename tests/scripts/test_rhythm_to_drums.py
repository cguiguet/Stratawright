"""Regression test for scripts/rhythm_to_drums.py on a synthetic, slightly loose rock beat."""

import json
import math
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

SCRIPT = os.path.join(os.path.dirname(__file__), "..", "..", "scripts", "rhythm_to_drums.py")
RATE, BEAT = 44100, 0.5  # 120 BPM


def write_beat(path, seed):
    """4 bars: kick on 1 and 3, snare on 2 and 4, hi-hat on every off-beat, each hit up to 15 ms off."""
    random.seed(seed)
    buf = [0.0] * int(RATE * 8.5)

    def add(start, generator, duration):
        first = max(0, int(start * RATE))
        for i in range(int(duration * RATE)):
            if first + i < len(buf):
                buf[first + i] += generator(i / RATE)

    def kick(t):
        return math.sin(2 * math.pi * (55 + 90 * math.exp(-t * 35)) * t) * math.exp(-t * 14) * 0.9

    def snare(t):
        return ((random.random() * 2 - 1) * 0.5 + 0.3 * math.sin(2 * math.pi * 190 * t)) * math.exp(-t * 22)

    last = [0.0]

    def hihat(t):
        white = random.random() * 2 - 1
        out, last[0] = white - last[0], white
        return out * math.exp(-t * 90) * 0.35

    expected = set()
    for bar in range(4):
        for beat in range(4):
            start = (bar * 4 + beat) * BEAT
            jitter = lambda: max(-start, random.uniform(-0.015, 0.015))  # noqa: E731
            if beat in (0, 2):
                add(start + jitter(), kick, 0.4)
                expected.add((f"{bar + 1}.{beat + 1}.0", 36))
            else:
                add(start + jitter(), snare, 0.25)
                expected.add((f"{bar + 1}.{beat + 1}.0", 38))
            add(start + BEAT / 2 + jitter(), hihat, 0.08)
            expected.add((f"{bar + 1}.{beat + 1}.480", 42))

    peak = max(abs(x) for x in buf)
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", int(x / peak * 0.8 * 32767)) for x in buf))
    return expected


class RhythmToDrumsTest(unittest.TestCase):
    def test_transcribes_a_loose_rock_beat(self):
        for seed in (3, 11):
            with tempfile.TemporaryDirectory() as directory:
                path = os.path.join(directory, "beat.wav")
                expected = write_beat(path, seed)
                output = subprocess.run([sys.executable, SCRIPT, "--wav", path, "--bpm", "120", "--dry-run"],
                                        capture_output=True, text=True, check=True).stdout
                notes = {(n["position"], n["note"]) for n in json.loads(output)["notes"]}
                self.assertEqual(notes, expected, f"seed {seed}")


if __name__ == "__main__":
    unittest.main()
