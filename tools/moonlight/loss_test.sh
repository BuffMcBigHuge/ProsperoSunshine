#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# P6.2: stream while the client container randomly drops a share of the incoming video UDP packets
# (source port 47998). FEC should repair light loss; heavier loss must recover through the client's
# IDR request instead of freezing.
#
#   tools/moonlight/loss_test.sh [host] [seconds] [drop probability, e.g. 0.03]
set -uo pipefail
cd "$(dirname "$0")/../.."
HOST=${1:-${PS5_HOST:?pass the console address or set PS5_HOST}}
SECS=${2:-30}
P=${3:-0.03}
DOCKER_ARGS="--cap-add NET_ADMIN" \
PRE_CMD="apt-get -qq update >/dev/null && apt-get -qq install -y iptables >/dev/null && iptables -A INPUT -p udp --sport 47998 -m statistic --mode random --probability $P -j DROP" \
    bash tools/moonlight/run_smoke.sh "$HOST" "$SECS"
