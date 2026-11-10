// SPDX-License-Identifier: GPL-3.0-or-later
// SwitchRecomp: what sm86.c (the runtime's Switch 2 Ampere -> Maxwell lowering) emits that Eden's Maxwell frontend
// didn't translate: IMAD in all its forms (with .HI, which division by a constant compiles to), CS2R, and sm86.c's
// own encodings of Ampere's MATCH, REDUX and the mesh stage buffer access (ISBE)

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
// high 32 bits of a 32x32 product, from 16 bit halves (the IR has no wide multiply)
IR::U32 MulHi(IR::IREmitter& ir, const IR::U32& a, const IR::U32& b, bool a_signed, bool b_signed) {
    const IR::U32 m16{ir.Imm32(0xFFFF)};
    const IR::U32 s16{ir.Imm32(16)};
    const IR::U32 al{ir.BitwiseAnd(a, m16)}, ah{ir.ShiftRightLogical(a, s16)};
    const IR::U32 bl{ir.BitwiseAnd(b, m16)}, bh{ir.ShiftRightLogical(b, s16)};
    const IR::U32 ll{ir.IMul(al, bl)}, m1{ir.IMul(ah, bl)}, m2{ir.IMul(al, bh)}, hh{ir.IMul(ah, bh)};
    const IR::U32 carry{ir.ShiftRightLogical(
        ir.IAdd(ir.IAdd(ir.ShiftRightLogical(ll, s16), ir.BitwiseAnd(m1, m16)), ir.BitwiseAnd(m2, m16)), s16)};
    IR::U32 hi{ir.IAdd(ir.IAdd(hh, ir.ShiftRightLogical(m1, s16)), ir.IAdd(ir.ShiftRightLogical(m2, s16), carry))};
    const IR::U32 zero{ir.Imm32(0)};
    if (a_signed) {
        hi = ir.ISub(hi, IR::U32{ir.Select(ir.ILessThan(a, zero, true), b, zero)});
    }
    if (b_signed) {
        hi = ir.ISub(hi, IR::U32{ir.Select(ir.ILessThan(b, zero, true), a, zero)});
    }
    return hi;
}

void Imad(TranslatorVisitor& v, u64 insn, const IR::U32& op_b, const IR::U32& op_c, bool wide_form) {
    union {
        u64 raw;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<8, 8, IR::Reg> src_reg;
        BitField<48, 1, u64> a_signed;
        BitField<53, 1, u64> b_signed;
        BitField<54, 1, u64> hi;
    } const imad{insn};
    const IR::U32 a{v.X(imad.src_reg)};
    IR::U32 result;
    if (wide_form && imad.hi != 0) {
        result = v.ir.IAdd(MulHi(v.ir, a, op_b, imad.a_signed != 0, imad.b_signed != 0), op_c);
    } else {
        result = v.ir.IAdd(v.ir.IMul(a, op_b), op_c);
    }
    v.X(imad.dest_reg, result);
}
} // Anonymous namespace

void TranslatorVisitor::IMAD_reg(u64 insn) { Imad(*this, insn, GetReg20(insn), GetReg39(insn), true); }
void TranslatorVisitor::IMAD_rc(u64 insn) { Imad(*this, insn, GetReg39(insn), GetCbuf(insn), true); }
void TranslatorVisitor::IMAD_cr(u64 insn) { Imad(*this, insn, GetCbuf(insn), GetReg39(insn), true); }
void TranslatorVisitor::IMAD_imm(u64 insn) { Imad(*this, insn, GetImm20(insn), GetReg39(insn), true); }
void TranslatorVisitor::IMAD32I(u64 insn) {
    const IR::Reg dest{static_cast<IR::Reg>(insn & 0xFF)};
    Imad(*this, insn, GetImm32(insn), X(dest), false);
}

void TranslatorVisitor::CS2R(u64 insn) {   // clock / SR pair: read as 0, like the runtime's OpenGL path
    const IR::Reg dest{static_cast<IR::Reg>(insn & 0xFF)};
    if (dest != IR::Reg::RZ) {
        X(dest, ir.Imm32(0));
        X(dest + 1, ir.Imm32(0));
    }
}

// MATCH: Rd = lanes whose value equals this lane's (Ra, or Ra:Ra+1 with bit 20); bit 21 ANY, else Rd is the active
// mask only when every active lane matches, and Pd (48) says whether they did
void TranslatorVisitor::SWR_MATCH(u64 insn) {
    const IR::Reg dest{static_cast<IR::Reg>(insn & 0xFF)};
    const IR::Reg src{static_cast<IR::Reg>((insn >> 8) & 0xFF)};
    const bool wide = ((insn >> 20) & 1) != 0, any = ((insn >> 21) & 1) != 0;
    const IR::U32 clamp{ir.Imm32(0x1f)}, seg{ir.Imm32(0)};
    const IR::U32 v{X(src)};
    const IR::U32 h{wide ? X(src + 1) : ir.Imm32(0)};
    IR::U32 mask{ir.Imm32(0)};
    for (u32 i = 0; i < 32; ++i) {
        const IR::U32 lane{ir.Imm32(i)};
        IR::U1 eq{ir.IEqual(ir.ShuffleIndex(v, lane, clamp, seg), v)};
        if (wide) {
            eq = ir.LogicalAnd(eq, ir.IEqual(ir.ShuffleIndex(h, lane, clamp, seg), h));
        }
        mask = IR::U32{ir.Select(eq, ir.BitwiseOr(mask, ir.Imm32(1u << i)), mask)};
    }
    if (any) {
        X(dest, mask);
        return;
    }
    const IR::U32 active{ir.SubgroupBallot(ir.Imm1(true))};
    const IR::U1 all{ir.IEqual(mask, active)};
    X(dest, IR::U32{ir.Select(all, active, ir.Imm32(0))});
    ir.SetPred(static_cast<IR::Pred>((insn >> 48) & 7), all);
}

// REDUX: Rd = AND / OR / XOR / SUM / MIN / MAX (op 20-22, signed bit 23) of Ra over the warp
void TranslatorVisitor::SWR_REDUX(u64 insn) {
    const IR::Reg dest{static_cast<IR::Reg>(insn & 0xFF)};
    const u32 op = static_cast<u32>((insn >> 20) & 7);
    const bool sg = ((insn >> 23) & 1) != 0;
    const IR::U32 clamp{ir.Imm32(0x1f)}, seg{ir.Imm32(0)};
    IR::U32 r{X(static_cast<IR::Reg>((insn >> 8) & 0xFF))};
    for (u32 k = 1; k < 32; k <<= 1) {
        const IR::U32 x{ir.ShuffleButterfly(r, ir.Imm32(k), clamp, seg)};
        switch (op) {
        case 0: r = ir.BitwiseAnd(r, x); break;
        case 1: r = ir.BitwiseOr(r, x); break;
        case 2: r = ir.BitwiseXor(r, x); break;
        case 4: r = ir.IMin(r, x, sg); break;
        case 5: r = ir.IMax(r, x, sg); break;
        default: r = IR::U32{ir.IAdd(r, x)}; break;
        }
    }
    X(dest, r);
}

void TranslatorVisitor::SWR_ISBE(u64) {
    throw NotImplementedException("Mesh stage buffer (ISBE)");
}

} // namespace Shader::Maxwell
