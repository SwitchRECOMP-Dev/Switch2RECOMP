/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#include "sm86.h"
#include "shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
u32 g_loop_limit=16384; int g_texoff_emul; extern int g_sh_ampere;
int main(int argc,char**argv){
  u64 w[]={0x19c0004007a11ull,0x1fca00078010ffull,0x19d00ff017624ull,0xfca00000e06ffull,0x7381ull,0xea200001ea900ull,0x794d,0};
  if(argc>1 && argv[1][0]=='a'){ /* IADD3 adds an unshifted index to the same cbuf base */
    w[0]=0xa10ull|7ull<<12|4ull<<24|0x670ull<<38; w[1]=255ull|7ull<<17|7ull<<20;
  }
  if(argc>1 && argv[1][0]=='m'){ /* IMAD's cbuf operand is the addend in form 3 */
    w[0]=0x624ull|7ull<<12|4ull<<24|0x670ull<<38; w[1]=5ull|7ull<<17;
  }
  u32 n,al; Sm86Info inf; u8* m=sm86_lower((u8*)w,sizeof w,0,&n,&al,&inf);
  u8* b=calloc(n+80,1); memcpy(b+80,m,n); ShaderInfo si={0}; si.cs_local[0]=64; si.cs_local[1]=si.cs_local[2]=1; g_sh_ampere=1;
  char* s=shader_to_glsl(b,n+80,ST_CS,&si);
  fprintf(stderr,"%d untranslated, %d storage buffers, base c0[%#x]\n",si.unsupported,si.ngbuf,si.ngbuf?si.gbuf_cbuf_off[0]:0);
  puts(s); free(s); free(b); free(m); return 0;
}
