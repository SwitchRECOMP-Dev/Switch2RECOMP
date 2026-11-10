/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#include "../runtime/shader.h"
#include <stdio.h>
#include <stdlib.h>
u32 g_loop_limit=16384; int g_texoff_emul;
int main(void){
  u64 code[16]={0}; code[11]=0xE30000000007000Full;
  for(u32 bank=0;bank<8;bank++){
    ShaderInfo si={0}; si.cs_local[0]=si.cs_local[1]=si.cs_local[2]=1; si.tex_bank_set=1; si.tex_bank=bank;
    char* src=shader_to_glsl((u8*)code,sizeof code,ST_CS,&si); free(src);
    if(!si.tex_bank_set || si.tex_bank!=bank){ fprintf(stderr,"FAIL compute texture bank %u lost\n",bank); return 1; }
  }
  puts("PASS compute texture banks 0..7 preserved"); return 0;
}
