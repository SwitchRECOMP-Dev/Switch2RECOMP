/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
typedef uint32_t u32; typedef uint64_t u64;
typedef struct { GLenum ifmt; int depth; } GlFmt;
typedef struct { GLuint id; u32 w,h,levels; GlFmt f; GLenum target; float scale; GLint swz[4]; u64 gva; u32 fkey; int is_rt; } Tex;
static u32 TW(Tex*t){return t->w;}static u32 TH(Tex*t){return t->h;}
static int g_nrt;static Tex*g_rt[8];static u64 st_drawn;
#define LOG printf
static PFNGLCOPYIMAGESUBDATAPROC p_cis=glCopyImageSubData;
#include "feedback_impl.inc"
int main(void){EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);eglInitialize(d,0,0);eglBindAPI(EGL_OPENGL_API);EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE};EGLContext ctx=eglCreateContext(d,EGL_NO_CONFIG_KHR,0,a);eglMakeCurrent(d,0,0,ctx);
Tex src={0};src.w=src.h=4;src.levels=1;src.target=GL_TEXTURE_2D;src.f.ifmt=GL_RGBA32F;src.scale=1;src.is_rt=1;src.gva=0x8112571000ull;
glGenTextures(1,&src.id);glBindTexture(GL_TEXTURE_2D,src.id);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA32F,4,4);float pixels[64];for(int i=0;i<64;i++)pixels[i]=(float)(i%4+1)/8;glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_FLOAT,pixels);GLuint fb;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,src.id,0);g_nrt=1;g_rt[0]=&src;st_drawn=1;Tex*snap=tex_feedback(&src);if(snap->id==src.id){puts("FAIL framebuffer alias");return 1;}float out[64];glBindTexture(GL_TEXTURE_2D,snap->id);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,out);for(int i=0;i<64;i++)if(out[i]!=pixels[i])return 2;
float zero[4]={0};glClearBufferfv(GL_COLOR,0,zero);glBindTexture(GL_TEXTURE_2D,snap->id);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,out);for(int i=0;i<64;i++)if(out[i]!=pixels[i])return 3;st_drawn++;snap=tex_feedback(&src);glBindTexture(GL_TEXTURE_2D,snap->id);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,out);for(int i=0;i<64;i++)if(out[i]!=0)return 4;printf("PASS feedback snapshot: original pixels preserved, next draw refreshed, GL error %x\n",glGetError());return 0;}
