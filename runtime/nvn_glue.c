/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* What the NVN layer (runtime/nvn, renderer=vulkan) expects from the runtime around it: frame pacing, timing and a
   few debug hooks. The debug ones (watchpoints, breakpoints, trace spans, flight recorder) do nothing here. */
#include "rt.h"
#ifdef _WIN32
#include <windows.h>
#endif

static u64 g_run0;
unsigned long long swr_run_ns(void){ if(!g_run0) g_run0=plat_ns(); return plat_ns()-g_run0; }
volatile u32 g_swr_present_seq;
volatile u64 g_frame_mode;          /* 0: the game's own present interval */
int g_opt_tile_skip=-1;
volatile int g_swr_trace_on;
volatile int g_flight_req;
#ifdef _WIN32
volatile DWORD g_host_focus_tid;
#else
volatile unsigned long g_host_focus_tid;
#endif
int vdec_playing(void){ return 0; }
void kernel_svc_report(void){}
void plat_bp_min(u64 m){ (void)m; }
void plat_bp(u64 rva, long n){ (void)rva; (void)n; }
void plat_watch_range(u64 guest, u64 len, int huge){ (void)guest; (void)len; (void)huge; }
void plat_watch_page_arm(u64 guest){ (void)guest; }
void swr_trace_span(const char* name, unsigned long long t0){ (void)name; (void)t0; }
void input_rec_present(u32 interval){ (void)interval; }

/* vblank pacing: the game moves its clock by its present interval (vblanks at 60 Hz) each frame, so a frame isn't
   shown before that much time has passed since the last one, or the game runs fast. SWR_PACE=0 turns it off */
void swr_present_pace(u32 iv){
  static int on=-1; if(on<0){ const char* e=getenv("SWR_PACE"); on= !e || *e!='0'; }
  static u64 next; if(iv<1) iv=1; if(iv>4) iv=4;
  if(!on) return;
  u64 per=(u64)iv*1000000000ull/60, now=swr_run_ns();
  if(!next || now>next+per) next=now;
  else if(now<next){ if(next-now>1500000) plat_sleep_ns((i64)(next-now-1000000)); while(swr_run_ns()<next) plat_yield(); }
  next+=per; }
void plat_dump_native_stacks(void){}
void swr_trace_poll(void){}
unsigned long long swr_trace_ts(void){ return 0; }
/* makes the folders of a file path (screenshots) */
void plat_mkdir_for(const char* path){
  char p[1200]; snprintf(p,sizeof p,"%s",path);
  for(char* q=p+1;*q;q++) if(*q=='/' || *q=='\\'){ char c=*q; *q=0;
#ifdef _WIN32
    CreateDirectoryA(p,NULL);
#else
    { extern int mkdir(const char*, unsigned); mkdir(p,0755); }
#endif
    *q=c; } }
#ifndef _WIN32
#include <pthread.h>
void plat_thread_name(const char* n){ char b[16]; snprintf(b,sizeof b,"%s",n? n : ""); pthread_setname_np(pthread_self(),b); }
#endif
