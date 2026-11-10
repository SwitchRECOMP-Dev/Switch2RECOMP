#!/bin/sh
# SwitchRecomp - Copyright 2026 MZ Labs
# Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
# Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen.
set -eu
cd "$(dirname "$0")/../.."
swr_tmp=$(mktemp -d)
trap 'rm -rf "$swr_tmp"' EXIT
cc -O1 -Iruntime tests/sm86_color/gen.c runtime/shader.c runtime/sm86.c -lm -o "$swr_tmp/gen"
cc -O1 -Iruntime tests/test_shader_bank.c runtime/shader.c -lm -o "$swr_tmp/bank"
"$swr_tmp/bank"
cc -O1 tests/sm86_color/run.c -lEGL -lGL -o "$swr_tmp/run"
cc -O1 -Iruntime tests/sm86_color/lea.c runtime/shader.c runtime/sm86.c -lEGL -lGL -lm -o "$swr_tmp/lea"
EGL_PLATFORM=surfaceless "$swr_tmp/lea"
for swr_kind in f h t s i j k l m z; do
"$swr_tmp/gen" "$swr_kind" > "$swr_tmp/test.glsl"
python3 - "$swr_tmp/test.glsl" "$swr_kind" <<'PY'
from pathlib import Path
import sys,re
p=Path(sys.argv[1]);s=p.read_text()
for r in [0,2]:
 if f'uint r{r} =' not in s:s=s.replace('void main(){',f'void main(){{\n  uint r{r} = 0u;')
s=s.replace('void main(){','layout(std430,binding=0) buffer Result { uint result[]; };\nvoid main(){')
decl=list(re.finditer(r'^  (uint|bool) \w+ = [^;]+;\n',s,re.M));pos=decl[-1].end()
s=s[:pos]+'  r0 = '+('0x3c003800u' if sys.argv[2] in ['h','i','j','k'] else '0x40000000u')+';\n'+s[pos:]

if sys.argv[2] in ['l','m','z']:
 s=s.replace('  r0 = 0x40000000u;', '  r0=128u+gl_LocalInvocationIndex; r1=r0;')
s=s.replace('  return;','  result[gl_LocalInvocationIndex]=r2; return;');p.write_text(s)
PY
if [ "$swr_kind" = f ]; then swr_expected=0x3f800000; elif [ "$swr_kind" = h ]; then swr_expected=0x3f000000; elif [ "$swr_kind" = t ]; then swr_expected=0x3a003400; elif [ "$swr_kind" = i ]; then swr_expected=0x46700000; elif [ "$swr_kind" = j ]; then swr_expected=0x3f800000; elif [ "$swr_kind" = k ]; then swr_expected=0x42600000; elif [ "$swr_kind" = l ]; then swr_expected=Ln; elif [ "$swr_kind" = m ]; then swr_expected=Lp; elif [ "$swr_kind" = z ]; then swr_expected=Lz; else swr_expected=0x3e800000; fi
EGL_PLATFORM=surfaceless "$swr_tmp/run" "$swr_tmp/test.glsl" "$swr_expected"
done
