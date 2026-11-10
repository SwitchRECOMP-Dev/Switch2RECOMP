/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include "shader.h"
#include "sm86.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
u32 g_loop_limit=16384; int g_texoff_emul; extern int g_sh_ampere;
static u32 bits(float f){u32 v;memcpy(&v,&f,4);return v;}
static GLuint program(const char* src){
  GLuint sh=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(sh,1,&src,0);glCompileShader(sh);GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[4096];glGetShaderInfoLog(sh,sizeof log,0,log);puts(log);return 0;}
  GLuint p=glCreateProgram();glAttachShader(p,sh);glLinkProgram(p);glDeleteShader(sh);glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(p);return 0;}return p;
}
static void word(u64* w,int* n,u64 lo,u64 hi){w[(*n)++]=lo;w[(*n)++]=hi;}
static void plop(u64*w,int*n,int dst,int a,int b,int c,int lut,int neg){word(w,n,0x89c|7ull<<12,(u64)(lut&7)|(u64)(lut>>3)<<8|(u64)c<<4|(u64)b<<13|(u64)neg<<16|(u64)dst<<17|7ull<<20|(u64)a<<23);}
int main(void){
  EGLDisplay ed=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);if(!eglInitialize(ed,0,0)||!eglBindAPI(EGL_OPENGL_API))return 2;
  EGLint at[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE};EGLContext ec=eglCreateContext(ed,EGL_NO_CONFIG_KHR,0,at);if(!eglMakeCurrent(ed,0,0,ec))return 2;
  GLuint buf;glGenBuffers(1,&buf);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buf);glBufferData(GL_SHADER_STORAGE_BUFFER,64,NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buf);
  int bad=0,total=0;
  for(int lut=0;lut<256;lut++)for(int neg=0;neg<2;neg++){
    u64 w[256]={0};int nw=0;
    word(w,&nw,0x803|7ull<<12|127ull<<32|255ull<<24,0);
    for(int val=0;val<8;val++){
      for(int j=0;j<3;j++)plop(w,&nw,j,7,7,7,(val>>(2-j)&1)?255:0,0);
      plop(w,&nw,3,0,1,2,lut,neg);
      word(w,&nw,0x883|7ull<<12|63ull<<24|8ull<<32,0);
      word(w,&nw,0x802|7ull<<12|10ull<<16|(u64)(0x10000+val)<<32,0);
    }
    plop(w,&nw,4,7,7,7,(lut&1)?255:0,0);
    plop(w,&nw,5,7,7,7,(lut&2)?255:0,0);
    word(w,&nw,0x892ull|4ull<<12|(u64)neg<<15|63ull<<24|63ull<<32,63ull|(u64)((lut&4)?255:0)<<8|5ull<<17|7ull<<23|1ull<<27);
    word(w,&nw,0x883|7ull<<12|63ull<<24|32ull<<32,0);
    word(w,&nw,0x802|7ull<<12|10ull<<16|0x10008ull<<32,0);
    word(w,&nw,0x794d,0);
    u32 n,al;Sm86Info inf;u8* lo=sm86_lower((u8*)w,nw*8,0,&n,&al,&inf);u8* code=calloc(n+80,1);memcpy(code+80,lo,n);free(lo);
    ShaderInfo si={0};si.cs_local[0]=si.cs_local[1]=si.cs_local[2]=1;g_sh_ampere=1;char* src=shader_to_glsl(code,n+80,ST_CS,&si);free(code);if(!src||inf.untranslated||si.unsupported){printf("untranslated %u/%d\n",inf.untranslated,si.unsupported);return 2;}
    char* main=strstr(src,"void main(){");char* text=calloc(strlen(src)+2048,1);snprintf(text,strlen(src)+2048,"%.*slayout(std430,binding=0) buffer Result { uint result[]; };\n%s",(int)(main-src),src,main);
    for(int val=0;val<9;val++){char marker[64];snprintf(marker,sizeof marker,"r10 = 0x%Xu;",0x10000+val);char* p=strstr(text,marker);if(!p)return 2;p+=strlen(marker);size_t tail=strlen(p);memmove(p+64,p,tail+1);char ins[64];snprintf(ins,sizeof ins," result[%d]=r176;",val);int k=strlen(ins);memcpy(p,ins,k);memmove(p+k,p+64,tail+1);}
    GLuint prog=program(text);if(!prog)return 2;glUseProgram(prog);glDispatchCompute(1,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);u32 got[9];glBindBuffer(GL_SHADER_STORAGE_BUFFER,buf);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof got,got);
    for(int val=0;val<9;val++){u32 expected=val==8? (((((lut&1)!=0)^neg)?((lut>>2)&1):((lut>>1)&1))<<5) : ((lut>>(val^(neg?2:0)))&1)<<3;total++;if(got[val]!=expected){if(bad<8)printf("lut %x neg %d input %d: %u != %u\n",lut,neg,val,got[val],expected);bad++;}}
    glDeleteProgram(prog);free(src);free(text);
  }
  GLenum err=glGetError();printf("spilled uniform predicate LUT: %d/%d wrong results, GL %x\n",bad,total,err);return bad||err;
}
