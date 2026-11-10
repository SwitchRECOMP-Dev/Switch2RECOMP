/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(int argc,char**argv){
  EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0); if(!eglInitialize(d,0,0)||!eglBindAPI(EGL_OPENGL_API))return 2;
  EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE}; EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,0,a); if(!eglMakeCurrent(d,0,0,c))return 2;
  FILE*f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,0,2);long n=ftell(f);rewind(f);char*s=calloc(n+1,1);if(fread(s,1,n,f)!=(size_t)n)return 2;fclose(f);
  GLuint sh=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(sh,1,(const char**)&s,0);glCompileShader(sh);GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[4096];glGetShaderInfoLog(sh,sizeof log,0,log);puts(log);return 2;}
  GLuint p=glCreateProgram();glAttachShader(p,sh);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok)return 2;glUseProgram(p);
  uint32_t src[64],out[64]={0},cb[16384]={0}; static const uint32_t colors[]={0x3e4ccccd,0x3ecccccd,0x3f4ccccd,0x3f800000};
  for(int i=0;i<64;i++)src[i]=colors[i&3]; cb[0x670/4]=0x10000;cb[0x674/4]=1;cb[0x678/4]=sizeof src;
  GLuint b[3];glGenBuffers(3,b);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,b[0]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof src,src,GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,b[0]);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,b[1]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof out,out,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,b[1]);
  glBindBuffer(GL_UNIFORM_BUFFER,b[2]);glBufferData(GL_UNIFORM_BUFFER,sizeof cb,cb,GL_STATIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,36,b[2]);
  glDispatchCompute(1,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glBindBuffer(GL_SHADER_STORAGE_BUFFER,b[1]);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof out,out);
  int bad=0;for(int i=0;i<64;i++)if(src[i]!=out[i])bad++;
  printf("%d/64 wrong color-buffer reads, GL error %x\n",bad,glGetError());return bad!=0;
}
