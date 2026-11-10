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
int main(void){
  EGLDisplay ed=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);if(!eglInitialize(ed,0,0)||!eglBindAPI(EGL_OPENGL_API))return 2;
  EGLint at[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE};EGLContext ec=eglCreateContext(ed,EGL_NO_CONFIG_KHR,0,at);if(!eglMakeCurrent(ed,0,0,ec))return 2;
  GLuint buf,cb;glGenBuffers(1,&buf);glGenBuffers(1,&cb);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buf);glBufferData(GL_SHADER_STORAGE_BUFFER,256,NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buf);
  u32 data[16384]={0};data[0]=bits(0.75f);glBindBuffer(GL_UNIFORM_BUFFER,cb);glBufferData(GL_UNIFORM_BUFFER,sizeof data,data,GL_STATIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,36,cb);
  int bad=0,total=0;
  for(int form=0;form<3;form++)for(int scale=1;scale<8;scale++)for(int neg=0;neg<2;neg++)for(int sat=0;sat<2;sat++){
    u64 w[]={0,0,0x794d,0};w[0]=(form==0?0x220:form==1?0x820:0xa20)|7ull<<12|2ull<<16;
    if(form==0)w[0]|=1ull<<32;else if(form==1)w[0]|=(u64)bits(0.75f)<<32;
    w[1]=(u64)scale<<20|(u64)neg<<8|(u64)sat<<13;
    u32 n,al;Sm86Info inf;u8* lower=sm86_lower((u8*)w,sizeof w,0,&n,&al,&inf);u8* code=calloc(n+80,1);memcpy(code+80,lower,n);free(lower);
    ShaderInfo si={0};si.cs_local[0]=64;si.cs_local[1]=si.cs_local[2]=1;g_sh_ampere=1;char* src=shader_to_glsl(code,n+80,ST_CS,&si);free(code);if(inf.untranslated||si.unsupported)return 2;
    char* main=strstr(src,"void main(){");char* body=strchr(main,'\n')+1;char* ret=strstr(body,"  return;");if(!ret)return 2;
    char* text=calloc(strlen(src)+2048,1);
    snprintf(text,strlen(src)+2048,"%.*s\nlayout(std430,binding=0) buffer Result { uint result[]; };\nvoid main(){\nuint ix=gl_LocalInvocationIndex;\nuint r0=floatBitsToUint(0.125+float(ix)*0.03125);\nuint r1=floatBitsToUint(0.5+float(ix)*0.015625);\n%.*s\nresult[ix]=r2;\n}\n",(int)(main-src),src,(int)(ret-body),body);
    char* p;while((p=strstr(text,"  uint r0 = 0u;")))memmove(p,p+15,strlen(p+15)+1);while((p=strstr(text,"  uint r1 = 0u;")))memmove(p,p+15,strlen(p+15)+1);
    GLuint prog=program(text);if(!prog)return 2;glUseProgram(prog);glDispatchCompute(1,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    u32 got[64];glBindBuffer(GL_SHADER_STORAGE_BUFFER,buf);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof got,got);
    for(int i=0;i<64;i++){float a=0.125f+i*0.03125f,b=form==0?0.5f+i*0.015625f:0.75f;float expected=ldexpf(a*b,scale-4);if(neg)expected=-expected;if(sat){if(expected<0)expected=0;if(expected>1)expected=1;}total++;
      if(got[i]!=bits(expected)){if(bad<8)printf("FMUL form=%d scale=%d neg=%d sat=%d lane=%d: %08x != %08x\n",form,scale,neg,sat,i,got[i],bits(expected));bad++;}}
    glDeleteProgram(prog);free(src);free(text);
  }
  printf("FMUL scaling: %d/%d wrong results (7 scales, register/immediate/cbuf, sign and saturation), GL error %x\n",bad,total,glGetError());return bad!=0;
}
