/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#include "sm86.h"
#include "shader.h"
#include <stdio.h>
#include <stdlib.h>
u32 g_loop_limit=16384; int g_texoff_emul; extern int g_sh_ampere;
int main(int argc,char** argv){
  u64 w[]={0x388|7ull<<12|1ull<<24,4ull<<9,0xb1d|7ull<<12,0,0x384|7ull<<12|2ull<<16|3ull<<24,4ull<<9,0x94d|7ull<<12,0};
  if(argc>1){ w[0]=(w[0]&~0xFFFull)|0x988; w[1]|=63ull; w[4]=(w[4]&~0xFFFull)|0x984|63ull<<32; }
  if(argc>2){ int sc=atoi(argv[2]); if(argc>3) w[5]|=(u64)sc<<14; else w[1]|=(u64)sc<<14; }
  u32 n,al; Sm86Info inf; u8*m=sm86_lower((u8*)w,sizeof w,0,&n,&al,&inf); u8*b=calloc(n+80,1); for(u32 i=0;i<n;i++)b[80+i]=m[i]; ShaderInfo si={0};si.cs_local[0]=4;si.cs_local[1]=4;si.cs_local[2]=4;si.shared_size=1024;g_sh_ampere=1;
  char*s=shader_to_glsl(b,n+80,ST_CS,&si);puts(s);return 0;
}
