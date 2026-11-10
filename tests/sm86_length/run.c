/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);eglInitialize(d,0,0);eglBindAPI(EGL_OPENGL_API);EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE};EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,0,a);eglMakeCurrent(d,0,0,c);FILE*f=fopen(argv[1],"rb");fseek(f,0,2);long n=ftell(f);rewind(f);char*s=calloc(n+1,1);if(fread(s,1,n,f)!=(size_t)n)return 2; fclose(f);GLuint sh=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(sh,1,(const char**)&s,0);glCompileShader(sh);GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(sh,sizeof log,0,log);puts(log);return 2;}GLuint p=glCreateProgram();glAttachShader(p,sh);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok)return 2;glUseProgram(p);GLuint tex;glGenTextures(1,&tex);glActiveTexture(GL_TEXTURE0+64);glBindTexture(GL_TEXTURE_2D,tex);float px[4]={0.25f,0.75f,0.5f,1};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,1,1,0,GL_RGBA,GL_FLOAT,px);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);GLuint b;glGenBuffers(1,&b);glBindBuffer(GL_SHADER_STORAGE_BUFFER,b);unsigned out[64]={0};glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof out,out,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,b);glDispatchCompute(1,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof out,out);int bad=0;for(int i=0;i<64;i++)if(out[i]!=(unsigned)strtoul(argv[2],0,0))bad++;printf("%d/64 wrong shader results\n",bad);return bad!=0;}
