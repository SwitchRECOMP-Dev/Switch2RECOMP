#!/bin/sh
# SwitchRecomp - Copyright 2026 MZ Labs
# Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
# Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen.
set -eu
cd "$(dirname "$0")/../.."
swr_tmp=$(mktemp -d)
trap 'rm -rf "$swr_tmp"' EXIT
cc -O1 -Iruntime tests/sm86_mem/gen.c runtime/shader.c runtime/sm86.c -lm -o "$swr_tmp/gen"
cc -O1 tests/sm86_mem/run.c -lEGL -lGL -o "$swr_tmp/run"
for swr_mode in plain uniform; do
if [ "$swr_mode" = uniform ]; then "$swr_tmp/gen" uniform > "$swr_tmp/test.glsl"; else "$swr_tmp/gen" > "$swr_tmp/test.glsl"; fi
python3 - "$swr_tmp/test.glsl" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text().replace('void main(){','layout(std430,binding=0) buffer Result { uint result[]; };\nvoid main(){').replace('  uint r3 = 0u;','  uint r3 = 0u;\n  uint id=gl_LocalInvocationIndex; r0=id+1u; r1=id*4u; r3=((id+1u)%64u)*4u;').replace('  return;','  result[id]=r2; return;');p.write_text(s)
PY
EGL_PLATFORM=surfaceless "$swr_tmp/run" "$swr_tmp/test.glsl"
done

for swr_scale in 1 2 3; do
for swr_access in store load; do
"$swr_tmp/gen" uniform "$swr_scale" $([ "$swr_access" = load ] && echo load) > "$swr_tmp/test.glsl"
python3 - "$swr_tmp/test.glsl" "$swr_scale" "$swr_access" <<'PYTEST'
from pathlib import Path
import sys
p=Path(sys.argv[1]);sc=int(sys.argv[2])+1;store=sys.argv[3]=='store'
a=f'id*16u/{1<<sc}u' if store else 'id*16u'
b='((id+1u)%64u)*16u' if store else f'((id+1u)%64u)*16u/{1<<sc}u'
s=p.read_text().replace('void main(){','layout(std430,binding=0) buffer Result { uint result[]; };\nvoid main(){').replace('  uint r3 = 0u;',f'  uint r3 = 0u;\n  uint id=gl_LocalInvocationIndex; r0=id+1u; r1={a}; r3={b};').replace('  return;','  result[id]=r2; return;');p.write_text(s)
PYTEST
EGL_PLATFORM=surfaceless "$swr_tmp/run" "$swr_tmp/test.glsl"
done
done
