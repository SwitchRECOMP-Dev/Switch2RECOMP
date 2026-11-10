#!/bin/sh
# SwitchRecomp - Copyright 2026 MZ Labs
# Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
# Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen.
set -eu
cd "$(dirname "$0")/../.."
swr_tmp=$(mktemp -d)
trap 'rm -rf "$swr_tmp"' EXIT
cc -O1 -Iruntime tests/sm86_gbuf/gen.c runtime/shader.c runtime/sm86.c -lm -o "$swr_tmp/gen"
cc -O1 tests/sm86_gbuf/run.c -lEGL -lGL -o "$swr_tmp/run"
for swr_mode in lea add mad; do
"$swr_tmp/gen" "$swr_mode" > "$swr_tmp/test.glsl"
python3 - "$swr_tmp/test.glsl" "$swr_mode" <<'INJECT'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text().replace('void main(){','layout(std430,binding=1) buffer Result { uint result[]; };\nvoid main(){')
s=s.replace('  uint r4 = 0u;', '  uint r4 = gl_LocalInvocationIndex'+('*4u' if sys.argv[2]=='add' else '')+';')
s=s.replace('  uint r5 = 0u;','  uint r5 = 4u;').replace('  return;','  result[gl_LocalInvocationIndex]=r0; return;')
p.write_text(s)
INJECT
EGL_PLATFORM=surfaceless "$swr_tmp/run" "$swr_tmp/test.glsl"
done
