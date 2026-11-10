/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#include "shader.h"
#include "sm86.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
u32 g_loop_limit=16384;int g_texoff_emul;extern int g_sh_ampere;
int main(void){
  u64 w[8]={0};int scales[3]={3,5,2};
  for(int i=0;i<3;i++){w[i*2]=0x220ull|7ull<<12|(u64)i<<16|(u64)i<<24|3ull<<32;w[i*2+1]=(u64)scales[i]<<20;}
  w[6]=0x794d;u32 n,al;Sm86Info inf;u8* lo=sm86_lower((u8*)w,sizeof w,0,&n,&al,&inf);u8* code=calloc(n+80,1);memcpy(code+80,lo,n);((u32*)code)[18]=15;
  ShaderInfo si={0};g_sh_ampere=1;char* src=shader_to_glsl(code,n+80,ST_FS,&si);
  char* body=strstr(src,"  r0 = ");if(!body)return 2;
  printf("%.*s",(int)(body-src),src);
  puts("  vec3 ref_=gl_FragCoord.x<32.0?vec3(0.28,0.34,0.42):gl_FragCoord.x<64.0?vec3(0.75,0.42,0.18):vec3(0.64);\n  r0=floatBitsToUint(ref_.r*2.0);r1=floatBitsToUint(ref_.g*0.5);r2=floatBitsToUint(ref_.b*4.0);r3=floatBitsToUint(1.0);");
  puts(body);free(src);free(code);free(lo);return inf.untranslated||si.unsupported;
}
