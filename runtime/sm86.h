/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* Switch 2 (Ampere) SASS -> Maxwell SASS for shader.c */
#ifndef SWR_SM86_H
#define SWR_SM86_H
#include "cpu.h"
typedef struct { u32 insts, untranslated, first_bad, bad_op; int maxreg, reg_clash; int tex_bank; } Sm86Info;   /* tex_bank: the cbuf bank its bound texture handles live in, -1 none */
/* has_sph: graphics stages start with the 0x50 byte header. returns a malloc'd Maxwell program (header + code)
   for shader_to_glsl, its size in out_len, and the Ampere program size in alen */
extern unsigned short g_sm86_untr[256]; extern int g_sm86_nuntr;
/* BRX with absolute Ampere offsets: lowered with this offset, g_sm86_nmap[i] = shader.c word index of Ampere
   instruction i of the last program lowered */
#define SM86_BRX_NATIVE 0x7FFFF8
/* the byte offset its table entries count from, per native BRX (lowered word index), when it isn't 0: MKW's vertex
   shaders jump from offset 0x30, past the 0x30 bytes NVN2 leaves after the header */
extern u32 g_sm86_brxb_idx[64], g_sm86_brxb_base[64]; extern u32 g_sm86_nbrxb;
/* RET from a function with several callers: BRX on the link register with this offset, the targets are
   g_sm86_rets (Ampere instruction indices after each CALL, their byte offsets are the link values) */
#define SM86_RET_NATIVE 0x7FFFF0
extern u32 g_sm86_rets[64]; extern u32 g_sm86_nrets;
extern u32* g_sm86_nmap; extern u32 g_sm86_nnmap;
extern int g_sm86_stage;   /* 0 vertex/fragment/compute, 1 geometry, 2 tessellation: set before sm86_lower */
u8* sm86_lower(const u8* code, u32 max_bytes, int has_sph, u32* out_len, u32* alen, Sm86Info* inf);
#endif
