/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* task/mesh epilogue + draw vertex shader: one triangle through the stage buffer */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char* rd(const char* p){FILE*f=fopen(p,"rb");fseek(f,0,2);long n=ftell(f);rewind(f);char*b=calloc(n+1,1);fread(b,1,n,f);fclose(f);return b;}
static GLuint sh(GLenum t,const char* s){GLuint x=glCreateShader(t);glShaderSource(x,1,&s,0);glCompileShader(x);GLint ok;glGetShaderiv(x,GL_COMPILE_STATUS,&ok);if(!ok){char l[4096];glGetShaderInfoLog(x,4096,0,l);printf("compile: %s\n",l);exit(1);}return x;}
static GLuint prog(GLuint a,GLuint b){GLuint p=glCreateProgram();glAttachShader(p,a);if(b)glAttachShader(p,b);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char l[4096];glGetProgramInfoLog(p,4096,0,l);printf("link: %s\n",l);exit(1);}return p;}
int main(int argc,char**argv){EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);eglInitialize(d,0,0);eglBindAPI(EGL_OPENGL_API);
 EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,0,a);eglMakeCurrent(d,0,0,c);
 GLuint cp=prog(sh(GL_COMPUTE_SHADER,rd(argv[1])),0), dp=prog(sh(GL_VERTEX_SHADER,rd(argv[2])),sh(GL_FRAGMENT_SHADER,"#version 440 core\nlayout(location=1) in vec4 in_attr1;\nout vec4 o; void main(){ o=vec4(1.0,in_attr1.y,0.0,1.0); }"));
 GLuint b[3]; glGenBuffers(3,b); glBindBuffer(GL_SHADER_STORAGE_BUFFER,b[0]); glBufferData(GL_SHADER_STORAGE_BUFFER,128,0,GL_DYNAMIC_COPY);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,b[1]); glBufferData(GL_SHADER_STORAGE_BUFFER,64*6*4,0,GL_DYNAMIC_COPY); glBindBuffer(GL_SHADER_STORAGE_BUFFER,b[2]); glBufferData(GL_SHADER_STORAGE_BUFFER,126*3*4,0,GL_DYNAMIC_COPY);
 for(int i=0;i<3;i++) glBindBufferBase(GL_SHADER_STORAGE_BUFFER,44+i,b[i]);
 glUseProgram(cp); GLint mu[4]={1,1,1,0}; glUniform4iv(300,1,mu); glDispatchCompute(1,1,1); glMemoryBarrier(GL_ALL_BARRIER_BITS);
 GLuint tex,fb,va; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex); glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,64,64); glGenFramebuffers(1,&fb); glBindFramebuffer(GL_FRAMEBUFFER,fb); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
 glViewport(0,0,64,64); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); glGenVertexArrays(1,&va); glBindVertexArray(va);
 glUseProgram(dp); glUniform4f(glGetUniformLocation(dp,"swr_vp"),1,0,0,0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,b[2]); glDrawElements(GL_TRIANGLES,126*3,GL_UNSIGNED_INT,0);
 unsigned char px[64*64*4]; glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,px); int red=0,g=0; for(int i=0;i<64*64;i++){ if(px[i*4]>200) red++; if(px[i*4+1]>50&&px[i*4+1]<80) g++; }
 unsigned mi[9]; glBindBuffer(GL_SHADER_STORAGE_BUFFER,b[2]); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,36,mi);
 printf("indices %u %u %u | %u %u %u ; covered %d px, attr1.y=0.25 on %d px, GL error %x\n",mi[0],mi[1],mi[2],mi[3],mi[4],mi[5],red,g,glGetError()); return !(red>300 && red<700 && g==red);}
