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
u32 g_loop_limit=16384; int g_texoff_emul; extern int g_sh_ampere;

int main(void){
  EGLDisplay ed=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);
  if(!eglInitialize(ed,0,0) || !eglBindAPI(EGL_OPENGL_API)) return 2;
  EGLint attr[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE};
  EGLContext ec=eglCreateContext(ed,EGL_NO_CONFIG_KHR,0,attr);
  if(!eglMakeCurrent(ed,0,0,ec)) return 2;
  GLuint out, cb; glGenBuffers(1,&out); glGenBuffers(1,&cb);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,out); glBufferData(GL_SHADER_STORAGE_BUFFER,256,NULL,GL_DYNAMIC_READ);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,out);
  u32 base=0x12345678, data[4]={base}; glBindBuffer(GL_UNIFORM_BUFFER,cb);
  glBufferData(GL_UNIFORM_BUFFER,65536,NULL,GL_STATIC_DRAW); glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof data,data);
  glBindBufferBase(GL_UNIFORM_BUFFER,36,cb);
  int bad=0, total=0;
  for(int cbuf=0;cbuf<2;cbuf++) for(int neg=0;neg<2;neg++) for(int shift=0;shift<32;shift++){
    /* RZ as the high word catches a modifier that overlaps the register field. */
    for(int zero_hi=0;zero_hi<2;zero_hi++){
      u64 w[4]={0x211ull|7ull<<12|2ull<<16,0,0x94dull|7ull<<12,0};
      if(!cbuf) w[0]|=1ull<<32;
      if(cbuf) w[0]=(w[0]&~0xe00ull)|0xa00ull;
      w[1]=(zero_hi?255ull:3ull)|1ull<<16|(u64)neg<<8|(u64)shift<<11;
      u32 n, al; Sm86Info inf; u8* lowered=sm86_lower((u8*)w,sizeof w,0,&n,&al,&inf);
      u8* code=calloc(n+80,1); memcpy(code+80,lowered,n); free(lowered);
      ShaderInfo si={0}; si.cs_local[0]=64; si.cs_local[1]=si.cs_local[2]=1; g_sh_ampere=1;
      char* src=shader_to_glsl(code,n+80,ST_CS,&si); free(code);
      const char* start=strstr(src,"void main(){"); const char* body=strchr(start,'\n')+1;
      char* end=strstr(body,"  return;"); if(!end || inf.untranslated) return 2;
      char* text=calloc(strlen(src)+2048,1);
      snprintf(text,strlen(src)+2048,"%.*s\nlayout(std430,binding=0) buffer Result { uint result[]; };\nvoid main(){\nuint ix=gl_LocalInvocationIndex;\nuint r0=ix==0u?0u:ix==1u?1u:ix==2u?0xffffffffu:ix==3u?0x80000000u:ix*1664525u+1013904223u;\nuint r1=0x12345678u; uint r3=ix*747796405u+2891336453u;\n%.*s\nresult[ix]=r2;\n}\n",(int)(start-src),src,(int)(end-body),body);
      /* the translator owns register declarations; inputs start after them. */
      char* p;
      while((p=strstr(text,"  uint r0 = 0u;"))) memmove(p,p+15,strlen(p+15)+1);
      while((p=strstr(text,"  uint r1 = 0u;"))) memmove(p,p+15,strlen(p+15)+1);
      while((p=strstr(text,"  uint r3 = 0u;"))) memmove(p,p+15,strlen(p+15)+1);
      GLuint sh=glCreateShader(GL_COMPUTE_SHADER); const char* tp=text; glShaderSource(sh,1,&tp,NULL); glCompileShader(sh);
      GLint ok; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
      if(!ok){ char log[2048]; glGetShaderInfoLog(sh,sizeof log,0,log); puts(log); puts(text); return 2; }
      GLuint prog=glCreateProgram(); glAttachShader(prog,sh); glLinkProgram(prog); glGetProgramiv(prog,GL_LINK_STATUS,&ok); if(!ok) return 2;
      glUseProgram(prog); glDispatchCompute(1,1,1); glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
      u32 got[64]; glBindBuffer(GL_SHADER_STORAGE_BUFFER,out); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof got,got);
      for(u32 i=0;i<64;i++){
        u32 lo=i==0?0:i==1?1:i==2?0xffffffffu:i==3?0x80000000u:i*1664525u+1013904223u;
        u32 hi=zero_hi?0:i*747796405u+2891336453u;
        uint64_t off=((uint64_t)hi<<32)|lo; if(neg) off=0ull-off;
        u32 expected=base+(u32)(off>>(32-shift)); total++;
        if(got[i]!=expected){ if(bad<8) printf("LEA.HI cbuf=%d neg=%d shift=%d hi0=%d lane=%u: %08x != %08x\n",cbuf,neg,shift,zero_hi,i,got[i],expected); bad++; }
      }
      glDeleteProgram(prog); glDeleteShader(sh); free(text); free(src);
    }
  }
  printf("LEA.HI: %d/%d wrong results (register/cbuf, +/- offset, all scales, carry and RZ)\n",bad,total); return bad!=0;
}
