/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
static GLuint sh(GLenum t,const char*s){GLuint a=glCreateShader(t);glShaderSource(a,1,&s,0);glCompileShader(a);return a;}
int main(int argc,char**argv){EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);eglInitialize(d,0,0);eglBindAPI(EGL_OPENGL_API);EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE};EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,0,a);eglMakeCurrent(d,0,0,c);
if(argc>1){FILE*f=fopen(argv[1],"rb");fseek(f,0,2);long n=ftell(f);rewind(f);char*b=calloc(n+1,1);fread(b,1,n,f);GLuint x=sh(GL_FRAGMENT_SHADER,b);GLint ok;glGetShaderiv(x,GL_COMPILE_STATUS,&ok);char log[8192];glGetShaderInfoLog(x,sizeof log,0,log);puts(log);printf("captured shader compile: %s\n",ok?"PASS":"FAIL");return !ok;}
const char*v="#version 430\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2-1,0,1);}";
const char*f="#version 430\nin vec2 uv;layout(binding=0)uniform sampler2D t;layout(location=256)uniform vec4 swr_lod0;out vec4 o;void main(){o=vec4(textureQueryLod(t,uv)+vec2(0,swr_lod0.x),0,1);}";
GLuint p=glCreateProgram();glAttachShader(p,sh(GL_VERTEX_SHADER,v));glAttachShader(p,sh(GL_FRAGMENT_SHADER,f));glLinkProgram(p);glUseProgram(p);GLuint va,fb,rt;glGenVertexArrays(1,&va);glBindVertexArray(va);glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenTextures(1,&rt);glBindTexture(GL_TEXTURE_2D,rt);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA32F,32,32);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,rt,0);glViewport(0,0,32,32);
float q[3];for(int i=0;i<3;i++){GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);glTexStorage2D(GL_TEXTURE_2D,i?6:10,GL_R8,i?32:512,i?32:512);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);float shift[4]={i==2?4:0,0,0,0};glProgramUniform4fv(p,256,1,shift);glDrawArrays(GL_TRIANGLES,0,3);float out[4];glReadPixels(16,16,1,1,GL_RGBA,GL_FLOAT,out);q[i]=out[1];printf("size %d query %g %g\n",i?32:512,out[0],out[1]);}printf("missing mip offset: %g\n",q[0]-q[1]);printf("corrected difference: %g\n",q[0]-q[2]);return fabsf(q[0]-q[1]-4)>0.01 || fabsf(q[0]-q[2])>0.01;}
