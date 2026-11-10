#!/bin/sh
# SwitchRecomp - Copyright 2026 MZ Labs
# Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
# Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen.
set -eu
cd "$(dirname "$0")/../.."
swr_tmp=$(mktemp -d)
trap 'rm -rf "$swr_tmp"' EXIT
cc -O1 -Iruntime tests/sm86_length/gen.c runtime/shader.c runtime/sm86.c -lm -o "$swr_tmp/gen"
cc -O1 tests/sm86_length/run.c -lEGL -lGL -o "$swr_tmp/run"
"$swr_tmp/gen" > "$swr_tmp/expanded.glsl"
python3 - "$swr_tmp/expanded.glsl" <<'CHECK'
from pathlib import Path
import sys
assert 'r2 = 0x3E4CCCCDu;' in Path(sys.argv[1]).read_text(), 'lowered shader lost its tail'
print('PASS expanded shader tail retained')
CHECK
"$swr_tmp/gen" nop > "$swr_tmp/test.glsl"
python3 - "$swr_tmp/test.glsl" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text().replace('void main(){','layout(std430,binding=0) buffer Result { uint result[]; };\nvoid main(){').replace('  return;','  result[gl_LocalInvocationIndex]=r2; return;');p.write_text(s)
PY
EGL_PLATFORM=surfaceless "$swr_tmp/run" "$swr_tmp/test.glsl" 0x3e4ccccd
