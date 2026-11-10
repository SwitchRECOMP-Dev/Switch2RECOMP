/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* Linux: vk_window.h on the runtime's own X11 window (backend_x11.c), as vk_window_hwnd.c does on Windows */
#include "vk_window.h"
#include "vk_internal.h"
#include <X11/Xlib.h>
#include <stdio.h>
int backend_vk_window(void** display, void** surface, int* w, int* h);   /* backend_x11.c: its own Display, the window */
static Display* g_dpy; static Window g_win; static volatile LONG g_serial; static int g_w, g_h;
void nvn_window_set(void* hwnd){ (void)hwnd; }
void nvn_window_resized(int w, int h){ g_w=w; g_h=h; InterlockedIncrement(&g_serial); }
int vkw_open(const char* title, int width, int height, int hidden){
  (void)title; (void)width; (void)height; (void)hidden;
  void *d=NULL, *s=NULL; int w=0, h=0;
  if(backend_vk_window(&d,&s,&w,&h)!=1 || !d || !s){ VKRED("no X11 window"); return -1; }
  g_dpy=(Display*)d; g_win=(Window)(uintptr_t)s; g_w=w; g_h=h;
  VKLOG("window %dx%d pixels (X11)", g_w, g_h);
  return 0; }
void vkw_close(void){}
int vkw_is_open(void){ return g_win!=0; }
const char* const* vkw_instance_exts(uint32_t* n){ static const char* const e[]={VK_KHR_SURFACE_EXTENSION_NAME,"VK_KHR_xlib_surface"}; *n=2; return e; }
int vkw_create_surface(VkInstance inst, VkSurfaceKHR* out){
  VkXlibSurfaceCreateInfoKHR c={VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR}; c.dpy=g_dpy; c.window=g_win;
  PFN_vkCreateXlibSurfaceKHR f=(PFN_vkCreateXlibSurfaceKHR)vkGetInstanceProcAddr(inst,"vkCreateXlibSurfaceKHR");
  if(!f || f(inst,&c,NULL,out)!=VK_SUCCESS){ VKRED("vkCreateXlibSurfaceKHR failed"); return -1; }
  return 0; }
void vkw_pixel_size(int* w, int* h){ if(g_dpy && g_win){ XWindowAttributes a; if(XGetWindowAttributes(g_dpy,g_win,&a) && a.width>0){ g_w=a.width; g_h=a.height; } } *w=g_w; *h=g_h; }
uint32_t vkw_resize_serial(void){ return (uint32_t)g_serial; }
int vkw_minimized(void){ return 0; }
int vkw_display_hz(void){ return 60; }
void vkw_request_size(int w, int h){ (void)w; (void)h; }
void vkw_request_fullscreen(int on){ (void)on; }
void vkw_set_close_handler(void (*fn)(void)){ (void)fn; }
void vkw_set_title(const char* title){ (void)title; }
