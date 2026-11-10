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
#include <string.h>
#include <math.h>
u32 g_loop_limit=16384; int g_texoff_emul; extern int g_sh_ampere;
static GLuint shader(GLenum type,const char* s){ GLuint sh=glCreateShader(type);glShaderSource(sh,1,&s,0);glCompileShader(sh);GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(sh,sizeof log,0,log);puts(log);exit(2);}return sh; }
static char* source(int dim,int mode,int lod){
  u64 w[]={0xb60ull|7ull<<12|4ull<<24|8ull<<32|(u64)dim<<61,2ull|15ull<<8|(u64)mode<<23,0x794d,0};
  u32 n,al;Sm86Info inf;u8* lo=sm86_lower((u8*)w,sizeof w,0,&n,&al,&inf);u8* code=calloc(n+80,1);memcpy(code+80,lo,n);((u32*)code)[18]=15;
  ShaderInfo si={0};g_sh_ampere=1;char* src=shader_to_glsl(code,n+80,ST_FS,&si);free(lo);free(code);if(!src||inf.untranslated||si.unsupported)exit(2);
  char* at=strstr(src,"  frag_color0 =");if(!at){puts(src);exit(2);}char* out=calloc(strlen(src)+1024,1);char init[512];
  if(dim==3)snprintf(init,sizeof init,"r4=floatBitsToUint(1.0);r5=0u;r6=0u;");
  else if(dim==7)snprintf(init,sizeof init,"r4=0u;r5=floatBitsToUint(1.0);r6=0u;r7=0u;");
  else if(dim==5)snprintf(init,sizeof init,"r4=0u;r5=floatBitsToUint(0.5);r6=floatBitsToUint(0.5);");
  else snprintf(init,sizeof init,"r4=floatBitsToUint(0.5);r5=floatBitsToUint(0.5);%s",dim==2?"r6=floatBitsToUint(0.5);":"");
  snprintf(out,strlen(src)+1024,"%.*s%s\n%s\n%s",(int)(at-src),src,init,mode==3?"":"uint r8=0u;",at);
  char* body=strstr(out,"  frag_color0 =");char* final=calloc(strlen(out)+100,1);snprintf(final,strlen(out)+100,"%.*sr8=floatBitsToUint(%d.0);\n%s",(int)(body-out),out,lod,body);free(src);free(out);return final;
}
int main(void){
  EGLDisplay ed=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);if(!eglInitialize(ed,0,0)||!eglBindAPI(EGL_OPENGL_API))return 2;
  EGLint at[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE};EGLContext ec=eglCreateContext(ed,EGL_NO_CONFIG_KHR,0,at);if(!eglMakeCurrent(ed,0,0,ec))return 2;
  GLuint vao,fb,out;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenTextures(1,&out);glBindTexture(GL_TEXTURE_2D,out);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA32F,8,8);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,out,0);glViewport(0,0,8,8);
  GLuint vs=shader(GL_VERTEX_SHADER,"#version 450 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}");
  int dims[]={1,2,3,5,7};GLenum targets[]={GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_CUBE_MAP,GL_TEXTURE_2D_ARRAY,GL_TEXTURE_CUBE_MAP_ARRAY};int bad=0,total=0;
  for(int t=0;t<5;t++){
    GLuint tex;GLenum tg=targets[t];glGenTextures(1,&tex);glActiveTexture(GL_TEXTURE0+32);glBindTexture(tg,tex);
    if(tg==GL_TEXTURE_2D||tg==GL_TEXTURE_CUBE_MAP)glTexStorage2D(tg,7,GL_RGBA32F,64,64);else glTexStorage3D(tg,7,GL_RGBA32F,64,64,t==1?64:t==4?6:1);
    for(int l=0;l<7;l++){float v[]={0.125f*(l+1),0.0625f*(7-l),0.03125f*(l+2),1};glClearTexImage(tex,l,GL_RGBA,GL_FLOAT,v);}
    glTexParameteri(tg,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);glTexParameteri(tg,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(tg,GL_TEXTURE_MAX_LEVEL,6);
    for(int mode=1;mode<=3;mode+=2)for(int l=0;l<7;l++){
      char* src=source(dims[t],mode,l);GLuint fs=shader(GL_FRAGMENT_SHADER,src),p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok)return 2;glUseProgram(p);glDrawArrays(GL_TRIANGLES,0,3);
      float px[8*8*4];glReadPixels(0,0,8,8,GL_RGBA,GL_FLOAT,px);int level=mode==1?0:l;float want[]={0.125f*(level+1),0.0625f*(7-level),0.03125f*(level+2),1};
      for(int i=0;i<64;i++)for(int k=0;k<4;k++){total++;if(fabsf(px[i*4+k]-want[k])>1e-6f){if(bad<8)printf("dim=%d mode=%d lod=%d channel=%d got=%g expected=%g\n",dims[t],mode,l,k,px[i*4+k],want[k]);bad++;}}
      glDeleteProgram(p);glDeleteShader(fs);free(src);
    }glDeleteTextures(1,&tex);
  }
  GLenum err=glGetError();printf("TEX explicit LOD: %d/%d wrong channels, 5 texture types, LZ/LL, 7 levels, GL error %x\n",bad,total,err);return bad||err;
}
