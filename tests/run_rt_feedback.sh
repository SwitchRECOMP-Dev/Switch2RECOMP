#!/bin/sh
# SwitchRecomp - Copyright 2026 MZ Labs
# Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
# Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen.
set -eu
cd "$(dirname "$0")/.."
swr_tmp=$(mktemp -d)
trap 'rm -rf "$swr_tmp"' EXIT
python3 - "$swr_tmp/feedback_impl.inc" <<'PY'
from pathlib import Path
import sys
s=Path('runtime/gpu_gl.c').read_text();a=s.index('static Tex* tex_feedback(');b=s.index('static void bind_textures(',a);Path(sys.argv[1]).write_text(s[a:b])
PY
cc -O1 -I"$swr_tmp" tests/test_rt_feedback.c -lEGL -lGL -o "$swr_tmp/test"
EGL_PLATFORM=surfaceless "$swr_tmp/test"
