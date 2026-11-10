/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#include "sm86.h"
#include "shader.h"
#include <stdio.h>
#include <stdlib.h>
u32 g_loop_limit=16384; int g_texoff_emul; extern int g_sh_ampere;
int main(int argc,char** argv){
  u64 w[4]={0};
  if(argc>1 && (argv[1][0]=='l'||argv[1][0]=='m'||argv[1][0]=='z')){w[0]=0x211|7ull<<12|2ull<<16|1ull<<32;w[1]=255ull|1ull<<16|(argv[1][0]=='m'?0:1ull<<8)|(argv[1][0]=='z'?0:25ull<<11);}
  else if(argc>1 && argv[1][0]=='k'){w[0]=0x306|7ull<<12|2ull<<16|1ull<<60;w[1]=2ull<<(75-64);}
  else if(argc>1 && (argv[1][0]=='i' || argv[1][0]=='j')){w[0]=(argv[1][0]=='i'?0x306:0x304)|7ull<<12|2ull<<16|1ull<<60;w[1]=2ull<<(75-64)|1ull<<(84-64);}
  else if(argc>1 && (argv[1][0]=='t' || argv[1][0]=='s')){w[0]=0xb60|7ull<<12|2ull<<16|0ull<<24|1ull<<32|1ull<<60|1ull<<61;w[1]=255ull|3ull<<8|(argv[1][0]=='t'?1ull<<15:0);}
  else if(argc>1 && argv[1][0]=='h'){w[0]=0x230|7ull<<12|2ull<<16|255ull<<24|0ull<<32|2ull<<60;w[1]=1ull<<(78-64);}
  else {w[0]=0x808|7ull<<12|2ull<<16|0ull<<24|0x3f800000ull<<32;}
  w[2]=0x94d|7ull<<12;
  u32 n,al; Sm86Info inf; u8*m=sm86_lower((u8*)w,sizeof w,0,&n,&al,&inf); u8*b=calloc(n+80,1); for(u32 i=0;i<n;i++)b[80+i]=m[i]; ShaderInfo si={0};si.cs_local[0]=4;si.cs_local[1]=4;si.cs_local[2]=4;si.shared_size=256;g_sh_ampere=1;
  char*s=shader_to_glsl(b,n+80,ST_CS,&si);puts(s);return 0;
}
