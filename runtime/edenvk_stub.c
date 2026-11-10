/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* renderer=vulkan without runtime/edenvk built in: the option is ignored and OpenGL runs */
#include "rt.h"
int g_edenvk_on;
int edenvk_start(int type, void* display, void* surface, u32 w, u32 h){ (void)type;(void)display;(void)surface;(void)w;(void)h;
  WARN("renderer=vulkan: this build has no Vulkan renderer (runtime/edenvk): using OpenGL\n"); return 0; }
void edenvk_resize(u32 w, u32 h){ (void)w;(void)h; }
void edenvk_map(u64 gva, u64 cpu, u64 size){ (void)gva;(void)cpu;(void)size; }
void edenvk_unmap(u64 gva, u64 size){ (void)gva;(void)size; }
void edenvk_method(u32 ch, u32 e, u32 m, u32 v, int last){ (void)ch;(void)e;(void)m;(void)v;(void)last; }
void edenvk_set_ampere(u32 ch){ (void)ch; }
void edenvk_stage_addr(u32 ch, u32 s, u64 a){ (void)ch;(void)s;(void)a; }
void edenvk_draw(u32 ch, int idx, u32 t, u32 f, u32 c, u32 bv, u32 bi, u32 n){ (void)ch;(void)idx;(void)t;(void)f;(void)c;(void)bv;(void)bi;(void)n; }
void edenvk_compute(u32 ch, const u32* q, u64 cb){ (void)ch;(void)q;(void)cb; }
void edenvk_submit_begin(void){}
void edenvk_submit_end(void){}
void edenvk_sync(void){}
void edenvk_present(u64 a, u32 w, u32 h, u32 p, u32 l, u32 b, u32 f, const int crop[4]){ (void)a;(void)w;(void)h;(void)p;(void)l;(void)b;(void)f;(void)crop; }
