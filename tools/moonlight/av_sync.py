#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audio/video synchronisation measurement (ROADMAP P5.2) with the moonlight-common-c test client.

Method
------
The PS5 UI is used as a simultaneous picture+sound event source: `stream_test --ui-ticks N` presses D-pad
RIGHT/LEFT (alternating, so the highlight stays on screen) through moonlight-common-c.  The home menu moves its
focus highlight and plays a short navigation click.  `--timing-log` records, on one CLOCK_MONOTONIC axis (client
arrival time of each callback, microseconds):

    V,frame,idr,arrival_us,presentationTimeUs,bytes      every received video frame
    A,index,arrival_us,rms,peak                          every received (5 ms) audio packet, decoded RMS
    P,tick,action,sent_us                                every input press / release sent

This script then

  1. runs the capture (tools/moonlight/run_smoke.sh) unless --analyze-only is given,
  2. decodes out/moonlight-test.h264 in the SDK container (ffmpeg only; no numpy needed): per decoded frame, the mean
     absolute luma difference to the previous frame, both for the whole frame and for a region of interest (--roi),
     and out/moonlight-test.s16 through a high-pass filter (clicks stand out from music/background) per audio packet,
  3. pairs every press with its first visual change and its first audio onset after the press, and reports
     press -> picture, press -> sound and sound-minus-picture (audio minus video, client arrival times),
  4. reports drift two ways: the offset trend over the run, and the clock-rate difference between the audio packet
     stream and the video frame stream versus the client clock (needs no events at all).

Detection is threshold based and adaptive (median + k * MAD of the per-frame / per-packet metric); `--roi`,
`--video-k`, `--audio-db` tune it.  Nothing is made up: events without a detectable picture or sound change are
listed as missed and excluded from the statistics.

    py -3 tools/moonlight/av_sync.py --host <console-ip> --duration 60 --ticks 30
    py -3 tools/moonlight/av_sync.py --analyze-only            # re-analyse out/moonlight-timing.csv + h264 + s16
"""
import argparse
import bisect
import json
import math
import os
import statistics
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "out"
IMAGE = os.environ.get("IMAGE", "prosperosunshine-sdk:v0.45")


def dock(cmd):
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    return subprocess.run(
        ["docker", "run", "--rm", "--mount", f"type=bind,source={REPO},target=/repo", IMAGE, "bash", "-c", cmd],
        env=env, capture_output=True, text=True)


def parse_meta(path, key):
    """ffmpeg metadata=print output -> list of (frame_index, value)."""
    vals = []
    idx = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("frame:"):
                idx = int(line.split()[0].split(":")[1])
            elif line.startswith(key + "="):
                try:
                    v = float(line.split("=", 1)[1])
                except ValueError:
                    v = float("-inf")
                vals.append((idx, v))
    return [v for _, v in vals]


def load_timing(path):
    t0 = None
    video, audio, presses = [], [], []
    for line in open(path, encoding="utf-8"):
        p = line.strip().split(",")
        if p[0] == "H":
            t0 = int(p[1])
        elif p[0] == "V":
            video.append(dict(frame=int(p[1]), idr=int(p[2]), t=int(p[3]), pts=int(p[4]), size=int(p[5])))
        elif p[0] == "A":
            audio.append(dict(idx=int(p[1]), t=int(p[2]), rms=float(p[3]), peak=float(p[4])))
        elif p[0] == "P":
            presses.append(dict(tick=int(p[1]), action=p[2], t=int(p[3])))
    return t0, video, audio, presses


def robust_threshold(vals, k, floor):
    med = statistics.median(vals)
    mad = statistics.median(abs(v - med) for v in vals) * 1.4826
    return max(med + k * mad, med + floor), med, mad


def linfit(xs, ys):
    n = len(xs)
    if n < 2:
        return 0.0, 0.0
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return 0.0, my
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    return b, my - b * mx


def stats_line(name, vals):
    if not vals:
        return f"{name:<28} n=0"
    s = sorted(vals)
    sd = statistics.pstdev(vals) if len(vals) > 1 else 0.0
    return (f"{name:<28} n={len(s):<3} median={statistics.median(s):8.1f}  mean={sum(s)/len(s):8.1f}  "
            f"min={s[0]:8.1f}  max={s[-1]:8.1f}  sd={sd:6.1f}   [ms]")


def bash_exe():
    """Git Bash on Windows: a plain "bash" from Python resolves to WSL, which has no Docker here."""
    if os.name == "nt":
        for candidate in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files (x86)\Git\bin\bash.exe"):
            if os.path.exists(candidate):
                return candidate
    return "bash"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("PS5_HOST"), required="PS5_HOST" not in os.environ)
    ap.add_argument("--duration", type=int, default=60)
    ap.add_argument("--ticks", type=int, default=30)
    ap.add_argument("--interval-ms", type=int, default=1500)
    ap.add_argument("--kind", choices=["dpad", "ps"], default="dpad", help="event: D-pad R/L, or PS button toggling the control centre")
    ap.add_argument("--tick-start", type=float, default=None, help="seconds into the stream of the first press (stream_test default 9)")
    ap.add_argument("--roi", default="0,0,1,1", help="x0,y0,x1,y1 as fractions of the frame for the picture detector")
    ap.add_argument("--video-k", type=float, default=8.0, help="video threshold = median + k*MAD of the per-frame difference")
    ap.add_argument("--video-floor", type=float, default=0.15, help="minimum threshold above the median (8-bit luma units)")
    ap.add_argument("--audio-db", type=float, default=12.0, help="audio onset = high-passed level this many dB above the preceding 200 ms")
    ap.add_argument("--audio-abs-db", type=float, default=-70.0, help="audio onset must also be above this high-passed level (dBFS)")
    ap.add_argument("--hp", type=int, default=1500, help="audio high-pass corner in Hz")
    ap.add_argument("--window-ms", type=int, default=700, help="how long after a press to look for the picture / sound")
    ap.add_argument("--pre-ms", type=int, default=0, help="accept audio onsets this long before the press (0 = causal only)")
    ap.add_argument("--timing", default="out/moonlight-timing.csv")
    ap.add_argument("--tag", default="", help="suffix for the output files (e.g. _end10min)")
    ap.add_argument("--analyze-only", action="store_true")
    ap.add_argument("--csv", default=None)
    a = ap.parse_args()

    timing = REPO / a.timing
    csv_path = REPO / (a.csv or f"out/av_sync_events{a.tag}.csv")
    if not a.analyze_only:
        # --no-script: the scripted startup input presses A, which would start the focused home-screen tile.
        sa = f"--ui-ticks {a.ticks} --tick-interval-ms {a.interval_ms} --tick-kind {a.kind} --timing-log {a.timing} --no-script"
        if a.tick_start is not None:
            sa += f" --tick-start {a.tick_start}"
        env = dict(os.environ, STREAM_ARGS=sa)
        r = subprocess.run([bash_exe(), "tools/moonlight/run_smoke.sh", a.host, str(a.duration)], cwd=REPO, env=env)
        if r.returncode != 0:
            print(f"run_smoke.sh failed ({r.returncode})", file=sys.stderr)
            return r.returncode

    t0, video, audio, presses = load_timing(timing)
    press = [p for p in presses if not p["action"].startswith("release")]
    if not video or not audio:
        print("timing log has no video or audio lines", file=sys.stderr)
        return 1

    # ---- decode the captured files in the container
    x0, y0, x1, y1 = [float(v) for v in a.roi.split(",")]
    vf = (f"scale=480:270:flags=area,format=yuv420p,tblend=all_mode=difference,split=2[d1][d2];"
          f"[d1]crop=iw*{x1-x0}:ih*{y1-y0}:iw*{x0}:ih*{y0},signalstats,"
          f"metadata=print:key=lavfi.signalstats.YAVG:file=out/av_roi.txt[o1];"
          f"[d2]signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=out/av_full.txt[o2]")
    r = dock(f"cd /repo && ffmpeg -v error -y -i out/moonlight-test.h264 -filter_complex '{vf}' "
             f"-map '[o1]' -f null - -map '[o2]' -f null -")
    if r.returncode != 0:
        print("video analysis failed:\n" + r.stderr, file=sys.stderr)
        return 1
    roi = parse_meta(OUT / "av_roi.txt", "lavfi.signalstats.YAVG")
    full = parse_meta(OUT / "av_full.txt", "lavfi.signalstats.YAVG")

    n_audio_samples = (OUT / "moonlight-test.s16").stat().st_size // 2
    af = (f"highpass=f={a.hp},asetnsamples=n=240:p=0,astats=metadata=1:reset=1,"
          f"ametadata=print:key=lavfi.astats.Overall.RMS_level:file=out/av_audio.txt")
    r = dock(f"cd /repo && ffmpeg -v error -y -f s16le -ar 48000 -ac 2 -i out/moonlight-test.s16 -af '{af}' -f null -")
    if r.returncode != 0:
        print("audio analysis failed:\n" + r.stderr, file=sys.stderr)
        return 1
    hp = parse_meta(OUT / "av_audio.txt", "lavfi.astats.Overall.RMS_level")

    if len(roi) == len(video) - 1:  # tblend needs a predecessor: no output for the first frame
        roi = [statistics.median(roi)] + roi
        full = [statistics.median(full)] + full
    print(f"video: {len(video)} frames logged, {len(roi)} decoded (roi {a.roi}); audio: {len(audio)} packets logged, "
          f"{len(hp)} analysis blocks ({n_audio_samples/2/240:.0f} packets of 240 samples)")
    if len(roi) != len(video):
        print(f"WARNING: decoded frame count {len(roi)} != logged frame count {len(video)}; frame-to-time mapping is approximate")
    if abs(len(hp) - len(audio)) > 2:
        print(f"WARNING: audio blocks {len(hp)} != logged packets {len(audio)}; mapping is approximate")

    n = min(len(roi), len(video))
    m = min(len(hp), len(audio))
    vt = [v["t"] for v in video[:n]]
    at = [p["t"] for p in audio[:m]]
    vm = roi[:n]
    am = [max(v, -120.0) for v in hp[:m]]

    # The host sends audio as FEC blocks of 4 x 5 ms packets, so packets arrive in bursts about every 20 ms.  Besides the
    # raw arrival time, estimate when each packet's audio was complete on the host: arrival minus 5 ms for every
    # packet that follows it in the same burst (packets less than 3 ms apart belong to one burst).
    behind = [0] * m
    cnt = 0
    for i in range(m - 1, -1, -1):
        if i < m - 1 and at[i + 1] - at[i] < 3000:
            cnt += 1
        else:
            cnt = 0
        behind[i] = cnt
    at_aligned = [at[i] - behind[i] * 5000 for i in range(m)]

    vthr, vmed, vmad = robust_threshold(vm, a.video_k, a.video_floor)
    print(f"picture detector: roi diff median={vmed:.3f} MAD={vmad:.3f} -> threshold {vthr:.3f} (8-bit luma units); "
          f"full-frame diff median={statistics.median(full):.3f}")
    print(f"sound detector:   high-pass>{a.hp} Hz level median={statistics.median(am):.1f} dBFS, onset = +{a.audio_db} dB over "
          f"the preceding 200 ms and > {a.audio_abs_db} dBFS")

    def first_video_change(p):
        for i in range(n):
            if vt[i] < p:
                continue
            if vt[i] > p + a.window_ms * 1000:
                return None
            if vm[i] > vthr:
                return i
        return None

    def first_audio_onset(p):
        start = p - a.pre_ms * 1000
        for i in range(max(40, bisect.bisect_left(at, start)), m):
            if at[i] > p + a.window_ms * 1000:
                return None
            bg = sum(10 ** (am[j] / 10) for j in range(i - 40, i)) / 40  # mean power of preceding 200 ms
            lvl = 10 ** (am[i] / 10)
            if am[i] > a.audio_abs_db and lvl > bg * 10 ** (a.audio_db / 10):
                return i
        return None

    rows = []
    for pr in press:
        vi = first_video_change(pr["t"])
        ai = first_audio_onset(pr["t"])
        rows.append(dict(tick=pr["tick"], action=pr["action"], press_s=(pr["t"] - t0) / 1e6,
                         v=None if vi is None else (vt[vi] - pr["t"]) / 1000.0,
                         a=None if ai is None else (at[ai] - pr["t"]) / 1000.0,
                         aa=None if ai is None else (at_aligned[ai] - pr["t"]) / 1000.0,
                         vframe=None if vi is None else video[vi]["frame"],
                         aidx=None if ai is None else audio[ai]["idx"],
                         vstrength=None if vi is None else vm[vi],
                         alevel=None if ai is None else am[ai]))
    for r_ in rows:
        r_["av"] = None if r_["v"] is None or r_["a"] is None else r_["a"] - r_["v"]
        r_["aav"] = None if r_["v"] is None or r_["aa"] is None else r_["aa"] - r_["v"]

    with open(csv_path, "w", encoding="utf-8") as f:
        f.write("tick,action,press_s,press_to_picture_ms,press_to_sound_ms,audio_minus_video_ms,video_frame,audio_packet,picture_diff,sound_level_dbfs\n")
        for r_ in rows:
            f.write(",".join("" if v is None else (f"{v:.3f}" if isinstance(v, float) else str(v)) for v in
                             (r_["tick"], r_["action"], r_["press_s"], r_["v"], r_["a"], r_["av"], r_["vframe"], r_["aidx"],
                              r_["vstrength"], r_["alevel"])) + "\n")

    print(f"\nper-event ({len(rows)} presses, times in ms after the press was sent; '-' = not detected)")
    print("  tick action   at_s   press->pic  press->snd  snd-pic")
    for r_ in rows:
        fmt = lambda v: "      -   " if v is None else f"{v:9.1f} "
        print(f"  {r_['tick']:3d}  {r_['action']:<6} {r_['press_s']:6.1f} {fmt(r_['v'])} {fmt(r_['a'])} {fmt(r_['av'])}")

    pv = [r_["v"] for r_ in rows if r_["v"] is not None]
    pa = [r_["a"] for r_ in rows if r_["a"] is not None]
    av = [r_["av"] for r_ in rows if r_["av"] is not None]
    aav = [r_["aav"] for r_ in rows if r_["aav"] is not None]
    print()
    print(stats_line("press -> picture (client)", pv))
    print(stats_line("press -> sound (client)", pa))
    print(stats_line("sound minus picture", av))
    print(stats_line("  (burst-aligned sound)", aav))
    print(f"detected: picture {len(pv)}/{len(rows)}, sound {len(pa)}/{len(rows)}, both {len(av)}/{len(rows)}")

    summary = dict(events=len(rows), picture=len(pv), sound=len(pa), both=len(av))
    if av:
        summary["av_median_ms"] = statistics.median(av)
        summary["av_aligned_median_ms"] = statistics.median(aav)
        summary["av_min_ms"], summary["av_max_ms"] = min(av), max(av)
    if len(av) >= 4:
        xs = [r_["press_s"] for r_ in rows if r_["av"] is not None]
        slope, _ = linfit(xs, av)  # ms per second
        h = len(av) // 3
        first, last = statistics.median(av[:h]), statistics.median(av[-h:])
        print(f"offset trend over the run: {slope*60:+.2f} ms/min (linear fit); median first third {first:.1f} ms, last third {last:.1f} ms")
        summary["av_trend_ms_per_min"] = slope * 60

    # ---- event-free drift: arrival time vs the stream's own clock, both streams on the client clock
    vx = [(v["t"] - t0) / 1e6 for v in video]
    vy = [(v["t"] - v["pts"]) / 1000.0 for v in video]  # arrival minus host pts, ms (offset arbitrary)
    vs, _ = linfit(vx, vy)
    ax = [(p["t"] - t0) / 1e6 for p in audio]
    ay = [(p["t"] / 1000.0) - p["idx"] * 5.0 for p in audio]  # arrival minus nominal 5 ms * index
    as_, _ = linfit(ax, ay)
    print("\nclock drift (no events needed), slope of arrival time against the stream's nominal timeline:")
    print(f"  video: arrival - presentationTimeUs   {vs*1000:+8.2f} us/s ({vs*60:+.2f} ms/min)")
    print(f"  audio: arrival - 5 ms * packet index  {as_*1000:+8.2f} us/s ({as_*60:+.2f} ms/min)")
    print(f"  audio relative to video               {(as_-vs)*60:+8.2f} ms/min")
    jv = [(video[i]["t"] - video[i - 1]["t"]) / 1000.0 for i in range(1, len(video))]
    ja = [(audio[i]["t"] - audio[i - 1]["t"]) / 1000.0 for i in range(1, len(audio))]
    print(f"  inter-arrival: video median {statistics.median(jv):.2f} ms max {max(jv):.1f} ms | audio median {statistics.median(ja):.2f} ms max {max(ja):.1f} ms")
    summary.update(video_clock_ms_per_min=vs * 60, audio_clock_ms_per_min=as_ * 60, audio_vs_video_ms_per_min=(as_ - vs) * 60)
    print("\nsummary json: " + json.dumps(summary))
    print(f"events csv: {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
