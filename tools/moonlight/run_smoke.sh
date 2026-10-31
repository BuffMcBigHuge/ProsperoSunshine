#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# End-to-end smoke test: serverinfo -> /launch (pair.py) -> stream_test in the container
# -> ffmpeg verification -> /cancel.  The client must already be paired (pair.py pair).
#
#   tools/moonlight/run_smoke.sh [host] [duration_s] [extra pair.py launch args, e.g. --corever]
#   STREAM_ARGS="--bitrate 20000" passes extra options to stream_test.
#   VIDEO_FORMAT=hevc checks the output as HEVC (with STREAM_ARGS="--hevc").
#   DOCKER_ARGS="--cap-add NET_ADMIN" and PRE_CMD="<shell>" run the client container with extra
#   rights and a setup command first, e.g. to inject packet loss (see loss_test.sh).
set -uo pipefail
cd "$(dirname "$0")/../.."
HOST=${1:-${PS5_HOST:?pass the console address or set PS5_HOST}}
DUR=${2:-20}
shift 2 2>/dev/null || true
LAUNCH_ARGS=("$@")
if command -v py >/dev/null 2>&1; then PY=(py -3); else PY=(python3); fi
REPO_WIN=$(pwd -W 2>/dev/null || pwd)
IMAGE=${IMAGE:-prosperosunshine-sdk:v0.45}
OUTDIR=out/moonlight
mkdir -p "$OUTDIR" out/moonlight-client
KEEP_TLS=""
dock() { MSYS_NO_PATHCONV=1 docker run --rm ${DOCKER_ARGS:-} --mount "type=bind,source=$REPO_WIN,target=/repo" "$IMAGE" bash -c "$1"; }

[ -f "$OUTDIR/stream_test" ] || { echo "build first: tools/moonlight/build.sh"; exit 2; }

"${PY[@]}" tools/moonlight/pair.py --host "$HOST" info >"$OUTDIR/serverinfo.txt" || { cat "$OUTDIR/serverinfo.txt"; exit 1; }
LJ=$("${PY[@]}" tools/moonlight/pair.py --host "$HOST" launch "${LAUNCH_ARGS[@]}") || { echo "launch failed"; exit 1; }
echo "launch: $LJ"
getj() { "${PY[@]}" -c "import json,sys; print(json.loads(sys.argv[1])['$1'])" "$LJ"; }
RTSP=$(getj sessionUrl0); RIKEY=$(getj rikey); RIKEYID=$(getj rikeyid)
APPVER=$(getj appversion); GFE=$(getj gfe); CODEC=$(getj codec_mode)

rm -f "$OUTDIR/moonlight-test.h264"
dock "cd /repo && ${PRE_CMD:-true} && timeout $((DUR+60)) out/moonlight/stream_test --addr $HOST --rtsp-url '$RTSP' --rikey $RIKEY --rikeyid $RIKEYID --appversion '$APPVER' --gfe '$GFE' --codec-mode $CODEC --out out/moonlight-test.h264 --audio-out out/moonlight-test.s16 --duration $DUR ${STREAM_ARGS:-}" 2>&1 | tee "$OUTDIR/stream_test.log"
RC=${PIPESTATUS[0]}
echo "stream_test exit code: $RC"
"${PY[@]}" tools/moonlight/pair.py --host "$HOST" cancel
if [ -s out/moonlight-test.h264 ]; then
  dock "cd /repo && ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,profile,width,height,avg_frame_rate,r_frame_rate,nb_read_frames -of default=nw=1 -f ${VIDEO_FORMAT:-h264} out/moonlight-test.h264; echo '--- ffmpeg full decode (-xerror):'; ffmpeg -v error -xerror -f ${VIDEO_FORMAT:-h264} -i out/moonlight-test.h264 -f null - && echo DECODE_OK"
fi
exit $RC
