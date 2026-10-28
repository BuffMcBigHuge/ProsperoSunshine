#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Sunshine is compiled as part of the host; only its licence ships here.
set -euo pipefail
cp "$src_sunshine/LICENSE" "$prefix/licenses/sunshine.txt"
