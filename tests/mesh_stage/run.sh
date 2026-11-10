#!/bin/sh
# SwitchRecomp - Copyright 2026 MZ Labs
# Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
# Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen.
# a mesh workgroup (translator output with a hand written body) writes one triangle through the stage buffer; the
# epilogue and the draw vertex shader have to put it on screen with its attribute
set -eu
cd "$(dirname "$0")"
swr_tmp=$(mktemp -d)
trap 'rm -rf "$swr_tmp"' EXIT
cc -O1 run.c -lEGL -lGL -o "$swr_tmp/test"
EGL_PLATFORM=surfaceless "$swr_tmp/test" mesh.comp draw.vert
