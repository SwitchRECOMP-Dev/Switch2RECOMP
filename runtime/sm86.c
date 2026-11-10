/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* Switch 2 (Ampere, SM 8.x) SASS -> Maxwell (GM20B) SASS, so shader.c can turn it into GLSL like any Switch 1
   shader. every 128 bit Ampere instruction becomes one or more 64 bit Maxwell ones; branch targets are fixed up
   at the end. Ampere has no Maxwell form for some things, those go through scratch registers:
     UR0..UR62 (uniform registers)  -> R176..R238
     texture sources/results        -> R239..R250
     immediates that don't fit       -> R253, R254
   a shader that already uses R176+ can't be lowered this way and is reported.
   Ampere field positions come from Mesa's NAK encoder (src/nouveau/compiler/nak/sm70_encode.rs, MIT license,
   Copyright Collabora Ltd. and Red Hat Inc.); the Maxwell side is what shader.c decodes */
#include "sm86.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UBASE  176u
#define TA     239u    /* tex source list A (4) */
#define TB     243u    /* tex source list B (4) */
#define TO     247u    /* tex result (4) */
#define SA     251u    /* operand fix-ups (abs/neg Maxwell can't take there) */
#define SB     252u
#define S1     253u
#define S2     254u
#define RZ     255u

typedef struct { u64 lo, hi; } W128;
static int g_tex_bank=-1;
u32* g_sm86_nmap; u32 g_sm86_nnmap;
static int* g_ret; static u32 g_nret;   /* RET -> instruction after its one CALL, -2: called from several places */
/* the instructions right after every CALL of the last program lowered: a RET from a function with several callers
   jumps on its link register (the byte offset of one of these, set by a MOV before the CALL) */
u32 g_sm86_rets[64]; u32 g_sm86_nrets;
u32 g_sm86_brxb_idx[64], g_sm86_brxb_base[64]; u32 g_sm86_nbrxb;
static u32 g_brxb_at[64], g_brxb_val[64]; static u32 g_nbrxb;   /* while lowering: instruction position, base */
unsigned short g_sm86_untr[256]; int g_sm86_nuntr;   /* every opcode left untranslated so far (the runtime logs them) */
static u32 F(const W128* w, int lo, int n){
  u64 v; if(lo>=64) v=w->hi>>(lo-64); else if(lo+n<=64) v=w->lo>>lo; else v=(w->lo>>lo)|(w->hi<<(64-lo));
  return (u32)(v & ((n>=32)? 0xFFFFFFFFull : ((1ull<<n)-1))); }

/* ---- output */
typedef struct { u64* m; u32 n, cap; u32* amap; u32 namp; struct { u32 at, tgt, kind; } *fx; u32 nfx, cfx;
  int bad; u32 first_bad; u32 bad_op; u64 guard; int maxreg; } Lw;

static void put(Lw* L, u64 v){ if(L->n==L->cap){ L->cap=L->cap? L->cap*2 : 256; L->m=realloc(L->m,L->cap*8); } L->m[L->n++]=v|L->guard; }

/* opcode bits 63..48 from shader.c's pattern ('-' = 0) */
static u64 P(const char* s){ u64 v=0; for(int i=0;i<16 && s[i];i++) if(s[i]=='1') v|=1ull<<(63-i); return v; }
#define FLD(v,lo) ((u64)(v)<<(lo))

static const W128* g_cur;
/* uniform predicates UP0..UP6 live in Maxwell predicates the shader doesn't use (picked per shader in sm86_lower),
   8 = no free one left */
static int g_upmap[8];
static int g_upspill;   /* bit k: UPk lives in register 175-k (no free predicate for it) */
static u32 upm(u32 x){ x&=7; return x==7? 7u : (u32)g_upmap[x]; }   /* the instruction being lowered, for SWR_SM86_TRACE */
static void untr(Lw* L, u32 i, u32 op){ if(!L->bad){ L->first_bad=i; L->bad_op=op; } L->bad++;
  { static int on=-1; static unsigned short cnt[4096]; if(on<0) on=getenv("SWR_SM86_TRACE")!=NULL;
    if(on && g_cur && cnt[op&0xFFF]<6){ cnt[op&0xFFF]++; fprintf(stderr,"sm86 untr %03x #%u lo %016llx hi %016llx\n",op,i,(unsigned long long)g_cur->lo,(unsigned long long)g_cur->hi); } }
  if(op){ int k=0; for(;k<g_sm86_nuntr;k++) if(g_sm86_untr[k]==op) break; if(k==g_sm86_nuntr && g_sm86_nuntr<256) g_sm86_untr[g_sm86_nuntr++]=(unsigned short)op; (void)i; }
  /* an opcode shader.c doesn't know: reported as "unknown" and counted as unsupported */
  put(L,0xFFFF000000000000ull|((u64)op<<32)|i); }

/* ---- registers */
static u32 gr(Lw* L, u32 r){ if(r!=RZ && (int)r>L->maxreg) L->maxreg=(int)r; return r; }
static u32 ur(u32 u){ u&=63; return u==63? RZ : UBASE+u; }

/* ---- operands */
enum { K_R, K_I, K_C };
typedef struct { int k; u32 v; u32 idx, off; int neg, abs; } Op;

/* slot at bit 32 for the given form (1 reg, 4 imm, 5 cbuf, 6 ureg; 2/3/7 = imm/cbuf/ureg too) */
static Op slot32(Lw* L, const W128* w, u32 form){
  Op o={0}; o.abs=F(w,62,1); o.neg=F(w,63,1);
  switch(form){
  case 1: o.k=K_R; o.v=gr(L,F(w,32,8)); break;
  case 2: case 4: o.k=K_I; o.v=F(w,32,32); o.abs=o.neg=0; break;
  case 3: case 5: o.k=K_C; o.off=F(w,38,16); o.idx=F(w,54,5); break;
  default: o.k=K_R; o.v=ur(F(w,32,6)); break; }
  return o; }
static Op slot64(Lw* L, const W128* w){ Op o={0}; o.k=K_R; o.v=gr(L,F(w,64,8)); o.abs=F(w,74,1); o.neg=F(w,75,1); return o; }
static Op src0(Lw* L, const W128* w, int uni){ Op o={0}; o.k=K_R; o.v= uni? ur(F(w,24,6)) : gr(L,F(w,24,8)); o.abs=F(w,73,1); o.neg=F(w,72,1); return o; }

/* B and C of a 3 source ALU op */
static void bc(Lw* L, const W128* w, u32 form, int uni, Op* b, Op* c){
  if(form==2||form==3||form==7){ *b=slot64(L,w); *c=slot32(L,w,form); }
  else { *b=slot32(L,w,form); *c=slot64(L,w); }
  if(uni){ if(b->k==K_R && form==1) b->v=ur(F(w,32,6)); if(c->k==K_R && form!=7) c->v=ur(F(w,64,6)); } }

/* the stage of the next program lowered: 0 vertex/fragment/compute, 1 geometry, 2 tessellation (control or eval).
   set by the caller; Switch 2 geometry and tessellation programs use the stage buffer and OUT differently */
int g_sm86_stage;
static void mov32i(Lw* L, u32 d, u32 v){ put(L,P("000000010000----")|FLD(0xF,12)|FLD(v,20)|d); }
static void movr(Lw* L, u32 d, u32 s){ put(L,P("0101110010011---")|FLD(0xF,39)|FLD(s,20)|d); }
static void movc(Lw* L, u32 d, u32 idx, u32 off){ put(L,P("0100110010011---")|FLD(0xF,39)|FLD(idx,34)|FLD(off>>2,20)|d); }
/* an operand into a register (scratch if needed) */
static u32 toreg(Lw* L, Op* o, u32 scratch){
  if(o->k==K_R) return o->v;
  if(o->k==K_I) mov32i(L,scratch,o->v); else movc(L,scratch,o->idx,o->off);
  o->k=K_R; o->v=scratch; return scratch; }
static int fits20i(u32 v){ int32_t s=(int32_t)v; return s>=-(1<<19) && s<(1<<19); }
static int fits20f(u32 v){ return (v&0xFFF)==0; }
/* a float operand with |x| or -x applied, for the places the Maxwell form has no modifier bits: FADD into a scratch */
static void fmod_fix(Lw* L, Op* o, u32 scratch){
  if(!o->abs && !o->neg) return;
  if(o->k==K_I){ if(o->abs) o->v&=0x7FFFFFFFu; if(o->neg) o->v^=0x80000000u; o->abs=o->neg=0; return; }
  int ab=o->abs, ng=o->neg; o->abs=o->neg=0; u32 src=toreg(L,o,scratch);
  put(L,P("0101110001011---")|FLD(RZ,20)|FLD(src,8)|scratch|FLD(ng,48)|FLD(ab,46)); o->k=K_R; o->v=scratch; }

/* Maxwell B operand for the R/C/I forms: returns the form index (0 R, 1 C, 2 I) and the field bits */
static int mb(Lw* L, Op* o, int fl, u64* bits, u32 scratch){
  if(o->k==K_I && !(fl? fits20f(o->v) : fits20i(o->v))) toreg(L,o,scratch);
  if(o->k==K_R){ *bits=FLD(o->v,20); return 0; }
  if(o->k==K_C){ *bits=FLD(o->idx,34)|FLD((o->off>>2)&0x3FFF,20); return 1; }
  if(fl) *bits=FLD((o->v>>12)&0x7FFFF,20)|FLD(o->v>>31,56); else *bits=FLD(o->v&0x7FFFF,20)|FLD((o->v>>19)&1,56);
  return 2; }

/* ---- one instruction */
static void lower1(Lw* L, const W128* w, u32 i, u32 n);
static void lower1i(Lw* L, const W128* w, u32 i, u32 n){
  u32 op12=F(w,0,12), op9=op12&0x1FF, form=F(w,9,3);
  u32 pg=F(w,12,3), pn=F(w,15,1);
  u32 d=F(w,16,8);
  if(!w->lo && !w->hi){ put(L,P("0101000010110---")); return; }   /* zero padding (NVN2 leaves 0x30 bytes after the header) */
  int uni= ((op9&0x180)==0x080 && F(w,91,1)) || op12==0xc82 || op12==0x882;   /* uniform ALU copies (0x08x..0x0bx), UMOV */
  u32 o9= uni? (op9&0x17F) : op9;
  { int uop= uni || op12==0x886 || op12==0x89c || op12==0x883 || op12==0x9c3 || op12==0xab9 || op12==0xabb;
    if(uop && pg!=7){ pg=upm(pg); if(pg>7){ untr(L,i,op12); return; } } }
  L->guard=FLD(pg,16)|FLD(pn,19);
  if(uni || op12==0x9c3 || op12==0xab9 || op12==0xabb) d=ur(d); else gr(L,d);   /* S2UR, ULDC write uniform registers too */
  Op a=src0(L,w,uni), b, c;
  /* ------------------------------------------------ ALU (9 bit opcode + form) */
  if(op12<0x200 || (op12&0xE00)==0x200 || (op12&0xE00)==0x400 || (op12&0xE00)==0x600 || (op12&0xE00)==0x800 || (op12&0xE00)==0xA00 || (op12&0xE00)==0xC00 || (op12&0xE00)==0xE00){
    switch(o9){
    case 0x002: /* MOV */
      b=slot32(L,w,form);
      if(b.k==K_I) mov32i(L,d,b.v); else if(b.k==K_C) movc(L,d,b.idx,b.off); else movr(L,d,b.v);
      return;
    case 0x010: { /* IADD3 (.X at 74, carry out pred at 81) */
      bc(L,w,form,uni,&b,&c); int x=F(w,74,1); u32 cop=F(w,81,3);
      if(c.k==K_R && c.v==RZ && (x || cop!=7)){   /* 2 sources with carry: IADD.CC / IADD.X */
        u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110000010---","0100110000010---","0011100-00010---"};
        put(L,P(pt[f])|bb|FLD(a.v,8)|d|FLD(a.neg,49)|FLD(b.neg,48)|FLD(x,43)|FLD(cop!=7,47)); return; }
      if(c.k!=K_R) toreg(L,&c,S2);
      u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"010111001100----","010011001100----","0011100-1100----"};
      put(L,P(pt[f])|bb|FLD(c.v,39)|FLD(a.v,8)|d|FLD(a.neg,51)|FLD(b.neg,50)|FLD(c.neg,49)); return; }
    case 0x012: { /* LOP3 */
      bc(L,w,form,uni,&b,&c); u32 lut=F(w,72,8), po=F(w,81,3); if(uni){ po=upm(po); if(po>7){ untr(L,i,op12); return; } }
      if(c.k!=K_R) toreg(L,&c,S2);
      if(po!=7 && b.k!=K_R) toreg(L,&b,S1);
      u64 bb; int f=mb(L,&b,0,&bb,S1);
      if(f==0) put(L,P("0101101111100---")|bb|FLD(c.v,39)|FLD(a.v,8)|d|FLD(lut,28)|FLD(po,48)|FLD(3,36));
      else put(L,P(f==1? "0000001---------" : "001111----------")|bb|FLD(c.v,39)|FLD(a.v,8)|d|FLD(lut,48));
      return; }
    case 0x019: { /* SHF */
      bc(L,w,form,uni,&b,&c); u32 ty=F(w,73,2); int right=F(w,76,1), hi=F(w,80,1), wrap=F(w,75,1);
      if(c.k!=K_R) toreg(L,&c,S2);
      if(b.k==K_C) toreg(L,&b,S1);
      if(!right && !hi){ /* lo << s */
        u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110001001---","0100110001001---","0011100-01001---"};
        put(L,P(pt[f])|bb|FLD(a.v,8)|d|FLD(wrap,39)); return; }
      if(right && hi){ /* hi >> s */
        u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110000101---","0100110000101---","0011100-00101---"};
        put(L,P(pt[f])|bb|FLD(c.v,8)|d|FLD(wrap,39)|FLD(ty==0||ty==2,48)); return; }
      if(b.k==K_I){ put(L,P(right? "0011100-11111---" : "0011011-11111---")|FLD(b.v&63,20)|FLD(c.v,39)|FLD(a.v,8)|d); return; }
      put(L,P(right? "0101110011111---" : "0101101111111---")|FLD(b.v,20)|FLD(c.v,39)|FLD(a.v,8)|d); return; }
    case 0x025: { /* IMAD.WIDE(.U32) Rd:Rd+1 = a * b + Rc:Rc+1 (signed when bit 73), b a register or an immediate
                     (Ocarina of Time's pointer + offset math). our own pseudo op, shader.c does the 64 bit math */
      bc(L,w,form,uni,&b,&c); int sg=F(w,73,1);
      if(uni || F(w,81,3)!=7 || d==RZ || c.k!=K_R || (c.v!=RZ && (c.v&1))){ untr(L,i,op12); return; }
      if(b.k!=K_R) toreg(L,&b,S1);
      gr(L,d+1); put(L,P("1111000000010---")|d|FLD(a.v,8)|FLD(b.v,20)|FLD(c.v,39)|FLD(sg,48)); return; }
    case 0x024: case 0x027: { /* IMAD / IMAD.HI */
      bc(L,w,form,uni,&b,&c); int sg=F(w,73,1), hi= o9==0x027;
      if(!hi && a.v==RZ && b.k==K_R && b.v==RZ){ /* IMAD.MOV */
        if(c.k==K_I) mov32i(L,d,c.v); else if(c.k==K_C) movc(L,d,c.idx,c.off); else movr(L,d,c.v); return; }
      if(c.k==K_I || (c.k==K_C && b.k!=K_R)) toreg(L,&c,S2);
      u64 hb= hi? FLD(1,54)|FLD(sg,48)|FLD(sg,53) : 0;
      if(c.k==K_C){ put(L,P("010100100-------")|FLD(b.v,39)|FLD(c.idx,34)|FLD((c.off>>2)&0x3FFF,20)|FLD(a.v,8)|d|hb); return; }
      u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"010110100-------","010010100-------","0011010-0-------"};
      put(L,P(pt[f])|bb|FLD(c.v,39)|FLD(a.v,8)|d|hb); return; }
    case 0x00c: { /* ISETP */
      if(F(w,72,1)){ untr(L,i,op12); return; }    /* .EX (64 bit compare) */
      b=slot32(L,w,form); if(uni && form==1) b.v=ur(F(w,32,6));
      u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"010110110110----","010010110110----","0011011-0110----"};
      u32 p1=F(w,81,3), p2=F(w,84,3), pc=F(w,87,3), pcn=F(w,90,1);
      if(uni){ p1=upm(p1); p2=upm(p2); pc=upm(pc); if(p1>7||p2>7||pc>7){ untr(L,i,op12); return; } }
      put(L,P(pt[f])|bb|FLD(a.v,8)|FLD(p1,3)|FLD(p2,0)|FLD(pc,39)|FLD(pcn,42)|FLD(F(w,74,2),45)|FLD(F(w,73,1),48)|FLD(F(w,76,3),49)); return; }
    case 0x00b: { /* FSETP */
      b=slot32(L,w,form); u64 bb; int f=mb(L,&b,1,&bb,S1); static const char* pt[3]={"010110111011----","010010111011----","0011011-1011----"};
      put(L,P(pt[f])|bb|FLD(a.v,8)|FLD(F(w,81,3),3)|FLD(F(w,84,3),0)|FLD(F(w,87,3),39)|FLD(F(w,90,1),42)|FLD(F(w,74,2),45)|FLD(F(w,76,4),48)|
          FLD(a.neg,43)|FLD(a.abs,7)|FLD(b.neg,6)|FLD(b.abs,44)|FLD(F(w,80,1),47)); return; }
    case 0x01a: { /* SGXT: sign/zero extend from b bits -> BFE with position 0, length b */
      b=slot32(L,w,form); if(uni && form==1) b.v=ur(F(w,32,6)); int sg=F(w,73,1);
      if(b.k==K_I){ put(L,P("0011100-00000---")|FLD(((b.v&0xFF)<<8)&0x7FFFF,20)|FLD(a.v,8)|d|FLD(sg,48)); return; }
      u32 r=toreg(L,&b,S1); put(L,P("0011100-01001---")|FLD(8,20)|FLD(r,8)|S1);   /* S1 = b << 8 */
      put(L,P("0101110000000---")|FLD(S1,20)|FLD(a.v,8)|d|FLD(sg,48)); return; }
    case 0x03e: if(uni){ /* UBREV */
      u32 s=ur(F(w,32,6)); put(L,P("0011100-00101---")|FLD(s,8)|d|FLD(1,39)|FLD(1,40)); return; }
      { /* F2FP.PACK_AB: d = half(a) << 16 | half(b) */
      b=slot32(L,w,form); u32 rb=toreg(L,&b,S1);
      put(L,P("0101110010101---")|FLD(rb,20)|SA|FLD(1,8)|FLD(2,10));            /* SA = f16(b) */
      if(a.v==RZ){ movr(L,d,SA); return; }
      put(L,P("0101110010101---")|FLD(a.v,20)|SB|FLD(1,8)|FLD(2,10));           /* SB = f16(a) */
      put(L,P("0011100-01001---")|FLD(16,20)|FLD(SB,8)|SB);                     /* SB <<= 16 */
      put(L,P("0101101111100---")|FLD(SB,20)|FLD(RZ,39)|FLD(SA,8)|d|FLD(0xFC,28)|FLD(7,48)|FLD(3,36)); return; }
    case 0x00a: { /* FSET (.BF: 1.0 / 0.0) */
      b=slot32(L,w,form); u64 bb; int f=mb(L,&b,1,&bb,S1); static const char* pt[3]={"01011000--------","01001000--------","0011000---------"};
      put(L,P(pt[f])|bb|FLD(a.v,8)|d|FLD(F(w,87,3),39)|FLD(F(w,90,1),42)|FLD(F(w,74,2),45)|FLD(F(w,76,4),48)|FLD(1,52)|
          FLD(a.neg,43)|FLD(a.abs,54)|FLD(b.neg,53)|FLD(b.abs,44)|FLD(F(w,80,1),55)); return; }
    case 0x03d: if(uni){ /* UFLO */
      u32 s=ur(F(w,32,6)); put(L,P("0101110000110---")|FLD(s,20)|d|FLD(F(w,63,1),40)|FLD(F(w,73,1),48)|FLD(F(w,74,1),41)); return; } break;
    case 0x03f: if(uni){ /* UPOPC */
      u32 s=ur(F(w,32,6)); put(L,P("0101110000001---")|FLD(s,20)|d|FLD(F(w,63,1),40)); return; } break;
    case 0x021: { /* FADD */
      b=slot32(L,w,form); u32 sat=F(w,77,1);
      if(b.k==K_I && sat) toreg(L,&b,S1);   /* FADD32I has no .SAT */
      if(b.k==K_I){ put(L,P("000010----------")|FLD(b.v,20)|FLD(a.v,8)|d|FLD(a.neg,56)|FLD(a.abs,54)); return; }
      u64 bb; int f=mb(L,&b,1,&bb,S1); static const char* pt[3]={"0101110001011---","0100110001011---","0011100-01011---"};
      put(L,P(pt[f])|bb|FLD(a.v,8)|d|FLD(a.neg,48)|FLD(a.abs,46)|FLD(b.neg,45)|FLD(b.abs,49)|FLD(sat,50)); return; }
    case 0x020: { /* FMUL */
      b=slot32(L,w,form); u32 sat=F(w,77,1), pdiv=F(w,84,3);
      static const u8 scale[8]={0,3,2,1,0,6,5,4};   /* Ampere D8..M8 -> Maxwell's scale order */
      if(a.abs){ fmod_fix(L,&a,SA); } if(b.abs){ fmod_fix(L,&b,SB); }
      int ng=a.neg^b.neg;
      /* FMUL32I has no scale field, a scaled immediate goes through a register */
      if(b.k==K_I && pdiv==4){ u32 v=b.v^(ng? 0x80000000u : 0); put(L,P("00011110--------")|FLD(v,20)|FLD(a.v,8)|d|FLD(sat,55)); return; }
      if(b.k==K_I) toreg(L,&b,S1);
      u64 bb; int f=mb(L,&b,1,&bb,S1); static const char* pt[3]={"0101110001101---","0100110001101---","0011100-01101---"};
      put(L,P(pt[f])|bb|FLD(a.v,8)|d|FLD(ng,48)|FLD(sat,50)|FLD(scale[pdiv],41)|FLD(F(w,76,1)? 2 : 0,44)); return; }
    case 0x023: { /* FFMA */
      bc(L,w,form,0,&b,&c); u32 sat=F(w,77,1); u64 fmz=FLD(F(w,76,1)? 2 : 0,53);
      if(a.abs) fmod_fix(L,&a,SA);
      if(b.abs) fmod_fix(L,&b,SB);
      if(c.abs) fmod_fix(L,&c,S2);
      int ng=a.neg^b.neg;
      if(c.k==K_I) toreg(L,&c,S2);
      if(c.k==K_C){ if(b.k!=K_R) toreg(L,&b,S1);
        put(L,P("010100011-------")|FLD(b.v,39)|FLD(c.idx,34)|FLD((c.off>>2)&0x3FFF,20)|FLD(a.v,8)|d|FLD(ng,48)|FLD(c.neg,49)|FLD(sat,50)|fmz); return; }
      if(b.k==K_I && !fits20f(b.v) && c.v==d && !b.neg){
        put(L,P("000011----------")|FLD(b.v,20)|FLD(a.v,8)|d|FLD(a.neg,56)|FLD(c.neg,57)|FLD(sat,55)|fmz); return; }
      u64 bb; int f=mb(L,&b,1,&bb,S1); static const char* pt[3]={"010110011-------","010010011-------","0011001-1-------"};
      put(L,P(pt[f])|bb|FLD(c.v,39)|FLD(a.v,8)|d|FLD(ng,48)|FLD(c.neg,49)|FLD(sat,50)|fmz); return; }
    case 0x009: { /* FMNMX */
      b=slot32(L,w,form); u64 bb; int f=mb(L,&b,1,&bb,S1); static const char* pt[3]={"0101110001100---","0100110001100---","0011100-01100---"};
      put(L,P(pt[f])|bb|FLD(a.v,8)|d|FLD(F(w,87,3),39)|FLD(F(w,90,1),42)|FLD(a.neg,48)|FLD(a.abs,46)|FLD(b.neg,45)|FLD(b.abs,49)); return; }
    case 0x007: case 0x008: { /* SEL, FSEL */
      b=slot32(L,w,form); if(uni && form==1) b.v=ur(F(w,32,6));
      if(o9==0x008){ fmod_fix(L,&a,SA); fmod_fix(L,&b,SB); }
      u32 sp=F(w,87,3); if(uni){ sp=upm(sp); if(sp>7){ untr(L,i,op12); return; } }
      u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110010100---","0100110010100---","0011100-10100---"};
      put(L,P(pt[f])|bb|FLD(a.v,8)|d|FLD(sp,39)|FLD(F(w,90,1),42)); return; }
    case 0x017: { /* IMNMX */
      b=slot32(L,w,form); u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110000100---","0100110000100---","0011100-00100---"};
      put(L,P(pt[f])|bb|FLD(a.v,8)|d|FLD(F(w,87,3),39)|FLD(F(w,90,1),42)|FLD(F(w,73,1),48)); return; }
    case 0x016: { /* PRMT (index mode) */
      if(F(w,72,3)){ untr(L,i,op12); return; }
      bc(L,w,form,uni,&b,&c); if(c.k!=K_R) toreg(L,&c,S2);
      u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"010110111100----","010010111100----","0011011-1100----"};
      put(L,P(pt[f])|bb|FLD(c.v,39)|FLD(a.v,8)|d); return; }
    case 0x011: { /* LEA / LEA.HI (the carry of .X is dropped) */
      bc(L,w,form,uni,&b,&c); u32 sh=F(w,75,5); int hi=F(w,80,1);
      if(hi){ if(c.k!=K_R) toreg(L,&c,S2); if(b.k!=K_R && b.k!=K_C) toreg(L,&b,S1);
        if(b.k==K_C) put(L,P("000110----------")|FLD(b.idx,34)|FLD((b.off>>2)&0x3FFF,20)|FLD(c.v,39)|FLD(a.v,8)|d|FLD(sh,51)|FLD(F(w,72,1),56));
        else put(L,P("0101101111011---")|FLD(b.v,20)|FLD(c.v,39)|FLD(a.v,8)|d|FLD(sh,28)|FLD(F(w,72,1),37));
        return; }
      if(b.neg && b.k!=K_I){ /* LEA Rd, Ra, -Rb (bit 63): Maxwell's LEA can't negate Rb, ISCADD can (bit 48). MKW's
                                alpha dither computes a*255 as (a << 8) - a and cut the wrong pixels with the sign lost */
        int nb=b.neg; if(b.k!=K_R) toreg(L,&b,S1);
        put(L,P("0101110000011---")|FLD(b.v,20)|FLD(a.v,8)|d|FLD(sh,39)|FLD(nb,48)|FLD(F(w,72,1),49)); return; }
      u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101101111010---","010010111101----","0011011-11010---"};
      put(L,P(pt[f])|bb|FLD(a.v,8)|d|FLD(sh,39)|FLD(F(w,72,1),45)|FLD(7,48)); return; }
    case 0x013: { /* IABS -> I2I.S32.S32 |x| */
      b=slot32(L,w,form); u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110011100---","0100110011100---","0011100-11100---"};
      put(L,P(pt[f])|bb|d|FLD(2,8)|FLD(2,10)|FLD(1,12)|FLD(1,13)|FLD(1,49)); return; }
    case 0x108: { /* MUFU */
      b=slot32(L,w,form); u32 fn=F(w,74,6);
      if(fn==6||fn==7||fn>9||F(w,73,1)){ untr(L,i,op12); return; }   /* 9: TANH (Fire Emblem), shader.c knows it */
      if(fn<=1) fmod_fix(L,&b,S1);   /* -x / |x| applied before the 2pi scale: the Maxwell MUFU below gets no modifiers for SIN/COS */
      u32 s=toreg(L,&b,S1);
      if(fn<=1){ put(L,P("00011110--------")|FLD(0x40C90FDBu,20)|FLD(s,8)|S2); s=S2; } /* Ampere takes x/2pi, shader.c's sin/cos radians */
      put(L,P("0101000010000---")|FLD(fn,20)|FLD(s,8)|d|FLD(fn<=1? 0 : b.neg,48)|FLD(fn<=1? 0 : b.abs,46)); return; }
    case 0x106: { /* I2F */
      b=slot32(L,w,form); u32 sel=b.k==K_I? 0 : F(w,60,2); if(F(w,84,2)==1) sel=(sel&1)<<1; u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110010111---","0100110010111---","0011100-10111---"};
      put(L,P(pt[f])|bb|d|FLD(F(w,75,2),8)|FLD(F(w,84,2),10)|FLD(F(w,74,1),13)|FLD(F(w,78,2),39)|FLD(sel,41)); return; }
    case 0x045: { /* I2FP.F32.S32/U32 (Ampere): I2F with the sizes fixed at 32 bits, signed in bit 74, rounding 78-79.
                    Mario Kart World's last pass builds its texcoords with it (the screen came out one flat colour) */
      b=slot32(L,w,form); u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110010111---","0100110010111---","0011100-10111---"};
      put(L,P(pt[f])|bb|d|FLD(2,8)|FLD(2,10)|FLD(F(w,74,1),13)|FLD(F(w,78,2),39)); return; }
    case 0x105: { /* F2I */
      b=slot32(L,w,form); u64 bb; int f=mb(L,&b,1,&bb,S1); static const char* pt[3]={"0101110010110---","0100110010110---","0011100-10110---"};
      put(L,P(pt[f])|bb|d|FLD(F(w,75,2),8)|FLD(F(w,84,2),10)|FLD(F(w,72,1),12)|FLD(F(w,78,2),39)|FLD(b.neg,49)|FLD(b.abs,45)); return; }
    case 0x104: case 0x107: { /* F2F, FRND */
      b=slot32(L,w,form); u32 sel=b.k==K_I? 0 : F(w,60,1); u64 bb; int f=mb(L,&b,1,&bb,S1); static const char* pt[3]={"0101110010101---","0100110010101---","0011100-10101---"};
      put(L,P(pt[f])|bb|d|FLD(F(w,75,2),8)|FLD(F(w,84,2),10)|FLD(F(w,78,2),39)|FLD(o9==0x107,42)|FLD(sel,41)|FLD(b.neg,45)|FLD(b.abs,49)); return; }
    case 0x109: { /* POPC */
      b=slot32(L,w,form); int nt=b.neg; b.neg=0; u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110000001---","0100110000001---","0011100-00001---"};
      put(L,P(pt[f])|bb|d|FLD(nt,40)); return; }
    case 0x100: { /* FLO */
      b=slot32(L,w,form); int nt=b.neg; b.neg=0; u64 bb; int f=mb(L,&b,0,&bb,S1); static const char* pt[3]={"0101110000110---","0100110000110---","0011100-00110---"};
      put(L,P(pt[f])|bb|d|FLD(nt,40)|FLD(F(w,73,1),48)|FLD(F(w,74,1),41)); return; }
    case 0x101: { /* BREV -> SHR.W with the reverse bit, by 0 */
      b=slot32(L,w,form); u32 s=toreg(L,&b,S1); put(L,P("0011100-00101---")|FLD(s,8)|d|FLD(1,39)|FLD(1,40)); return; }
    case 0x030: { /* HADD2, register form (the HUD glyph shader turns its coverage into a float with it) */
      if(form!=1){ untr(L,i,op12); return; }
      u32 rb=gr(L,F(w,32,8));
      put(L,P("0101110100010---")|FLD(a.v,8)|FLD(rb,20)|d|FLD(F(w,74,2),47)|FLD(F(w,60,2),28)|
          FLD(F(w,72,1),43)|FLD(F(w,73,1),44)|FLD(F(w,63,1),31)|FLD(F(w,62,1),30)|FLD(F(w,77,1),32)|FLD(F(w,78,1),49)); return; }
    case 0x031: case 0x035: { /* HFMA2 used as a move: HFMA2.MMA Rd, -RZ, RZ, imm */
      bc(L,w,form,0,&b,&c);
      if(a.v==RZ && b.k==K_R && b.v==RZ){ if(c.k==K_I) mov32i(L,d,c.v); else if(c.k==K_C) movc(L,d,c.idx,c.off); else movr(L,d,c.v); return; }
      if(a.v==RZ && b.k==K_I && b.v==0){ if(c.k==K_R) movr(L,d,c.v); else untr(L,i,op12); return; }
      untr(L,i,op12); return; }
    case 0x148: return;   /* WARPSYNC */
    case 0x022: { /* FSWZADD (immediate lane codes only): the quad add behind derivatives. Ampere's lane codes 1 and 2
                     are Maxwell's 2 and 1 (a-b / b-a), the rest is the same instruction */
      if(op12!=0x822) break; u32 sw=F(w,32,32); if(sw>0xFF || F(w,72,8)){ untr(L,i,op12); return; }
      u32 mx=((sw&0x55)<<1)|((sw&0xAA)>>1);
      put(L,P("0101000011111---")|FLD(gr(L,F(w,24,8)),8)|FLD(gr(L,F(w,64,8)),20)|FLD(mx,28)|d); return; }
    default: break; }
  }
  /* ------------------------------------------------ everything else (12 bit opcodes) */
  switch(op12){
  case 0x919: case 0x9c3: put(L,P("1111000011001---")|FLD(F(w,72,8),20)|d); return;               /* S2R, S2UR */
  case 0x805: put(L,P("0101000011001---")|FLD(F(w,72,8),20)|d); return;                         /* CS2R */
  case 0xb82: case 0xab9: case 0xabb: { /* LDC, ULDC, ULDC indexed by a uniform register (c[bank][URi + off]) */
    u32 sz=F(w,73,3), ra= op12==0xab9? RZ : op12==0xabb? (F(w,24,6)==63? RZ : ur(F(w,24,6))) : gr(L,F(w,24,8)); i32 off=(int16_t)F(w,38,16); u32 idx=F(w,54,5);
    if(sz==6){ for(int k=0;k<2;k++) put(L,P("1110111110010---")|FLD(ra,8)|(d==RZ? RZ : d+2*k)|FLD(idx,36)|FLD((u32)(off+8*k)&0xFFFF,20)|FLD(5,48)); return; }
    put(L,P("1110111110010---")|FLD(ra,8)|d|FLD(idx,36)|FLD((u32)off&0xFFFF,20)|FLD(sz,48)); return; }
  case 0x326: { /* IPA */
    u32 freq=F(w,78,2); put(L,P("11100000--------")|FLD(F(w,64,8),30)|FLD(RZ,8)|FLD(RZ,20)|FLD(RZ,39)|d|FLD(freq==1? 2 : 0,54)|FLD(F(w,76,2)==1,52)); return; }
  case 0x321: { /* ALD */
    u32 vtx=gr(L,F(w,32,8)), off=gr(L,F(w,24,8));
    put(L,P("1110111111011---")|FLD(F(w,40,10),20)|FLD(F(w,74,2),47)|FLD(F(w,79,1),32)|FLD(vtx!=RZ? vtx : off,8)|FLD(vtx,39)|d|FLD(F(w,77,1),31)); return; }
  case 0x322: { /* AST */
    u32 data=gr(L,F(w,32,8)), vtx=gr(L,F(w,64,8)), off=gr(L,F(w,24,8));
    put(L,P("1110111111110---")|FLD(F(w,40,10),20)|FLD(F(w,74,2),47)|FLD(off,8)|FLD(vtx,39)|data|FLD(F(w,77,1),31)); return; }
  case 0x95b: put(L,P("111000110011----")|0xF); return;                                         /* KILL */
  case 0x94d: put(L,P("111000110000----")|0xF); return;                                         /* EXIT */
  case 0x547: { u32 up=upm(F(w,24,3)); if(up>7){ untr(L,i,op12); return; }   /* BRA.U with a uniform predicate */
    if(pg!=7 || pn){ untr(L,i,op12); return; } L->guard=FLD(up,16)|FLD(F(w,27,1),19); op12=0x947; }
    /* fall through */
  case 0x944: case 0x947: case 0x945: { /* CALL (MKW only calls blocks that never return: a jump), BRA, BSSY */
    /* BRA's offset low bits: 1 .U, 2 .DIV (taken when the warp has diverged), 3 .CONV (when the active threads are the
       mask in bit 91's uniform register). they guard warp synchronous fast paths; threads run converged here, so
       .DIV is never taken and .CONV always is (as Lichii does) */
    if(op12==0x947 && F(w,0,12)==0x947 && F(w,32,2)==2){ if(F(w,91,1)){ untr(L,i,op12); return; } return; }
    if(op12==0x947 && F(w,0,12)==0x947 && F(w,32,2)<=1 && F(w,91,1)){ untr(L,i,op12); return; }
    if(op12==0x944) op12=0x947;
    int64_t off; if(op12==0x947){ off=(int64_t)((((u64)F(w,34,30))|((u64)F(w,64,18)<<30))<<16)>>16; }
    else off=(int64_t)((u64)F(w,34,30)<<34)>>34;
    int64_t t=(int64_t)i+1+off/4;      /* offsets count 32 bit words from the next instruction (NAK's get_rel_offset) */
    if(op12==0x947){ u32 cp=F(w,87,3), cn=F(w,90,1);
      if(cp!=7 || cn){ /* @Pg BRA Pc: taken when both hold. Maxwell has one predicate: @!Pg BRA over it, then @Pc BRA */
        if(pg!=7 || pn){ if(i+1>n){ untr(L,i,op12); return; }
          if(L->nfx==L->cfx){ L->cfx=L->cfx? L->cfx*2 : 64; L->fx=realloc(L->fx,L->cfx*sizeof *L->fx); }
          L->guard=FLD(pg,16)|FLD(!pn,19); L->fx[L->nfx].at=L->n; L->fx[L->nfx].tgt=i+1; L->fx[L->nfx].kind=0; L->nfx++; put(L,P("111000100100----")|0xF); }
        L->guard=FLD(cp,16)|FLD(cn,19); }
      /* bit 32 is .U (the branch is warp uniform, Bananza / Fast Fusion): only a hint for us. bits 82-86 and 92+ are
         convergence modes we don't model; bit 91 names a convergence mask register (B0..), a plain jump here */
      if(F(w,0,12)==0x947 && (F(w,82,5) || F(w,92,13))){ untr(L,i,op12); return; } }   /* CALL sets bit 86 */
    if(t<0 || t>(int64_t)n){ untr(L,i,op12); return; }
    if(L->nfx==L->cfx){ L->cfx=L->cfx? L->cfx*2 : 64; L->fx=realloc(L->fx,L->cfx*sizeof *L->fx); }
    L->fx[L->nfx].at=L->n; L->fx[L->nfx].tgt=(u32)t; L->fx[L->nfx].kind=op12==0x945; L->nfx++;
    put(L, op12==0x947? P("111000100100----")|0xF : P("111000101001----")); return; }
  case 0x950: { /* RET: CALL is lowered as a jump, so a function called from one place returns with a jump back there */
    int t= (g_ret && i<g_nret)? g_ret[i] : -1;
    if(t==-2 && pg==7){ /* RET Ra with an absolute target (pc + 16 + offset == 0, as the Fire Emblem / Fast Fusion
                           libraries do): a jump on the link register's value over the return points */
      int64_t rel=(int64_t)((((u64)F(w,32,32))|((u64)F(w,64,18)<<32))<<14)>>14;
      if((int64_t)i*16+16+rel==0 && F(w,24,8)!=RZ){ put(L,P("111000100101----")|0xF|FLD(gr(L,F(w,24,8)),8)|FLD(SM86_RET_NATIVE,20)); return; } }
    if(t<0 || pg!=7){ untr(L,i,op12); return; }
    if(L->nfx==L->cfx){ L->cfx=L->cfx? L->cfx*2 : 64; L->fx=realloc(L->fx,L->cfx*sizeof *L->fx); }
    L->fx[L->nfx].at=L->n; L->fx[L->nfx].tgt=(u32)t; L->fx[L->nfx].kind=0; L->nfx++; put(L,P("111000100100----")|0xF); return; }
  case 0x941: put(L,P("1111000011111---")|0xF); return;                                         /* BSYNC -> SYNC */
  case 0x804: { /* R2P PR, Ra, immediate mask, byte select (bits 76-77): predicates from register bits (UI vertex shaders
                   of Mario Kart World pick their texcoord path this way) */
    u32 mask=F(w,32,32); if(F(w,16,8) || mask>0x7F){ untr(L,i,op12); return; }
    put(L,P("0011100011110---")|FLD(gr(L,F(w,24,8)),8)|FLD(mask,20)|FLD(F(w,76,2),41)); return; }
  case 0x98e: case 0x3a8: case 0x9a8: { /* RED / ATOMG: global atomics (types 0 u32, 1 s32, 3 f32 go through; ops ADD MIN MAX INC DEC AND OR XOR as Maxwell) */
    u32 addr=gr(L,F(w,24,8)), data=gr(L,F(w,32,8)), ty=F(w,73,3), aop=F(w,87,4); i32 off=(i32)(F(w,40,24)<<8)>>8;
    if(aop>7 || (ty!=0 && ty!=1 && ty!=3) || off<-(1<<19) || off>=(1<<19)){ untr(L,i,op12); return; }
    if(op12==0x9a8 && F(w,91,1) && (F(w,64,6)&63)!=63){ put(L,P("0101110000010---")|FLD(addr,8)|FLD(ur(F(w,64,6)),20)|S2); addr=S2; }
    if(op12==0x98e) put(L,P("1110101111111---")|FLD(addr,8)|data|FLD(ty,20)|FLD(aop,23)|FLD((u32)off&0xFFFFF,28)|FLD(1,48));
    else put(L,P("11101101--------")|FLD(addr,8)|d|FLD(data,20)|FLD(ty,49)|FLD(aop,52)|FLD((u32)off&0xFFFFF,28)|FLD(1,48));
    return; }
  case 0x243: { /* F2IP.U8.F32 Rd, Ra, Rb, RZ: Rb as a byte in bits 0-7, Ra in 8-15 (RZ: none), NaN 0, clamped to 0..255,
                   rounding from bits 78-79 (Ocarina of Time packs colours this way). pseudo op for shader.c */
    if(F(w,64,8)!=255 || F(w,32,32)>255){ untr(L,i,op12); return; }
    put(L,P("1111000000001---")|d|FLD(gr(L,F(w,24,8)),8)|FLD(gr(L,F(w,32,8)),20)|FLD(F(w,78,2),39)); return; }
  case 0xfae: { /* LDGSTS.128 [Rs + soff], [Rg.64 + goff]: an async global -> shared copy, done at once here (LDG into
                   the tex result registers, STS from them). a disabled copy (predicate at 87-89, negated by 90) stores
                   zeros */
    u32 sh=gr(L,d), gl=gr(L,F(w,24,8)); i32 goff=(i32)(F(w,32,12)<<20)>>20, soff=((i32)(F(w,48,16)<<16))>>12;
    /* two encodings: the promoted copy (negate in bit 90) and the one with a cache descriptor in a uniform register
       pair (bits 64-69, negate in bit 70; the descriptor is only a cache policy) */
    u32 w2=(u32)w->hi; int promoted=(w2 & ~0x0780003Fu)==0x08101D40u, desc=(w2 & ~0x0380007Fu)==0x0C101C00u;
    u32 ep=F(w,87,3), en= promoted? F(w,90,1) : F(w,70,1);
    if(F(w,44,4) || (!promoted && !desc) || (gl!=RZ && (gl&1))){ untr(L,i,op12); return; }
    for(u32 q=0;q<4;q++) put(L,P("0101110010011---")|FLD(0xF,39)|FLD(RZ,20)|(TO+q));
    { u64 g=L->guard; if(ep!=7 || en) L->guard=FLD(ep,16)|FLD(en,19);
      put(L,P("1110111011010---")|FLD(gl,8)|TO|FLD((u32)goff&0xFFFFFF,20)|FLD(6,48)|FLD(1,45)); L->guard=g; }
    put(L,P("1110111101011---")|FLD(sh,8)|TO|FLD((u32)soff&0xFFFFFF,20)|FLD(6,48)); return; }
  case 0x949: { /* BRX Ra: jumps to (pc + 16 + offset) + Ra, Ra from a jump table (LDC). the base is usually 0, the
                   program start; MKW's vertex shaders count from 0x30. Maxwell's BRX is relative, so it gets a marker
                   offset and shader.c maps the table's entries through g_sm86_nmap (Ampere instruction -> lowered
                   word), the base from g_sm86_brxb */
    int64_t rel=(int64_t)((((u64)F(w,32,32))|((u64)F(w,64,18)<<32))<<14)>>14, base=(int64_t)i*16+16+rel;
    if(pg!=7 || pn || F(w,82,14)!=(0x03800000u>>18) || base<0 || base>=(int64_t)n*16 || (base&15)){ untr(L,i,op12); return; }
    if(base){ if(g_nbrxb>=64){ untr(L,i,op12); return; } g_brxb_at[g_nbrxb]=L->n; g_brxb_val[g_nbrxb]=(u32)base; g_nbrxb++; }
    put(L,P("111000100101----")|0xF|FLD(gr(L,F(w,24,8)),8)|FLD(SM86_BRX_NATIVE,20)); return; }
  case 0x59f: { /* SURED.D.2D.ADD.STRONG.GPU U32 with a bound handle: MKW's exposure histogram (+1 per pixel into a
                   64x17 R32UI image). same check as Eglantine's lowering: words[2]==0x0010e000, 2D, bank<18 */
    u32 hi32=(u32)(w->hi&0xFFFFFFFFu), dim=F(w,61,3), hoff=F(w,40,14), bank=F(w,54,5);
    if(hi32!=0x0010e000u || dim!=3 || bank>=18 || hoff>0x1FFF || F(w,59,2)){ untr(L,i,op12); return; }
    if(g_tex_bank<0) g_tex_bank=(int)bank;
    put(L,P("11101011010-----")|FLD(gr(L,F(w,32,8)),0)|FLD(gr(L,F(w,24,8)),8)|FLD(3,33)|FLD(hoff&0x1FFF,36)|FLD(1,51)); return; }
  case 0x998: case 0x99c: { /* SULD.P / SUST.P with the handle in a register (bits 64-71): Maxwell's SULD / SUST with the
                               handle at 39 (bit 51 clear), which shader.c tracks back to its cbuf. dims 3-5 */
    int st= op12==0x99c; u32 data= st? gr(L,F(w,32,8)) : d, crd=gr(L,F(w,24,8)), hd=gr(L,F(w,64,8));
    u32 ch=F(w,72,4), dim=F(w,61,3), w1=F(w,32,32), w2=(u32)(w->hi&0xFFFFFFFFu);
    if((w1 & ~(st? 0xE00000FFu : 0xE0000000u)) || (w2&~0xFFFu)!=(st? 0x00100000u : 0x001E0000u) || dim<3 || dim>5 || !ch ||
       (st && ch!=1 && ch!=3 && ch!=15)){ untr(L,i,op12); return; }
    put(L,P(st? "11101011001-----" : "11101011000-----")|data|FLD(crd,8)|FLD(ch,20)|FLD(dim,33)|FLD(hd,39)); return; }
  case 0xb9d: { /* SUST.D.BA.32 (byte addressed x, 2D) with a bound handle (Fast Fusion's byte stores) */
    u32 hoff=F(w,40,14), bank=F(w,54,5);
    if((u32)(w->hi&0xFFFFFFFFu)!=0x0010a900u || F(w,61,3)!=3 || F(w,16,8) || bank>=18 || hoff>0x1FFF || F(w,59,2)){ untr(L,i,op12); return; }
    if(g_tex_bank<0) g_tex_bank=(int)bank;
    put(L,P("11101011001-----")|FLD(gr(L,F(w,32,8)),0)|FLD(gr(L,F(w,24,8)),8)|FLD(4,20)|FLD(1,23)|FLD(3,33)|FLD(hoff,36)|FLD(1,51)|FLD(1,52)); return; }
  case 0xb97: case 0xb9b: { /* SULD / SUST with a bound handle (c[bank][offset]), formatted with a component mask */
    u32 dim=F(w,61,3), hoff=F(w,40,14), mask=F(w,72,4); if(!mask) mask=1;
    u32 ra=gr(L,F(w,24,8)), rd= op12==0xb9b? gr(L,F(w,32,8)) : d;
    if(dim==1 || dim>5){ untr(L,i,op12); return; }
    put(L,P(op12==0xb97? "11101011000-----" : "11101011001-----")|FLD(1,51)|FLD(dim,33)|FLD(hoff&0x1FFF,36)|FLD(mask,20)|FLD(ra,8)|rd); return; }
  case 0x806: case 0x886: { /* VOTE / VOTEU (into a uniform register): the ballot and the vote */
    u32 pd=F(w,81,3); if(op12==0x886){ d=ur(F(w,16,6)); pd=upm(pd); if(pd>7){ untr(L,i,op12); return; } }
    put(L,P("0101000011011---")|d|FLD(F(w,87,3),39)|FLD(F(w,90,1),42)|FLD(F(w,72,2),48)|FLD(pd,45)); return; }
  case 0x81c: case 0x89c: { /* PLOP3 / UPLOP3: each predicate as a 0/~0 mask, LOP3 with the same table, then != 0 */
    u32 s0=F(w,87,3), n0=F(w,90,1), s1=F(w,77,3), n1=F(w,80,1), s2=F(w,68,3), n2=F(w,71,1);
    u32 lut0=(F(w,72,5)<<3)|F(w,64,3), lut1=F(w,16,8), d0=F(w,81,3), d1=F(w,84,3);
    u32 sp[3]={s0,s1,s2}, sn[3]={n0,n1,n2}, rr[3]={S1,S2,SA};
    for(int k=0;k<3;k++){
      int un=op12==0x89c || (k==2 && F(w,67,1));
      /* a spilled uniform predicate is already a 0/~0 mask in its register */
      if(un && sp[k]!=7 && (g_upspill>>sp[k]&1)){
        movr(L,rr[k],175u-sp[k]); if(sn[k]) put(L,P("0101101111100---")|FLD(RZ,20)|FLD(RZ,39)|FLD(rr[k],8)|rr[k]|FLD(0x0F,28));
      } else {
        u32 pp=un? upm(sp[k]) : sp[k]; if(pp>7){ untr(L,i,op12); return; }
        put(L,P("0101000010001---")|rr[k]|FLD(pp,12)|FLD(sn[k],15)|FLD(7,29)|FLD(7,39));
      }
    }
    for(int k=0;k<2;k++){ u32 pdst=k? d1 : d0, lut=k? lut1 : lut0; if(pdst==7) continue;
      put(L,P("0101101111100---")|FLD(S2,20)|FLD(SA,39)|FLD(S1,8)|SB|FLD(lut,28));
      if(op12==0x89c && (g_upspill>>pdst&1)) movr(L,175u-pdst,SB);
      else { if(op12==0x89c) pdst=upm(pdst); if(pdst>7){ untr(L,i,op12); return; }
        put(L,P("0011011-0110----")|FLD(SB,8)|FLD(pdst,3)|FLD(7,0)|FLD(7,39)|FLD(5,49)); }
    }
    return; }
  case 0x803: { /* P2R Rd, PR, Ra, immediate mask (byte select 76-77) */
    u32 mask=F(w,32,32); if(mask>0x7F){ untr(L,i,op12); return; }
    put(L,P("0011100011101---")|FLD(gr(L,F(w,24,8)),8)|d|FLD(mask,20)|FLD(F(w,76,2),41)); return; }
  case 0xb69: { /* TMML with a bound handle: LOD query (Mario Kart World's terrain shaders pick their virtual texture
                   pages with it). Maxwell TMML takes the same coordinates and handle index */
    u32 dim=F(w,61,3), arr=(dim>>2)&1, type=dim&3, hoff=F(w,40,14), mask=F(w,72,4); if(!mask) mask=3;
    u32 ra=gr(L,F(w,24,8)), d1=gr(L,F(w,64,8));
    if(g_tex_bank<0) g_tex_bank=(int)F(w,54,5);
    put(L,P("1101111101011---")|FLD(ra,8)|FLD(RZ,20)|TO|FLD(type<<1|arr,28)|FLD(mask,31)|FLD(hoff&0x1FFF,36));
    u32 to[4]={d,d==RZ? RZ : d+1,d1,d1==RZ? RZ : d1+1}; u32 nr=0; for(int q=0;q<4;q++) if(mask>>q&1) nr++;
    for(u32 q=0;q<nr;q++) if(to[q]!=RZ) movr(L,to[q],TO+q);
    return; }
  case 0x883: { /* UP2UR URd, URZ, mask: the uniform predicates (in the predicates picked for them) as bits */
    u32 mask=F(w,32,32), dd=ur(F(w,16,6)); if(F(w,24,6)!=63 || mask>0x7F || F(w,76,2)){ untr(L,i,op12); return; }
    mov32i(L,dd,0);
    for(u32 k=0;k<7;k++) if(mask>>k&1){ u32 pp=upm(k);
      if(g_upspill>>k&1) movr(L,S1,175u-k);
      else if(pp>7) continue;
      else put(L,P("0101000010001---")|S1|FLD(pp,12)|FLD(7,29)|FLD(7,39));                              /* S1 = UPk ? ~0 : 0 */
      put(L,P("001111----------")|FLD(1u<<k,20)|FLD(dd,39)|FLD(S1,8)|dd|FLD(0xEA,48)); }         /* dd |= S1 & bit */
    return; }
  case 0x34e: /* LEPC (the program's own address) in a geometry program: only the exit handler setup reads it
                 (LEPC + offset -> the handler, installed by 0x357). The handler's work (closing the output) is
                 implicit in GLSL, so the value doesn't matter */
    if(g_sm86_stage!=1 || F(w,24,8) || F(w,32,32) || (u32)(w->hi&0xFFFFFFFFu) || pg!=7){ untr(L,i,op12); return; }
    if(d!=RZ){ mov32i(L,d,0); if(d+1<RZ) mov32i(L,d+1,0); }
    return;
  case 0x357: case 0x957: /* geometry exit handler install / its return: nothing to do (see LEPC) */
    if(g_sm86_stage!=1 || F(w,32,32) || (u32)(w->hi&0xFFFFFFFFu)){ untr(L,i,op12); return; }
    return;
  case 0x324: case 0x924: { /* OUT in a geometry program: Rd = next vertex handle, Ra handle, stream in Rb (0x324) or an
                               immediate (0x924), 78-79: 1 EMIT, 2 CUT, 3 both, 0 finalize (handler: OUT RZ, R0, RZ).
                               Maxwell's OUT_R / OUT_I take the same fields */
    u32 ty=F(w,78,2), hw2=(u32)(w->hi&0xFFFFFFFFu), w1=F(w,32,32);
    if(g_sm86_stage!=1 || (hw2&~0xC000u)){ untr(L,i,op12); return; }
    if(ty==0 && (op12!=0x324 || d!=RZ || w1!=255)){ untr(L,i,op12); return; }
    if(op12==0x324){ if(w1&~0xFFu){ untr(L,i,op12); return; }
      put(L,P("1111101111100---")|d|FLD(gr(L,F(w,24,8)),8)|FLD(gr(L,w1),20)|FLD(ty,39)); }
    else { if(w1>0x7FFFF){ untr(L,i,op12); return; }
      put(L,P("1111011011100---")|d|FLD(gr(L,F(w,24,8)),8)|FLD(w1,20)|FLD(ty,39)); }
    return; }
  case 0x923: case 0x927: { /* ISBERD / ISBEWR: task and mesh programs talk through the stage buffer. Ampere: Ra 24-31,
                               offset 40-63, ISBEWR data 32-39, .32 bit 75 (else U8), region 76-77 (3 attributes,
                               0 the map), .SKEW 78, .O 79. Maxwell has nothing like it, so it goes on as our own
                               instruction (0xEFC0, free there) that shader.c reads for the task/mesh stages:
                               data/dest 0-7, Ra 8-15, offset 20-43, .32 44, attributes 45, skew 46, .O 47, write 48 */
    /* geometry / tessellation programs: ISBERD Rd, [Ra] maps an input index to its attribute handle, which is the
       index itself for us (ALD / AST take it as the vertex) */
    if(g_sm86_stage && op12==0x923){ if(F(w,32,32) || (u32)(w->hi&0xFFFFFFFFu) || F(w,24,8)==RZ){ untr(L,i,op12); return; }
      if(d!=RZ) movr(L,d,gr(L,F(w,24,8))); return; }
    int wr= op12==0x927; u32 reg=F(w,76,2), out=F(w,79,1);
    if((reg!=0 && reg!=3) || (!wr && reg!=3)){ untr(L,i,op12); return; }
    u32 v= wr? gr(L,F(w,32,8)) : d, ra=gr(L,F(w,24,8));
    put(L,0xEFC0000000000000ull|FLD(v,0)|FLD(ra,8)|FLD(F(w,40,24),20)|FLD(F(w,75,1),44)|FLD(reg==3,45)|FLD(F(w,78,1),46)|FLD(out,47)|FLD(wr,48)); return; }
  case 0x3a1: /* MATCH (.U64 bit 73, .ANY bit 72, all-equal predicate 81-83): our own warp op, shader.c runs it across the
                 32 lanes in compute shaders */
    put(L,P("1110111111001---")|d|FLD(gr(L,F(w,24,8)),8)|FLD(F(w,73,1),20)|FLD(F(w,72,1),21)|FLD(F(w,81,3),48)); return;
  case 0x3c4: /* REDUX into a uniform register, op 78-80 (AND OR XOR SUM MIN MAX), .S32 bit 73: our own warp op too */
    put(L,P("1110111111100---")|FLD(ur(F(w,16,6)),0)|FLD(gr(L,F(w,24,8)),8)|FLD(F(w,78,3),20)|FLD(F(w,73,1),23)); return;
  case 0x3c2: /* R2UR */
    movr(L,ur(F(w,16,6)),gr(L,F(w,24,8))); return;
  case 0x389: case 0x589: case 0x989: case 0xf89: { /* SHFL: lane in Rb (0x389/0x589) or bits 53-57, clamp/segment mask in Rc
                                                       (0x389/0x989) or bits 40-52, mode 58-59, in-range predicate 81-83. Maxwell's
                                                       SHFL takes the same fields (immediates flagged at 28/29) */
    u64 o=P("1110111100010---")|FLD(gr(L,F(w,24,8)),8)|d|FLD(F(w,58,2),30)|FLD(F(w,81,3),48);
    if(op12==0x389 || op12==0x589) o|=FLD(gr(L,F(w,32,8)),20); else o|=FLD(F(w,53,5),20)|FLD(1,28);
    if(op12==0x389 || op12==0x989) o|=FLD(gr(L,F(w,64,8)),39); else o|=FLD(F(w,40,13),34)|FLD(1,29);
    put(L,o); return; }
  case 0x9af:   /* LDGDEPBAR: our LDGSTS copies are already done when they're issued */
  case 0x95c:   /* BPT.TRAP after an assert check: the guard that leads here never holds in a working program */
  case 0x942: case 0x918: case 0x91a: case 0x992: case 0x9ab: case 0x98f: case 0x946: case 0x355: case 0x356: case 0x95d:
    put(L,P("0101000010110---")); return;                                                       /* BREAK, NOP, DEPBAR, MEMBAR, ERRBAR, CCTL, YIELD, BMOV, NANOSLEEP */
  case 0xb1d: put(L,P("1111000010101---")); return;                                             /* BAR.SYNC */
  case 0x381: case 0x386: case 0x383: case 0x384: case 0x387: case 0x388:   /* the same without a uniform register in the address */
  case 0x981: case 0x984: case 0x983: case 0x986: case 0x988: case 0x987: { /* LDG LDS LDL STG STS STL */
    int nou= op12<0x900; op12=(op12&0xFF)|0x900;
    int st= op12==0x986||op12==0x988||op12==0x987; u32 addr=gr(L,F(w,24,8)); i32 off=(i32)(F(w,40,24)<<8)>>8; u32 sz=F(w,73,3);
    u32 ua= nou? 63 : st? F(w,64,6) : F(w,32,6); u32 data= st? gr(L,F(w,32,8)) : d;
    /* shared addresses can count vectors instead of bytes (.X4 .X8 .X16, bits 78-79): MKW's cube reduction does
       STS.128 [R.X16] and the channels of neighbouring texels got mixed */
    u32 ash=F(w,78,2); if((op12==0x984 || op12==0x988) && ash){
      put(L,P("0011100-01001---")|FLD(ash+1,20)|FLD(addr,8)|SA); addr=SA; }
    if((ua&63)!=63){ /* + uniform address */ put(L,P("0101110000010---")|FLD(addr,8)|FLD(ur(ua),20)|S2); addr=S2; }
    const char* pt= op12==0x981? "1110111011010---" : op12==0x984? "1110111101001---" : op12==0x983? "1110111101000---" :
                    op12==0x986? "1110111011011---" : op12==0x988? "1110111101011---" : "1110111101010---";
    put(L,P(pt)|FLD(addr,8)|data|FLD((u32)off&0xFFFFFF,20)|FLD(sz,48)|FLD(op12==0x981||op12==0x986? F(w,90,1) : 0,45)); return; }
  case 0xb6c: case 0x36c: case 0xf6c: { /* TXD: coordinates in Ra (x, y[, layer]), the four gradients interleaved in Rb
                     (dx.x, dy.x, dx.y, dy.y) as on Maxwell. 0x36c: the handle first in Ra (Maxwell TXD.B the same),
                     0xf6c: the handle in a uniform register (Star Fox), copied in front of the coordinates */
    u32 dim=F(w,61,3), mask=F(w,72,4), ra=gr(L,F(w,24,8)), rb=gr(L,F(w,32,8)), d1=gr(L,F(w,64,8));
    if((dim!=1 && dim!=5 && dim!=0 && dim!=4) || !mask || F(w,59,2) || F(w,76,5) || F(w,81,3)!=7){ untr(L,i,op12); return; }
    u32 nc=(dim&3)+1+(dim>>2&1), mty=((dim&3)<<1)|(dim>>2&1);
    int bl= op12!=0xb6c;
    if(op12==0xf6c){ movr(L,TA,ur(F(w,40,6))); for(u32 q=0;q<nc && q<3;q++) movr(L,TA+1+q,ra+q); ra=TA; }
    put(L,P(bl? "1101111001------" : "1101111000------")|FLD(ra,8)|FLD(rb,20)|TO|FLD(mty,28)|FLD(mask,31)|FLD(bl? 0 : F(w,40,14),36)|FLD(F(w,90,1),49));
    u32 nr=0; for(int q=0;q<4;q++) if(mask>>q&1) nr++;
    u32 to[4]={d,d==RZ? RZ : d+1,d1,d1==RZ? RZ : d1+1};
    for(u32 q=0;q<nr;q++) if(to[q]!=RZ) movr(L,to[q],TO+q);
    return; }
  case 0xf60: case 0xf66: case 0xf63:   /* TEX / TLD / TLD4 with the handle in a uniform register (bit 91, index at 40) */
  case 0xb60: case 0x361: case 0xb66: case 0x367: case 0xb63: case 0x364: case 0xb6f: case 0x370: { /* TEX TLD TLD4 TXQ */
    int ubl= (op12&0xF00)==0xF00; if(ubl && (!F(w,91,1) || F(w,46,8))){ untr(L,i,op12); return; }
    int bl= (op12&0xF00)==0x300; int kind= (op12&0xFF)==0x60||(op12&0xFF)==0x61? 0 : (op12&0xFF)==0x66||(op12&0xFF)==0x67? 1 : (op12&0xFF)==0x63||(op12&0xFF)==0x64? 2 : 3;
    u32 hoff= bl||ubl? 0 : F(w,40,14), hidx=F(w,54,5);
    u32 ra=gr(L,F(w,24,8)), rb=gr(L,F(w,32,8)), d1=gr(L,F(w,64,8)); u32 mask=F(w,72,4); if(!mask) mask=1;
    if(!bl && !ubl && g_tex_bank<0) g_tex_bank=(int)hidx;   /* the bank of the bound handle (NVN2: 2), Maxwell takes it from tex_cb_index instead */
    if(kind==3){ put(L,P(bl? "1101111101010---" : "1101111101001---")|FLD(ra,8)|d|FLD(F(w,62,2)==0? 1 : 2,22)|FLD(mask,31)|FLD(hoff,36)); return; }
    u32 dim=F(w,61,3); int arr=dim>>2&1, type=dim&3; u32 lodm=kind==2? 0 : F(w,87,3); int aoffi= kind==2? F(w,76,2)==1 : F(w,76,1); int dc= kind==1? 0 : F(w,78,1);
    /* TLD4 bits 76-77 = 2: per-tap offsets (PTP), two registers of packed x/y pairs after the coordinates */
    int ptp= kind==2 && F(w,76,2)==2;
    if(lodm>3 || (kind==2 && F(w,76,2)==3)){ untr(L,i,op12); return; }
    if(ubl){ /* the handle goes first in Maxwell's Rb list, the sources stay where the non bindless form has them */
      if(!F(w,60,1)){ untr(L,i,op12); return; }
      u32 nc=(type==3? 3 : type+1)+arr, nb=(lodm>=2)+aoffi+2*ptp+dc; u32 tot=nc+nb, la[8]; u32 k=0;
      if(tot>4){ untr(L,i,op12); return; }
      u32 n0= tot>=3? 2 : 1;
      for(u32 q=0;q<n0;q++) la[k++]=ra+q; for(u32 q=n0;q<tot;q++) la[k++]=rb+(q-n0);
      for(u32 q=0;q<nc;q++) movr(L,TA+q,la[q]);
      for(u32 q=0;q<nb;q++) movr(L,TB+1+q,la[nc+q]);
      movr(L,TB,ur(F(w,40,6))); ra=TA; rb=TB; bl=1; }
    else if(F(w,60,1)){ /* scalar form: the sources are spread over Ra/Rb, gather them into the Maxwell lists */
      u32 nc=(type==3? 3 : type+1)+arr+bl, nb=(lodm>=2)+aoffi+2*ptp+dc; u32 tot=nc+nb, la[8]; u32 k=0;
      if(tot>4){ untr(L,i,op12); return; }
      u32 n0= tot==4? 2 : tot==3? 2 : 1;
      for(u32 q=0;q<n0;q++) la[k++]=ra+q; for(u32 q=n0;q<tot;q++) la[k++]=rb+(q-n0);
      /* bindless: the handle is the first source on Ampere, Maxwell has it first in Rb */
      u32 ia=0; if(bl){ movr(L,TB,la[0]); ia=1; }
      for(u32 q=0;q<nc-bl;q++) movr(L,TA+q,la[ia+q]);
      for(u32 q=0;q<nb;q++) movr(L,TB+bl+q,la[nc+q]);
      ra=TA; rb=TB; }
    else if(bl){ /* handle first in Ra on Ampere (src0) vs Maxwell's Rb list: rebuild */
      untr(L,i,op12); return; }
    u32 dst=TO;
    /* bindless TLD / TLD4 are Maxwell's TLD.B (0xdd) and TLD4.B (0xdec, offset type at 36, component at 38) */
    u64 base= kind==1? P(bl? "11011101--------" : "11011100--------")|FLD(lodm==3,55)|FLD(aoffi,35) :
              kind==2? (bl? P("1101111011------")|FLD(dc,50)|FLD(ptp? 2 : aoffi,36)|FLD(F(w,87,2),38)
                          : P("110010----------")|FLD(dc,50)|FLD(ptp? 2 : aoffi,54)|FLD(F(w,87,2),56)) :
                       P(bl? "1101111010------" : "11000-----------")|FLD(dc,50)|FLD(aoffi,54)|FLD(lodm,55);
    put(L,base|FLD(ra,8)|FLD(rb,20)|dst|FLD((u32)(type<<1|arr),28)|FLD(mask,31)|(bl && kind==2? 0 : FLD(hoff,36)));
    /* results: packed components go to d, d+1, d1, d1+1 */
    u32 nr=0; for(int q=0;q<4;q++) if(mask>>q&1) nr++;
    u32 to[4]={d,d==RZ? RZ : d+1,d1,d1==RZ? RZ : d1+1};
    if(F(w,79,2)==1){ /* TEX.F16: the components come packed two by two as halves (HADD2 reads them after) */
      for(u32 q=0;q<nr;q+=2){ u32 dd=q? d1 : d; if(dd==RZ) continue;
        put(L,P("0101110010101---")|FLD(TO+q,20)|SA|FLD(1,8)|FLD(2,10));
        if(q+1==nr){ movr(L,dd,SA); continue; }
        put(L,P("0101110010101---")|FLD(TO+q+1,20)|SB|FLD(1,8)|FLD(2,10));
        put(L,P("0011100-01001---")|FLD(16,20)|FLD(SB,8)|SB);
        put(L,P("0101101111100---")|FLD(SB,20)|FLD(RZ,39)|FLD(SA,8)|dd|FLD(0xFC,28)|FLD(7,48)|FLD(3,36)); }
    } else for(u32 q=0;q<nr;q++) if(to[q]!=RZ) movr(L,to[q],TO+q);
    return; }
  default: break; }
  untr(L,i,op12); }

/* lowered Maxwell program (SPH + code with control words), malloc'd. alen = Ampere bytes (header included) */

/* uniform predicates left without a free predicate live in registers (0 / ~0): around the one instruction that reads
   or writes one, a predicate it doesn't touch is saved, used for it, and put back. Mario Kart World's vertex shaders
   pick cbuf values with seven UISETP/ULDC pairs on the same uniform index, seven uniform predicates alive at once */
#define SPILL_SAVE 168u
static u32 spr(u32 k){ return 175u-k; }
static void lower1(Lw* L, const W128* w, u32 i, u32 n){
  if(!g_upspill){ lower1i(L,w,i,n); return; }
  u32 op12=F(w,0,12), op9=op12&0x1FF, pg=F(w,12,3);
  int uni= ((op9&0x180)==0x080 && F(w,91,1)) || op12==0xc82 || op12==0x882;
  int uop= uni || op12==0x886 || op12==0x89c || op12==0x883 || op12==0x9c3 || op12==0xab9 || op12==0xabb;
  u32 o9= uni? (op9&0x17F) : op9;
  int gsp= uop && pg!=7 && (g_upspill>>pg&1);
  int dsp=-1; if(uni && (o9==0x00c || o9==0x012) && (o9==0x012 || F(w,84,3)==7) && F(w,81,3)!=7 && (g_upspill>>F(w,81,3)&1)) dsp=(int)F(w,81,3);
  if(!gsp && dsp<0){ lower1i(L,w,i,n); return; }
  /* guard and output can both be spilled: each borrows a predicate the instruction doesn't name, saved and put back */
  u32 busy=0; if(uop && pg!=7 && !gsp) busy|=1u<<upm(pg); if(!uop && pg!=7) busy|=1u<<pg;
  if(uni && o9==0x00c){ u32 pc=F(w,87,3); if(pc!=7) busy|=1u<<upm(pc); }
  u32 keys[2], slots[2], nk=0; if(gsp) keys[nk++]=pg; if(dsp>=0 && (!nk || keys[0]!=(u32)dsp)) keys[nk++]=(u32)dsp;
  u32 mask=0; int old[2];
  for(u32 q=0;q<nk;q++){ int v=6; while(v>=0 && ((busy|mask)>>v&1)) v--; if(v<0){ untr(L,i,op12); return; } slots[q]=(u32)v; mask|=1u<<v; old[q]=g_upmap[keys[q]]; }
  L->guard=FLD(7,16);
  put(L,P("0011100011101---")|FLD(RZ,8)|SPILL_SAVE|FLD(mask,20));
  for(u32 q=0;q<nk;q++){
    put(L,P("010110110110----")|FLD(RZ,20)|FLD(spr(keys[q]),8)|FLD(slots[q],3)|FLD(7,0)|FLD(7,39)|FLD(5,49));
    g_upmap[keys[q]]=(int)slots[q];
  }
  lower1i(L,w,i,n); L->guard=FLD(7,16);
  if(dsp>=0) put(L,P("0101000010001---")|spr((u32)dsp)|FLD(upm((u32)dsp),12)|FLD(7,29)|FLD(7,39));
  for(u32 q=0;q<nk;q++) g_upmap[keys[q]]=old[q];
  put(L,P("0011100011110---")|FLD(SPILL_SAVE,8)|FLD(mask,20));
  if(L->maxreg<(int)SPILL_SAVE) L->maxreg=(int)SPILL_SAVE; }

u8* sm86_lower(const u8* code, u32 max_bytes, int has_sph, u32* out_len, u32* alen, Sm86Info* inf){
  u32 hdr= has_sph? 0x50 : 0; if(max_bytes<hdr+16) return NULL;
  u32 n=(max_bytes-hdr)/16; g_tex_bank=-1;
  /* length: past the last EXIT / self branch that's beyond every forward target */
  u32 end=0, maxt=0;
  for(u32 i=0;i<n;i++){ W128 w; memcpy(&w,code+hdr+i*16,16); u32 op=F(&w,0,12);
    if(op==0x944) op=0x947;
    if(op==0x947||op==0x945){ int64_t off= op==0x947? (int64_t)((((u64)F(&w,34,30))|((u64)F(&w,64,18)<<30))<<16)>>16 : (int64_t)((u64)F(&w,34,30)<<34)>>34;
      int64_t t=(int64_t)i+1+off/4; if(t>(int64_t)maxt && t<(int64_t)n) maxt=(u32)t;
      if(op==0x947 && t==(int64_t)i && i>=maxt && F(&w,12,4)==7){ end=i+1; break; } }
    if(op==0x94d && F(&w,12,4)==7 && i>=maxt){ end=i+1; /* NVIDIA ends with EXIT then BRA to self */ continue; }
    if(end && i>end+2) break;
    if(i>=16383) break; }
  if(!end) end= n<16384? n : 16384;
  { /* which predicates the shader uses, and which uniform ones: those get the free ones */
    u32 usedp=0, usedu=0, uall=0; int ufirst[8], ulast[8]; for(int k=0;k<8;k++){ ufirst[k]=-1; ulast[k]=-1; }
    for(u32 i=0;i<end;i++){ W128 w; memcpy(&w,code+hdr+i*16,16); u32 op=F(&w,0,12), o9=op&0x1FF; int un=(o9&0x180)==0x080 && F(&w,91,1);
      uall|=usedu; usedu=0;
      u32 a9= un? (o9&0x17F) : o9;
      int uop= un || op==0x886 || op==0x89c || op==0x883 || op==0x9c3 || op==0xab9 || op==0xabb || op==0xc82 || op==0x882;
      u32* set= uop? &usedu : &usedp;
      *set|=1u<<F(&w,12,3);
      int alu= op<0x200 || (op&0xE00)!=0x000;
      if(alu && (a9==0x00c||a9==0x00b||a9==0x00a||a9==0x02a||a9==0x034||a9==0x010||a9==0x011||a9==0x012||a9==0x024||a9==0x025||a9==0x007||a9==0x008||a9==0x009||a9==0x017)){
        *set|=1u<<F(&w,81,3); *set|=1u<<F(&w,84,3); *set|=1u<<F(&w,87,3); if(a9==0x010) *set|=1u<<F(&w,77,3); }
      if(op==0x81c){ usedp|=1u<<F(&w,81,3)|1u<<F(&w,84,3)|1u<<F(&w,87,3)|1u<<F(&w,77,3); if(F(&w,67,1)) usedu|=1u<<F(&w,68,3); else usedp|=1u<<F(&w,68,3); }
      if(op==0x89c) usedu|=1u<<F(&w,81,3)|1u<<F(&w,84,3)|1u<<F(&w,87,3)|1u<<F(&w,77,3)|1u<<F(&w,68,3);
      if(op==0x886){ usedu|=1u<<F(&w,81,3); usedp|=1u<<F(&w,87,3); }
      if(op==0x806 || op==0x3a1 || (op&0x1FF)==0x189 || op==0xb97) usedp|=1u<<F(&w,81,3)|1u<<F(&w,87,3);
      if(op==0x947 || op==0x944) usedp|=1u<<F(&w,87,3);
      if(op==0x547) usedu|=1u<<F(&w,24,3);
      if(op==0x804 || op==0x803) usedp|=F(&w,32,7);
      /* UP2UR saves all 7 uniform predicates (mask 0x7F) even when the shader uses two: those don't take a predicate */
      for(int k=0;k<7;k++) if(usedu>>k&1){ if(ufirst[k]<0) ufirst[k]=(int)i; ulast[k]=(int)i; } }
    usedu|=uall;
    usedp&=0x7F; usedu&=0x7F;
    { static int on=-1; if(on<0) on=getenv("SWR_SM86_TRACE")!=NULL; if(on) fprintf(stderr,"sm86 preds used %02x uniform %02x\n",usedp,usedu); }
    for(int k=0;k<8;k++) g_upmap[k]=8;
    /* uniform predicates share a free predicate when their ranges (first to last mention) don't overlap. a backward
       branch makes the ranges unreliable, then each needs its own */
    int back=0; for(u32 i=0;i<end;i++){ W128 w; memcpy(&w,code+hdr+i*16,16); u32 op=F(&w,0,12);
      if(op==0x947 || op==0x547){ int64_t off=(int64_t)((((u64)F(&w,34,30))|((u64)F(&w,64,18)<<30))<<16)>>16; if(off<0) back=1; } }
    int qlast[7]; for(int q=0;q<7;q++) qlast[q]=-2;
    for(int it=0;it<7;it++){ int k=-1; for(int j=0;j<7;j++) if((usedu>>j&1) && g_upmap[j]==8 && (k<0 || ufirst[j]<ufirst[k])) k=j; if(k<0) break;
      int pick=-1; for(int q=0;q<7;q++) if(!((usedp>>q)&1) && qlast[q]==-2){ pick=q; break; }
      if(pick<0 && !back) for(int q=0;q<7;q++) if(!((usedp>>q)&1) && qlast[q]>=0 && qlast[q]<ufirst[k]){ pick=q; break; }
      if(pick<0){ g_upmap[k]=9; continue; }
      g_upmap[k]=pick; qlast[pick]=ulast[k]; }
    { static int on=-1; if(on<0) on=getenv("SWR_SM86_TRACE")!=NULL; if(on){ fprintf(stderr,"sm86 back %d",back); for(int k=0;k<7;k++) fprintf(stderr," UP%d[%d..%d]->%d",k,ufirst[k],ulast[k],g_upmap[k]); fprintf(stderr,"\n"); } }
    g_upspill=0; for(int k=0;k<7;k++) if(g_upmap[k]==9) g_upspill|=1<<k;
    for(int k=0;k<8;k++) if(g_upmap[k]==9) g_upmap[k]=8; }
  { /* RET targets: each function's start is a CALL target; one call site -> return right after it */
    free(g_ret); g_ret=malloc(sizeof(int)*(end+1)); g_nret=end; for(u32 i=0;i<end;i++) g_ret[i]=-1;
    u32 tg[64], site[64], cnt[64]; int nt=0;
    for(u32 i=0;i<end;i++){ W128 w; memcpy(&w,code+hdr+i*16,16); if(F(&w,0,12)!=0x944) continue;
      int64_t off=(int64_t)((((u64)F(&w,34,30))|((u64)F(&w,64,18)<<30))<<16)>>16; int64_t t=(int64_t)i+1+off/4; if(t<0 || t>=(int64_t)end) continue;
      int k=0; for(;k<nt;k++) if(tg[k]==(u32)t) break; if(k==nt){ if(nt==64) continue; tg[nt]=(u32)t; cnt[nt]=0; nt++; } cnt[k]++; site[k]=i; }
    for(u32 i=0;i<end;i++){ W128 w; memcpy(&w,code+hdr+i*16,16); if(F(&w,0,12)!=0x950) continue;
      int best=-1; for(int k=0;k<nt;k++) if(tg[k]<=i && (best<0 || tg[k]>tg[best])) best=k;
      if(best>=0 && cnt[best]==1) g_ret[i]=(int)site[best]+1;
      else if(best>=0 && cnt[best]>1) g_ret[i]=-2; }
    g_sm86_nrets=0;
    for(u32 i=0;i<end && g_sm86_nrets<64;i++){ W128 w; memcpy(&w,code+hdr+i*16,16); if(F(&w,0,12)==0x944) g_sm86_rets[g_sm86_nrets++]=i+1; } }
  g_nbrxb=0;
  Lw L={0}; L.maxreg=-1; L.amap=malloc(sizeof(u32)*(end+1)); L.namp=end;
  for(u32 i=0;i<end;i++){ W128 w; memcpy(&w,code+hdr+i*16,16); L.amap[i]=L.n; g_cur=&w; lower1(&L,&w,i,end); g_cur=NULL; L.guard=0; }
  L.amap[end]=L.n; L.guard=FLD(7,16); put(&L,P("111000110000----")|0xF);   /* EXIT at the end in case the last block falls through */
  /* layout: SPH, then groups of control word + 3 instructions */
  u32 nm=L.n, words=hdr/8 + ((nm+2)/3)*4 + 4;
  u64* o=calloc(words+8,8); if(hdr) memcpy(o,code,hdr);
  u32 base=hdr/8;
#define SLOT(k) (base+((k)/3)*4+1+((k)%3))
  for(u32 k=0;k<nm;k++) o[SLOT(k)]=L.m[k];
  for(u32 g=0; g<(nm+2)/3; g++) o[base+g*4]=0x001F8000FC0007E0ull;   /* control word: no stalls/barriers */
  for(u32 f=0;f<L.nfx;f++){ u32 src=SLOT(L.fx[f].at), dst=SLOT(L.amap[L.fx[f].tgt]); int32_t off=(int32_t)(dst-src-1)*8;
    o[src]=(o[src]&~(0xFFFFFFull<<20))|((u64)((u32)off&0xFFFFFF)<<20); }
  *out_len=(SLOT(nm>0? nm-1 : 0)+1)*8; if(alen) *alen=hdr+end*16;
  if(inf){ inf->insts=end; inf->untranslated=L.bad; inf->first_bad=L.first_bad; inf->bad_op=L.bad_op; inf->maxreg=L.maxreg; inf->reg_clash= L.maxreg>=(int)UBASE; inf->tex_bank=g_tex_bank; }
  free(g_sm86_nmap); g_sm86_nmap=malloc(sizeof(u32)*(end+1)); g_sm86_nnmap=end;
  for(u32 k=0;k<=end;k++) g_sm86_nmap[k]=SLOT(L.amap[k])+(hdr? 0 : 10);   /* compute code goes to shader.c 80 bytes in */
  g_sm86_nbrxb=g_nbrxb; for(u32 k=0;k<g_nbrxb;k++){ g_sm86_brxb_idx[k]=SLOT(g_brxb_at[k])+(hdr? 0 : 10); g_sm86_brxb_base[k]=g_brxb_val[k]; }
  free(L.m); free(L.amap); free(L.fx);
  return (u8*)o; }
