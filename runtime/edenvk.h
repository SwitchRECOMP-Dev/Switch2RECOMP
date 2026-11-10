/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* renderer=vulkan (runtime/edenvk/glue/edenvk_glue.cpp, or edenvk_stub.c in builds without it) */
#ifndef SWR_EDENVK_H
#define SWR_EDENVK_H
#include "rt.h"
extern int g_edenvk_on;     /* Eden's Vulkan renderer runs: the engines' methods go to it, not to OpenGL */
extern int g_edenvk_want;   /* renderer=vulkan in the .ini */
int edenvk_start(int type, void* display, void* surface, u32 w, u32 h);
void edenvk_resize(u32 w, u32 h);
void edenvk_map(u64 gva, u64 cpu, u64 size);
void edenvk_unmap(u64 gva, u64 size);
void edenvk_method(u32 ch, u32 engine, u32 m, u32 v, int last);   /* engine: 0 3D, 1 compute, 2 I2M, 3 2D, 4 DMA */
void edenvk_set_ampere(u32 ch);
void edenvk_stage_addr(u32 ch, u32 stage, u64 addr);
void edenvk_draw(u32 ch, int indexed, u32 topology, u32 first, u32 count, u32 base_vertex, u32 base_instance, u32 instances);
void edenvk_compute(u32 ch, const u32* qmd2, u64 code_base);
void edenvk_submit_begin(void);
void edenvk_submit_end(void);
void edenvk_sync(void);
void edenvk_present(u64 addr, u32 w, u32 h, u32 pitch, u32 layout, u32 bhl, u32 fmt, const int crop[4]);
#endif
