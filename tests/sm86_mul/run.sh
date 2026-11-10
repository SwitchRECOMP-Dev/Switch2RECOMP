#!/bin/sh
# SwitchRecomp - Copyright 2026 MZ Labs
# Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
# Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen.
set -eu
cd "$(dirname "$0")/../.."
swr_tmp=$(mktemp -d)
trap 'rm -rf "$swr_tmp"' EXIT
cc -O1 -Iruntime tests/sm86_mul/test.c runtime/shader.c runtime/sm86.c -lEGL -lGL -lm -o "$swr_tmp/test"
EGL_PLATFORM=surfaceless "$swr_tmp/test"
