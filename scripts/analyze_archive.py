#!/usr/bin/env python3
"""Measure a Ninjam server session archive.

The server writes every interval it receives to disk verbatim -- one Ogg file
per user per channel per interval -- plus a clipsort.log manifest mapping GUID
to username, channel index and channel name. That makes it possible to compare
what our client put on the wire against what a known-good client (Jamtaba,
ReaNINJAM) put on the wire, under identical conditions, with numbers instead of
opinions.

For each clip this reports:

  rate/ch     declared sample rate and channel count of the Ogg stream
  frames      decoded length, and its ratio to the expected interval length
  rms/peak    level, to catch gain and silence problems
  impulse     offset of the loudest sample from the start of the interval

The impulse column is the point of the exercise. With the plugin's Test Tone
toggle enabled, each transmitted interval begins with a full-scale one-sample
impulse. Its offset in the archived clip IS the transmit alignment error, in
samples. A constant non-zero offset is a fixed latency worth characterising; an
offset that drifts from interval to interval is a clock bug.

Usage:
  scripts/analyze_archive.py /tmp/njarchive
  scripts/analyze_archive.py /tmp/njarchive/20260101_1200.ninjam
  scripts/analyze_archive.py /tmp/njarchive --user njtest

Decoding needs an Ogg Vorbis decoder. Tries, in order: soundfile, then the
`oggdec` or `ffmpeg` command line tools. If none is available it still reports
the manifest and file sizes.

Note the archive only captures intervals uploaded while a session directory is
open. The server opens one on a periodic check that runs every 30 seconds and
only while an authenticated user is connected, so stay connected for at least
that long before the intervals you intend to measure.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

# --------------------------------------------------------------------------
# Manifest
# --------------------------------------------------------------------------

CLIP_RE = re.compile(r'^user\s+([0-9a-f]{32})\s+"([^"]*)"\s+(\d+)\s+"([^"]*)"')
INTERVAL_RE = re.compile(r"^interval\s+(\d+)\s+(\d+)\s+(\d+)")


class Clip:
    def __init__(self, guid, user, channel_index, channel_name, interval, bpm, bpi):
        self.guid = guid
        self.user = user
        self.channel_index = channel_index
        self.channel_name = channel_name
        self.interval = interval
        self.bpm = bpm
        self.bpi = bpi
        self.path = None


def parse_manifest(session_dir):
    """Returns (clips, last_bpm, last_bpi).

    'user' lines appearing between two 'interval' lines belong to that
    interval, so the current interval number is carried forward as we scan.
    """
    log = session_dir / "clipsort.log"
    if not log.is_file():
        return [], None, None

    clips = []
    interval = 0
    bpm = bpi = None
    for line in log.read_text(errors="replace").splitlines():
        m = INTERVAL_RE.match(line)
        if m:
            interval, bpm, bpi = int(m.group(1)), int(m.group(2)), int(m.group(3))
            continue
        m = CLIP_RE.match(line)
        if m:
            clips.append(
                Clip(m.group(1), m.group(2), int(m.group(3)), m.group(4),
                     interval, bpm, bpi)
            )
    return clips, bpm, bpi


def locate(session_dir, guid):
    """Clips live in a subdirectory named by the first hex character."""
    p = session_dir / guid[0] / f"{guid}.OGG"
    if p.is_file():
        return p
    for cand in session_dir.rglob(f"{guid}.*"):
        return cand
    return None


# --------------------------------------------------------------------------
# Decoding
# --------------------------------------------------------------------------

class Decoded:
    def __init__(self, samples, rate, channels):
        self.samples = samples      # list of floats, first channel only
        self.rate = rate
        self.channels = channels


def _decode_soundfile(path):
    try:
        import soundfile as sf
    except ImportError:
        return None
    try:
        data, rate = sf.read(str(path), always_2d=True, dtype="float32")
    except Exception:
        return None
    return Decoded([float(f[0]) for f in data], rate, data.shape[1])


def _decode_via_wav(argv, tmp):
    """Runs a decoder that writes WAV to `tmp`, then reads and removes `tmp`.

    `tmp` is passed explicitly and never inferred from argv -- deriving it from
    the argument list is how you end up deleting the archive you are measuring.
    """
    try:
        subprocess.run(argv, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, OSError):
        return None

    try:
        with wave.open(tmp, "rb") as w:
            rate = w.getframerate()
            ch = w.getnchannels()
            width = w.getsampwidth()
            frames = w.readframes(w.getnframes())
    except Exception:
        return None
    finally:
        try:
            os.unlink(tmp)
        except OSError:
            pass

    if width != 2:
        return None
    count = len(frames) // 2
    ints = struct.unpack("<%dh" % count, frames[: count * 2])
    return Decoded([v / 32768.0 for v in ints[::ch]], rate, ch)


def decode(path):
    d = _decode_soundfile(path)
    if d:
        return d
    # Decode into the system temp directory, never alongside the archive.
    import tempfile
    fd, tmp = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    try:
        if shutil.which("oggdec"):
            d = _decode_via_wav(["oggdec", "-Q", "-o", tmp, str(path)], tmp)
            if d:
                return d
        if shutil.which("ffmpeg"):
            d = _decode_via_wav(
                ["ffmpeg", "-v", "quiet", "-y", "-i", str(path), tmp], tmp
            )
            if d:
                return d
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)
    return None


# --------------------------------------------------------------------------
# Measurement
# --------------------------------------------------------------------------

def measure(samples):
    if not samples:
        return 0.0, 0.0, -1
    total = 0.0
    peak = 0.0
    peak_at = 0
    for i, v in enumerate(samples):
        total += v * v
        a = abs(v)
        if a > peak:
            peak = a
            peak_at = i
    return (total / len(samples)) ** 0.5, peak, peak_at


def find_sessions(root):
    root = Path(root)
    if (root / "clipsort.log").is_file():
        return [root]
    return sorted(p for p in root.glob("*.ninjam") if p.is_dir())


def main():
    ap = argparse.ArgumentParser(
        description="Measure a Ninjam server session archive."
    )
    ap.add_argument("archive", help="archive root, or a single .ninjam session")
    ap.add_argument("--user", help="only report clips from users matching this")
    ap.add_argument("--rate", type=float, default=None,
                    help="expected sample rate for the interval-length check "
                         "(default: the rate declared by each clip)")
    args = ap.parse_args()

    sessions = find_sessions(args.archive)
    if not sessions:
        print(f"no .ninjam session directories found under {args.archive}",
              file=sys.stderr)
        print("The server only opens one on a periodic check that runs every "
              "30 s while an authenticated user is connected.", file=sys.stderr)
        return 1

    decoder_available = True
    exit_code = 0

    for session in sessions:
        clips, bpm, bpi = parse_manifest(session)
        print(f"\n=== {session.name} ===")
        if not clips:
            print("  clipsort.log lists no clips.")
            print("  (It is written with buffered stdio and only flushed when "
                  "the session closes -- stop the server and re-run.)")
            oggs = sorted(session.rglob("*.OGG"))
            if oggs:
                print(f"  {len(oggs)} clip files are present on disk:")
                for o in oggs:
                    print(f"    {o.name}  {o.stat().st_size} bytes")
            continue

        if args.user:
            clips = [c for c in clips if args.user in c.user]

        print(f"  tempo {bpm} bpm / {bpi} bpi")
        header = (f"  {'user':<24} {'ch':<10} {'iv':>3} {'rate':>6} {'ch':>2} "
                  f"{'frames':>8} {'vs exp':>7} {'rms':>7} {'peak':>6} "
                  f"{'impulse':>8}")
        print(header)
        print("  " + "-" * (len(header) - 2))

        by_user = {}

        for c in clips:
            c.path = locate(session, c.guid)
            if c.path is None:
                print(f"  {c.user:<24} {c.channel_name:<10} {c.interval:>3}   "
                      f"MISSING FILE")
                exit_code = 1
                continue

            size = c.path.stat().st_size
            d = decode(c.path)
            if d is None:
                decoder_available = False
                print(f"  {c.user:<24} {c.channel_name:<10} {c.interval:>3}   "
                      f"{size} bytes (no decoder available)")
                continue

            rms, peak, peak_at = measure(d.samples)
            rate = args.rate or d.rate
            expected = int(bpi / (bpm / 60.0) * rate) if bpm and bpi else 0
            ratio = (len(d.samples) / expected) if expected else 0.0

            print(f"  {c.user:<24} {c.channel_name:<10} {c.interval:>3} "
                  f"{d.rate:>6} {d.channels:>2} {len(d.samples):>8} "
                  f"{ratio*100:>6.1f}% {rms:>7.4f} {peak:>6.3f} "
                  f"{peak_at:>8}")

            by_user.setdefault(c.user, []).append((peak_at, ratio, d.rate))

        # Per-user summary: this is where a clock bug shows itself.
        if by_user:
            print()
            for user, rows in by_user.items():
                offsets = [r[0] for r in rows]
                ratios = [r[1] for r in rows]
                rates = {r[2] for r in rows}
                spread = max(offsets) - min(offsets) if offsets else 0
                print(f"  {user}: {len(rows)} clips, rate(s) {sorted(rates)}")
                print(f"    impulse offset  min {min(offsets)}  max "
                      f"{max(offsets)}  spread {spread} samples")
                print(f"    length vs expected  min {min(ratios)*100:.1f}%  "
                      f"max {max(ratios)*100:.1f}%")
                if spread > 64:
                    print("    WARNING: impulse offset drifts between "
                          "intervals -- transmit alignment is not stable.")
                    exit_code = 1
                if any(abs(r - 1.0) > 0.02 for r in ratios):
                    print("    WARNING: interval length is off by more than "
                          "2% -- check the encoder sample rate.")
                    exit_code = 1

    if not decoder_available:
        print("\nNo Ogg decoder found. Install one for the full report:",
              file=sys.stderr)
        print("  pip install soundfile     (or)  apt install vorbis-tools",
              file=sys.stderr)

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
