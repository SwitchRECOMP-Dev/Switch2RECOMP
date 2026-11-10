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
  u32 count=6000, bytes=(count+2)*16; u64*raw=calloc(1,bytes);
  for(u32 i=0;i<count;i++){raw[i*2]=argc>1 && argv[1][0]=='n'? 0x918|7ull<<12 : 0x23e|7ull<<12|2ull<<16;}
  raw[count*2]=0x802|7ull<<12|2ull<<16|0x3e4ccccdull<<32; raw[(count+1)*2]=0x94d|7ull<<12;
  u32 len,alen; Sm86Info inf; int old=argc>1 && argv[1][0]=='o'; u32 cap=old? 0x10000 : bytes;
  u8* low=sm86_lower((u8*)raw,cap,0,&len,&alen,&inf); u32 keep=old && len>0x10000? 0x10000 : len;
  u8*b=calloc(1,keep+80);memcpy(b+80,low,keep);ShaderInfo si={0};si.cs_local[0]=4;si.cs_local[1]=4;si.cs_local[2]=4;g_sh_ampere=1;
  char*s=shader_to_glsl(b,keep+80,ST_CS,&si);fprintf(stderr,"raw %u lowered %u kept %u unsupported %d\n",cap,len,keep,si.unsupported);puts(s);return 0;
}
