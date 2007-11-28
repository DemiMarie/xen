/******************************************************************************
 * x86_emulate.c
 * 
 * Generic x86 (32-bit and 64-bit) instruction decoder and emulator.
 * 
 * Copyright (c) 2005-2007 Keir Fraser
 * Copyright (c) 2005-2007 XenSource Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __XEN__
#include <stddef.h>
#include <stdint.h>
#include <public/xen.h>
#else
#include <xen/config.h>
#include <xen/types.h>
#include <xen/lib.h>
#include <asm/regs.h>
#undef cmpxchg
#endif
#include <asm-x86/x86_emulate.h>

/* Operand sizes: 8-bit operands or specified/overridden size. */
#define ByteOp      (1<<0) /* 8-bit operands. */
/* Destination operand type. */
#define DstBitBase  (0<<1) /* Memory operand, bit string. */
#define ImplicitOps (1<<1) /* Implicit in opcode. No generic decode. */
#define DstReg      (2<<1) /* Register operand. */
#define DstMem      (3<<1) /* Memory operand. */
#define DstMask     (3<<1)
/* Source operand type. */
#define SrcNone     (0<<3) /* No source operand. */
#define SrcImplicit (0<<3) /* Source operand is implicit in the opcode. */
#define SrcReg      (1<<3) /* Register operand. */
#define SrcMem      (2<<3) /* Memory operand. */
#define SrcMem16    (3<<3) /* Memory operand (16-bit). */
#define SrcImm      (4<<3) /* Immediate operand. */
#define SrcImmByte  (5<<3) /* 8-bit sign-extended immediate operand. */
#define SrcMask     (7<<3)
/* Generic ModRM decode. */
#define ModRM       (1<<6)
/* Destination is only written; never read. */
#define Mov         (1<<7)

static uint8_t opcode_table[256] = {
    /* 0x00 - 0x07 */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    ByteOp|DstReg|SrcMem|ModRM, DstReg|SrcMem|ModRM,
    ByteOp|DstReg|SrcImm, DstReg|SrcImm, ImplicitOps, ImplicitOps,
    /* 0x08 - 0x0F */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    ByteOp|DstReg|SrcMem|ModRM, DstReg|SrcMem|ModRM,
    ByteOp|DstReg|SrcImm, DstReg|SrcImm, ImplicitOps, 0,
    /* 0x10 - 0x17 */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    ByteOp|DstReg|SrcMem|ModRM, DstReg|SrcMem|ModRM,
    ByteOp|DstReg|SrcImm, DstReg|SrcImm, ImplicitOps, ImplicitOps,
    /* 0x18 - 0x1F */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    ByteOp|DstReg|SrcMem|ModRM, DstReg|SrcMem|ModRM,
    ByteOp|DstReg|SrcImm, DstReg|SrcImm, ImplicitOps, ImplicitOps,
    /* 0x20 - 0x27 */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    ByteOp|DstReg|SrcMem|ModRM, DstReg|SrcMem|ModRM,
    ByteOp|DstReg|SrcImm, DstReg|SrcImm, 0, ImplicitOps,
    /* 0x28 - 0x2F */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    ByteOp|DstReg|SrcMem|ModRM, DstReg|SrcMem|ModRM,
    ByteOp|DstReg|SrcImm, DstReg|SrcImm, 0, ImplicitOps,
    /* 0x30 - 0x37 */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    ByteOp|DstReg|SrcMem|ModRM, DstReg|SrcMem|ModRM,
    ByteOp|DstReg|SrcImm, DstReg|SrcImm, 0, ImplicitOps,
    /* 0x38 - 0x3F */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    ByteOp|DstReg|SrcMem|ModRM, DstReg|SrcMem|ModRM,
    ByteOp|DstReg|SrcImm, DstReg|SrcImm, 0, ImplicitOps,
    /* 0x40 - 0x4F */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0x50 - 0x5F */
    ImplicitOps|Mov, ImplicitOps|Mov, ImplicitOps|Mov, ImplicitOps|Mov,
    ImplicitOps|Mov, ImplicitOps|Mov, ImplicitOps|Mov, ImplicitOps|Mov,
    ImplicitOps|Mov, ImplicitOps|Mov, ImplicitOps|Mov, ImplicitOps|Mov,
    ImplicitOps|Mov, ImplicitOps|Mov, ImplicitOps|Mov, ImplicitOps|Mov,
    /* 0x60 - 0x67 */
    ImplicitOps, ImplicitOps, DstReg|SrcMem|ModRM, DstReg|SrcMem16|ModRM|Mov,
    0, 0, 0, 0,
    /* 0x68 - 0x6F */
    ImplicitOps|Mov, DstMem|SrcImm|ModRM|Mov,
    ImplicitOps|Mov, DstMem|SrcImmByte|ModRM|Mov,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0x70 - 0x77 */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0x78 - 0x7F */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0x80 - 0x87 */
    ByteOp|DstMem|SrcImm|ModRM, DstMem|SrcImm|ModRM,
    ByteOp|DstMem|SrcImm|ModRM, DstMem|SrcImmByte|ModRM,
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    /* 0x88 - 0x8F */
    ByteOp|DstMem|SrcReg|ModRM|Mov, DstMem|SrcReg|ModRM|Mov,
    ByteOp|DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    DstMem|SrcReg|ModRM|Mov, DstReg|SrcNone|ModRM,
    DstReg|SrcMem|ModRM|Mov, DstMem|SrcNone|ModRM|Mov,
    /* 0x90 - 0x97 */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0x98 - 0x9F */
    ImplicitOps, ImplicitOps, ImplicitOps, 0,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0xA0 - 0xA7 */
    ByteOp|ImplicitOps|Mov, ImplicitOps|Mov,
    ByteOp|ImplicitOps|Mov, ImplicitOps|Mov,
    ByteOp|ImplicitOps|Mov, ImplicitOps|Mov,
    ByteOp|ImplicitOps, ImplicitOps,
    /* 0xA8 - 0xAF */
    ByteOp|DstReg|SrcImm, DstReg|SrcImm,
    ByteOp|ImplicitOps|Mov, ImplicitOps|Mov,
    ByteOp|ImplicitOps|Mov, ImplicitOps|Mov,
    ByteOp|ImplicitOps, ImplicitOps,
    /* 0xB0 - 0xB7 */
    ByteOp|DstReg|SrcImm|Mov, ByteOp|DstReg|SrcImm|Mov,
    ByteOp|DstReg|SrcImm|Mov, ByteOp|DstReg|SrcImm|Mov,
    ByteOp|DstReg|SrcImm|Mov, ByteOp|DstReg|SrcImm|Mov,
    ByteOp|DstReg|SrcImm|Mov, ByteOp|DstReg|SrcImm|Mov,
    /* 0xB8 - 0xBF */
    DstReg|SrcImm|Mov, DstReg|SrcImm|Mov, DstReg|SrcImm|Mov, DstReg|SrcImm|Mov,
    DstReg|SrcImm|Mov, DstReg|SrcImm|Mov, DstReg|SrcImm|Mov, DstReg|SrcImm|Mov,
    /* 0xC0 - 0xC7 */
    ByteOp|DstMem|SrcImm|ModRM, DstMem|SrcImmByte|ModRM,
    ImplicitOps, ImplicitOps,
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    ByteOp|DstMem|SrcImm|ModRM|Mov, DstMem|SrcImm|ModRM|Mov,
    /* 0xC8 - 0xCF */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0xD0 - 0xD7 */
    ByteOp|DstMem|SrcImplicit|ModRM, DstMem|SrcImplicit|ModRM, 
    ByteOp|DstMem|SrcImplicit|ModRM, DstMem|SrcImplicit|ModRM, 
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0xD8 - 0xDF */
    0, 0, 0, 0, 0, 0, 0, 0,
    /* 0xE0 - 0xE7 */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0xE8 - 0xEF */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0xF0 - 0xF7 */
    0, ImplicitOps, 0, 0,
    ImplicitOps, ImplicitOps,
    ByteOp|DstMem|SrcNone|ModRM, DstMem|SrcNone|ModRM,
    /* 0xF8 - 0xFF */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ByteOp|DstMem|SrcNone|ModRM, DstMem|SrcNone|ModRM
};

static uint8_t twobyte_table[256] = {
    /* 0x00 - 0x07 */
    0, ImplicitOps|ModRM, 0, 0, 0, ImplicitOps, 0, 0,
    /* 0x08 - 0x0F */
    ImplicitOps, ImplicitOps, 0, 0, 0, ImplicitOps|ModRM, 0, 0,
    /* 0x10 - 0x17 */
    0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x18 - 0x1F */
    ImplicitOps|ModRM, ImplicitOps|ModRM, ImplicitOps|ModRM, ImplicitOps|ModRM,
    ImplicitOps|ModRM, ImplicitOps|ModRM, ImplicitOps|ModRM, ImplicitOps|ModRM,
    /* 0x20 - 0x27 */
    ImplicitOps|ModRM, ImplicitOps|ModRM, ImplicitOps|ModRM, ImplicitOps|ModRM,
    0, 0, 0, 0,
    /* 0x28 - 0x2F */
    0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x30 - 0x37 */
    ImplicitOps, 0, ImplicitOps, 0, 0, 0, 0, 0,
    /* 0x38 - 0x3F */
    0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x40 - 0x47 */
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    /* 0x48 - 0x4F */
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    /* 0x50 - 0x5F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x60 - 0x6F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x70 - 0x7F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x80 - 0x87 */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0x88 - 0x8F */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0x90 - 0x97 */
    ByteOp|DstMem|SrcNone|ModRM|Mov, ByteOp|DstMem|SrcNone|ModRM|Mov,
    ByteOp|DstMem|SrcNone|ModRM|Mov, ByteOp|DstMem|SrcNone|ModRM|Mov,
    ByteOp|DstMem|SrcNone|ModRM|Mov, ByteOp|DstMem|SrcNone|ModRM|Mov,
    ByteOp|DstMem|SrcNone|ModRM|Mov, ByteOp|DstMem|SrcNone|ModRM|Mov,
    /* 0x98 - 0x9F */
    ByteOp|DstMem|SrcNone|ModRM|Mov, ByteOp|DstMem|SrcNone|ModRM|Mov,
    ByteOp|DstMem|SrcNone|ModRM|Mov, ByteOp|DstMem|SrcNone|ModRM|Mov,
    ByteOp|DstMem|SrcNone|ModRM|Mov, ByteOp|DstMem|SrcNone|ModRM|Mov,
    ByteOp|DstMem|SrcNone|ModRM|Mov, ByteOp|DstMem|SrcNone|ModRM|Mov,
    /* 0xA0 - 0xA7 */
    ImplicitOps, ImplicitOps, ImplicitOps, DstBitBase|SrcReg|ModRM,
    0, 0, 0, 0, 
    /* 0xA8 - 0xAF */
    ImplicitOps, ImplicitOps, 0, DstBitBase|SrcReg|ModRM,
    0, 0, 0, DstReg|SrcMem|ModRM,
    /* 0xB0 - 0xB7 */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM,
    DstReg|SrcMem|ModRM|Mov, DstBitBase|SrcReg|ModRM,
    DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem|ModRM|Mov,
    ByteOp|DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem16|ModRM|Mov,
    /* 0xB8 - 0xBF */
    0, 0, DstBitBase|SrcImmByte|ModRM, DstBitBase|SrcReg|ModRM,
    DstReg|SrcMem|ModRM, DstReg|SrcMem|ModRM,
    ByteOp|DstReg|SrcMem|ModRM|Mov, DstReg|SrcMem16|ModRM|Mov,
    /* 0xC0 - 0xC7 */
    ByteOp|DstMem|SrcReg|ModRM, DstMem|SrcReg|ModRM, 0, 0,
    0, 0, 0, ImplicitOps|ModRM,
    /* 0xC8 - 0xCF */
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    ImplicitOps, ImplicitOps, ImplicitOps, ImplicitOps,
    /* 0xD0 - 0xDF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0xE0 - 0xEF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0xF0 - 0xFF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Type, address-of, and value of an instruction's operand. */
struct operand {
    enum { OP_REG, OP_MEM, OP_IMM, OP_NONE } type;
    unsigned int  bytes;
    unsigned long val, orig_val;
    union {
        /* OP_REG: Pointer to register field. */
        unsigned long *reg;
        /* OP_MEM: Segment and offset. */
        struct {
            enum x86_segment seg;
            unsigned long    off;
        } mem;
    };
};

/* EFLAGS bit definitions. */
#define EFLG_VIP  (1<<20)
#define EFLG_VIF  (1<<19)
#define EFLG_AC   (1<<18)
#define EFLG_VM   (1<<17)
#define EFLG_RF   (1<<16)
#define EFLG_NT   (1<<14)
#define EFLG_IOPL (3<<12)
#define EFLG_OF   (1<<11)
#define EFLG_DF   (1<<10)
#define EFLG_IF   (1<<9)
#define EFLG_TF   (1<<8)
#define EFLG_SF   (1<<7)
#define EFLG_ZF   (1<<6)
#define EFLG_AF   (1<<4)
#define EFLG_PF   (1<<2)
#define EFLG_CF   (1<<0)

/* Exception definitions. */
#define EXC_DE  0
#define EXC_DB  1
#define EXC_BP  3
#define EXC_OF  4
#define EXC_BR  5
#define EXC_UD  6
#define EXC_GP 13

/*
 * Instruction emulation:
 * Most instructions are emulated directly via a fragment of inline assembly
 * code. This allows us to save/restore EFLAGS and thus very easily pick up
 * any modified flags.
 */

#if defined(__x86_64__)
#define _LO32 "k"          /* force 32-bit operand */
#define _STK  "%%rsp"      /* stack pointer */
#define _BYTES_PER_LONG "8"
#elif defined(__i386__)
#define _LO32 ""           /* force 32-bit operand */
#define _STK  "%%esp"      /* stack pointer */
#define _BYTES_PER_LONG "4"
#endif

/*
 * These EFLAGS bits are restored from saved value during emulation, and
 * any changes are written back to the saved value after emulation.
 */
#define EFLAGS_MASK (EFLG_OF|EFLG_SF|EFLG_ZF|EFLG_AF|EFLG_PF|EFLG_CF)

/* Before executing instruction: restore necessary bits in EFLAGS. */
#define _PRE_EFLAGS(_sav, _msk, _tmp)                           \
/* EFLAGS = (_sav & _msk) | (EFLAGS & ~_msk); _sav &= ~_msk; */ \
"movl %"_sav",%"_LO32 _tmp"; "                                  \
"push %"_tmp"; "                                                \
"push %"_tmp"; "                                                \
"movl %"_msk",%"_LO32 _tmp"; "                                  \
"andl %"_LO32 _tmp",("_STK"); "                                 \
"pushf; "                                                       \
"notl %"_LO32 _tmp"; "                                          \
"andl %"_LO32 _tmp",("_STK"); "                                 \
"andl %"_LO32 _tmp",2*"_BYTES_PER_LONG"("_STK"); "              \
"pop  %"_tmp"; "                                                \
"orl  %"_LO32 _tmp",("_STK"); "                                 \
"popf; "                                                        \
"pop  %"_sav"; "

/* After executing instruction: write-back necessary bits in EFLAGS. */
#define _POST_EFLAGS(_sav, _msk, _tmp)          \
/* _sav |= EFLAGS & _msk; */                    \
"pushf; "                                       \
"pop  %"_tmp"; "                                \
"andl %"_msk",%"_LO32 _tmp"; "                  \
"orl  %"_LO32 _tmp",%"_sav"; "

/* Raw emulation: instruction has two explicit operands. */
#define __emulate_2op_nobyte(_op,_src,_dst,_eflags,_wx,_wy,_lx,_ly,_qx,_qy)\
do{ unsigned long _tmp;                                                    \
    switch ( (_dst).bytes )                                                \
    {                                                                      \
    case 2:                                                                \
        asm volatile (                                                     \
            _PRE_EFLAGS("0","4","2")                                       \
            _op"w %"_wx"3,%1; "                                            \
            _POST_EFLAGS("0","4","2")                                      \
            : "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp)              \
            : _wy ((_src).val), "i" (EFLAGS_MASK),                         \
              "m" (_eflags), "m" ((_dst).val) );                           \
        break;                                                             \
    case 4:                                                                \
        asm volatile (                                                     \
            _PRE_EFLAGS("0","4","2")                                       \
            _op"l %"_lx"3,%1; "                                            \
            _POST_EFLAGS("0","4","2")                                      \
            : "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp)              \
            : _ly ((_src).val), "i" (EFLAGS_MASK),                         \
              "m" (_eflags), "m" ((_dst).val) );                           \
        break;                                                             \
    case 8:                                                                \
        __emulate_2op_8byte(_op, _src, _dst, _eflags, _qx, _qy);           \
        break;                                                             \
    }                                                                      \
} while (0)
#define __emulate_2op(_op,_src,_dst,_eflags,_bx,_by,_wx,_wy,_lx,_ly,_qx,_qy)\
do{ unsigned long _tmp;                                                    \
    switch ( (_dst).bytes )                                                \
    {                                                                      \
    case 1:                                                                \
        asm volatile (                                                     \
            _PRE_EFLAGS("0","4","2")                                       \
            _op"b %"_bx"3,%1; "                                            \
            _POST_EFLAGS("0","4","2")                                      \
            : "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp)              \
            : _by ((_src).val), "i" (EFLAGS_MASK),                         \
              "m" (_eflags), "m" ((_dst).val) );                           \
        break;                                                             \
    default:                                                               \
        __emulate_2op_nobyte(_op,_src,_dst,_eflags,_wx,_wy,_lx,_ly,_qx,_qy);\
        break;                                                             \
    }                                                                      \
} while (0)
/* Source operand is byte-sized and may be restricted to just %cl. */
#define emulate_2op_SrcB(_op, _src, _dst, _eflags)                         \
    __emulate_2op(_op, _src, _dst, _eflags,                                \
                  "b", "c", "b", "c", "b", "c", "b", "c")
/* Source operand is byte, word, long or quad sized. */
#define emulate_2op_SrcV(_op, _src, _dst, _eflags)                         \
    __emulate_2op(_op, _src, _dst, _eflags,                                \
                  "b", "q", "w", "r", _LO32, "r", "", "r")
/* Source operand is word, long or quad sized. */
#define emulate_2op_SrcV_nobyte(_op, _src, _dst, _eflags)                  \
    __emulate_2op_nobyte(_op, _src, _dst, _eflags,                         \
                  "w", "r", _LO32, "r", "", "r")

/* Instruction has only one explicit operand (no source operand). */
#define emulate_1op(_op,_dst,_eflags)                                      \
do{ unsigned long _tmp;                                                    \
    switch ( (_dst).bytes )                                                \
    {                                                                      \
    case 1:                                                                \
        asm volatile (                                                     \
            _PRE_EFLAGS("0","3","2")                                       \
            _op"b %1; "                                                    \
            _POST_EFLAGS("0","3","2")                                      \
            : "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp)              \
            : "i" (EFLAGS_MASK), "m" (_eflags), "m" ((_dst).val) );        \
        break;                                                             \
    case 2:                                                                \
        asm volatile (                                                     \
            _PRE_EFLAGS("0","3","2")                                       \
            _op"w %1; "                                                    \
            _POST_EFLAGS("0","3","2")                                      \
            : "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp)              \
            : "i" (EFLAGS_MASK), "m" (_eflags), "m" ((_dst).val) );        \
        break;                                                             \
    case 4:                                                                \
        asm volatile (                                                     \
            _PRE_EFLAGS("0","3","2")                                       \
            _op"l %1; "                                                    \
            _POST_EFLAGS("0","3","2")                                      \
            : "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp)              \
            : "i" (EFLAGS_MASK), "m" (_eflags), "m" ((_dst).val) );        \
        break;                                                             \
    case 8:                                                                \
        __emulate_1op_8byte(_op, _dst, _eflags);                           \
        break;                                                             \
    }                                                                      \
} while (0)

/* Emulate an instruction with quadword operands (x86/64 only). */
#if defined(__x86_64__)
#define __emulate_2op_8byte(_op, _src, _dst, _eflags, _qx, _qy)         \
do{ asm volatile (                                                      \
        _PRE_EFLAGS("0","4","2")                                        \
        _op"q %"_qx"3,%1; "                                             \
        _POST_EFLAGS("0","4","2")                                       \
        : "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp)               \
        : _qy ((_src).val), "i" (EFLAGS_MASK),                          \
          "m" (_eflags), "m" ((_dst).val) );                            \
} while (0)
#define __emulate_1op_8byte(_op, _dst, _eflags)                         \
do{ asm volatile (                                                      \
        _PRE_EFLAGS("0","3","2")                                        \
        _op"q %1; "                                                     \
        _POST_EFLAGS("0","3","2")                                       \
        : "=m" (_eflags), "=m" ((_dst).val), "=&r" (_tmp)               \
        : "i" (EFLAGS_MASK), "m" (_eflags), "m" ((_dst).val) );         \
} while (0)
#elif defined(__i386__)
#define __emulate_2op_8byte(_op, _src, _dst, _eflags, _qx, _qy)
#define __emulate_1op_8byte(_op, _dst, _eflags)
#endif /* __i386__ */

/* Fetch next part of the instruction being emulated. */
#define insn_fetch_bytes(_size)                                         \
({ unsigned long _x, _eip = _regs.eip;                                  \
   if ( !mode_64bit() ) _eip = (uint32_t)_eip; /* ignore upper dword */ \
   _regs.eip += (_size); /* real hardware doesn't truncate */           \
   generate_exception_if((uint8_t)(_regs.eip - ctxt->regs->eip) > 15,   \
                         EXC_GP);                                       \
   rc = ops->insn_fetch(x86_seg_cs, _eip, &_x, (_size), ctxt);          \
   if ( rc ) goto done;                                                 \
   _x;                                                                  \
})
#define insn_fetch_type(_type) ((_type)insn_fetch_bytes(sizeof(_type)))

#define _truncate_ea(ea, byte_width)            \
({  unsigned long __ea = (ea);                  \
    unsigned int _width = (byte_width);         \
    ((_width == sizeof(unsigned long)) ? __ea : \
     (__ea & ((1UL << (_width << 3)) - 1)));    \
})
#define truncate_ea(ea) _truncate_ea((ea), ad_bytes)

#define mode_64bit() (def_ad_bytes == 8)

#define fail_if(p)                                      \
do {                                                    \
    rc = (p) ? X86EMUL_UNHANDLEABLE : X86EMUL_OKAY;     \
    if ( rc ) goto done;                                \
} while (0)

#define generate_exception_if(p, e)                                     \
({  if ( (p) ) {                                                        \
        fail_if(ops->inject_hw_exception == NULL);                      \
        rc = ops->inject_hw_exception(e, ctxt) ? : X86EMUL_EXCEPTION;   \
        goto done;                                                      \
    }                                                                   \
})

/* Given byte has even parity (even number of 1s)? */
static int even_parity(uint8_t v)
{
    asm ( "test %%al,%%al; setp %%al"
              : "=a" (v) : "0" (v) );
    return v;
}

/* Update address held in a register, based on addressing mode. */
#define _register_address_increment(reg, inc, byte_width)               \
do {                                                                    \
    int _inc = (inc); /* signed type ensures sign extension to long */  \
    unsigned int _width = (byte_width);                                 \
    if ( _width == sizeof(unsigned long) )                              \
        (reg) += _inc;                                                  \
    else if ( mode_64bit() )                                            \
        (reg) = ((reg) + _inc) & ((1UL << (_width << 3)) - 1);          \
    else                                                                \
        (reg) = ((reg) & ~((1UL << (_width << 3)) - 1)) |               \
                (((reg) + _inc) & ((1UL << (_width << 3)) - 1));        \
} while (0)
#define register_address_increment(reg, inc) \
    _register_address_increment((reg), (inc), ad_bytes)

#define sp_pre_dec(dec) ({                                              \
    _register_address_increment(_regs.esp, -(dec), ctxt->sp_size/8);    \
    _truncate_ea(_regs.esp, ctxt->sp_size/8);                           \
})
#define sp_post_inc(inc) ({                                             \
    unsigned long __esp = _truncate_ea(_regs.esp, ctxt->sp_size/8);     \
    _register_address_increment(_regs.esp, (inc), ctxt->sp_size/8);     \
    __esp;                                                              \
})

#define jmp_rel(rel)                                                    \
do {                                                                    \
    _regs.eip += (int)(rel);                                            \
    if ( !mode_64bit() )                                                \
        _regs.eip = ((op_bytes == 2)                                    \
                     ? (uint16_t)_regs.eip : (uint32_t)_regs.eip);      \
} while (0)

static int __handle_rep_prefix(
    struct cpu_user_regs *int_regs,
    struct cpu_user_regs *ext_regs,
    int ad_bytes)
{
    unsigned long ecx = ((ad_bytes == 2) ? (uint16_t)int_regs->ecx :
                         (ad_bytes == 4) ? (uint32_t)int_regs->ecx :
                         int_regs->ecx);

    if ( ecx-- == 0 )
    {
        ext_regs->eip = int_regs->eip;
        return 1;
    }

    if ( ad_bytes == 2 )
        *(uint16_t *)&int_regs->ecx = ecx;
    else if ( ad_bytes == 4 )
        int_regs->ecx = (uint32_t)ecx;
    else
        int_regs->ecx = ecx;
    int_regs->eip = ext_regs->eip;
    return 0;
}

#define handle_rep_prefix()                                                \
do {                                                                       \
    if ( rep_prefix && __handle_rep_prefix(&_regs, ctxt->regs, ad_bytes) ) \
        goto done;                                                         \
} while (0)

/*
 * Unsigned multiplication with double-word result.
 * IN:  Multiplicand=m[0], Multiplier=m[1]
 * OUT: Return CF/OF (overflow status); Result=m[1]:m[0]
 */
static int mul_dbl(unsigned long m[2])
{
    int rc;
    asm ( "mul %4; seto %b2"
          : "=a" (m[0]), "=d" (m[1]), "=q" (rc)
          : "0" (m[0]), "1" (m[1]), "2" (0) );
    return rc;
}

/*
 * Signed multiplication with double-word result.
 * IN:  Multiplicand=m[0], Multiplier=m[1]
 * OUT: Return CF/OF (overflow status); Result=m[1]:m[0]
 */
static int imul_dbl(unsigned long m[2])
{
    int rc;
    asm ( "imul %4; seto %b2"
          : "=a" (m[0]), "=d" (m[1]), "=q" (rc)
          : "0" (m[0]), "1" (m[1]), "2" (0) );
    return rc;
}

/*
 * Unsigned division of double-word dividend.
 * IN:  Dividend=u[1]:u[0], Divisor=v
 * OUT: Return 1: #DE
 *      Return 0: Quotient=u[0], Remainder=u[1]
 */
static int div_dbl(unsigned long u[2], unsigned long v)
{
    if ( (v == 0) || (u[1] >= v) )
        return 1;
    asm ( "div %4"
          : "=a" (u[0]), "=d" (u[1])
          : "0" (u[0]), "1" (u[1]), "r" (v) );
    return 0;
}

/*
 * Signed division of double-word dividend.
 * IN:  Dividend=u[1]:u[0], Divisor=v
 * OUT: Return 1: #DE
 *      Return 0: Quotient=u[0], Remainder=u[1]
 * NB. We don't use idiv directly as it's moderately hard to work out
 *     ahead of time whether it will #DE, which we cannot allow to happen.
 */
static int idiv_dbl(unsigned long u[2], unsigned long v)
{
    int negu = (long)u[1] < 0, negv = (long)v < 0;

    /* u = abs(u) */
    if ( negu )
    {
        u[1] = ~u[1];
        if ( (u[0] = -u[0]) == 0 )
            u[1]++;
    }

    /* abs(u) / abs(v) */
    if ( div_dbl(u, negv ? -v : v) )
        return 1;

    /* Remainder has same sign as dividend. It cannot overflow. */
    if ( negu )
        u[1] = -u[1];

    /* Quotient is overflowed if sign bit is set. */
    if ( negu ^ negv )
    {
        if ( (long)u[0] >= 0 )
            u[0] = -u[0];
        else if ( (u[0] << 1) != 0 ) /* == 0x80...0 is okay */
            return 1;
    }
    else if ( (long)u[0] < 0 )
        return 1;

    return 0;
}

static int
test_cc(
    unsigned int condition, unsigned int flags)
{
    int rc = 0;

    switch ( (condition & 15) >> 1 )
    {
    case 0: /* o */
        rc |= (flags & EFLG_OF);
        break;
    case 1: /* b/c/nae */
        rc |= (flags & EFLG_CF);
        break;
    case 2: /* z/e */
        rc |= (flags & EFLG_ZF);
        break;
    case 3: /* be/na */
        rc |= (flags & (EFLG_CF|EFLG_ZF));
        break;
    case 4: /* s */
        rc |= (flags & EFLG_SF);
        break;
    case 5: /* p/pe */
        rc |= (flags & EFLG_PF);
        break;
    case 7: /* le/ng */
        rc |= (flags & EFLG_ZF);
        /* fall through */
    case 6: /* l/nge */
        rc |= (!(flags & EFLG_SF) != !(flags & EFLG_OF));
        break;
    }

    /* Odd condition identifiers (lsb == 1) have inverted sense. */
    return (!!rc ^ (condition & 1));
}

static int
get_cpl(
    struct x86_emulate_ctxt *ctxt,
    struct x86_emulate_ops  *ops)
{
    struct segment_register reg;

    if ( ctxt->regs->eflags & EFLG_VM )
        return 3;

    if ( (ops->read_segment == NULL) ||
         ops->read_segment(x86_seg_ss, &reg, ctxt) )
        return -1;

    return reg.attr.fields.dpl;
}

static int
_mode_iopl(
    struct x86_emulate_ctxt *ctxt,
    struct x86_emulate_ops  *ops)
{
    int cpl = get_cpl(ctxt, ops);
    return ((cpl >= 0) && (cpl <= ((ctxt->regs->eflags >> 12) & 3)));
}

#define mode_ring0() (get_cpl(ctxt, ops) == 0)
#define mode_iopl()  _mode_iopl(ctxt, ops)

static int
in_realmode(
    struct x86_emulate_ctxt *ctxt,
    struct x86_emulate_ops  *ops)
{
    unsigned long cr0;
    int rc;

    if ( ops->read_cr == NULL )
        return 0;

    rc = ops->read_cr(0, &cr0, ctxt);
    return (!rc && !(cr0 & 1));
}

static int
load_seg(
    enum x86_segment seg,
    uint16_t sel,
    struct x86_emulate_ctxt *ctxt,
    struct x86_emulate_ops *ops)
{
    struct segment_register reg;
    int rc;

    if ( !in_realmode(ctxt, ops) ||
         (ops->read_segment == NULL) ||
         (ops->write_segment == NULL) )
        return X86EMUL_UNHANDLEABLE;

    if ( (rc = ops->read_segment(seg, &reg, ctxt)) != 0 )
        return rc;

    reg.sel  = sel;
    reg.base = (uint32_t)sel << 4;

    return ops->write_segment(seg, &reg, ctxt);
}

void *
decode_register(
    uint8_t modrm_reg, struct cpu_user_regs *regs, int highbyte_regs)
{
    void *p;

    switch ( modrm_reg )
    {
    case  0: p = &regs->eax; break;
    case  1: p = &regs->ecx; break;
    case  2: p = &regs->edx; break;
    case  3: p = &regs->ebx; break;
    case  4: p = (highbyte_regs ?
                  ((unsigned char *)&regs->eax + 1) : 
                  (unsigned char *)&regs->esp); break;
    case  5: p = (highbyte_regs ?
                  ((unsigned char *)&regs->ecx + 1) : 
                  (unsigned char *)&regs->ebp); break;
    case  6: p = (highbyte_regs ?
                  ((unsigned char *)&regs->edx + 1) : 
                  (unsigned char *)&regs->esi); break;
    case  7: p = (highbyte_regs ?
                  ((unsigned char *)&regs->ebx + 1) : 
                  (unsigned char *)&regs->edi); break;
#if defined(__x86_64__)
    case  8: p = &regs->r8;  break;
    case  9: p = &regs->r9;  break;
    case 10: p = &regs->r10; break;
    case 11: p = &regs->r11; break;
    case 12: p = &regs->r12; break;
    case 13: p = &regs->r13; break;
    case 14: p = &regs->r14; break;
    case 15: p = &regs->r15; break;
#endif
    default: p = NULL; break;
    }

    return p;
}

#define decode_segment_failed x86_seg_tr
enum x86_segment
decode_segment(
    uint8_t modrm_reg)
{
    switch ( modrm_reg )
    {
    case 0: return x86_seg_es;
    case 1: return x86_seg_cs;
    case 2: return x86_seg_ss;
    case 3: return x86_seg_ds;
    case 4: return x86_seg_fs;
    case 5: return x86_seg_gs;
    default: break;
    }
    return decode_segment_failed;
}

int
x86_emulate(
    struct x86_emulate_ctxt *ctxt,
    struct x86_emulate_ops  *ops)
{
    /* Shadow copy of register state. Committed on successful emulation. */
    struct cpu_user_regs _regs = *ctxt->regs;

    uint8_t b, d, sib, sib_index, sib_base, twobyte = 0, rex_prefix = 0;
    uint8_t modrm, modrm_mod = 0, modrm_reg = 0, modrm_rm = 0;
    unsigned int op_bytes, def_op_bytes, ad_bytes, def_ad_bytes;
#define REPE_PREFIX  1
#define REPNE_PREFIX 2
    unsigned int lock_prefix = 0, rep_prefix = 0;
    int override_seg = -1, rc = X86EMUL_OKAY;
    struct operand src, dst;

    /* Data operand effective address (usually computed from ModRM). */
    struct operand ea;

    /* Default is a memory operand relative to segment DS. */
    ea.type    = OP_MEM;
    ea.mem.seg = x86_seg_ds;
    ea.mem.off = 0;

    op_bytes = def_op_bytes = ad_bytes = def_ad_bytes = ctxt->addr_size/8;
    if ( op_bytes == 8 )
    {
        op_bytes = def_op_bytes = 4;
#ifndef __x86_64__
        return X86EMUL_UNHANDLEABLE;
#endif
    }

    /* Prefix bytes. */
    for ( ; ; )
    {
        switch ( b = insn_fetch_type(uint8_t) )
        {
        case 0x66: /* operand-size override */
            op_bytes = def_op_bytes ^ 6;
            break;
        case 0x67: /* address-size override */
            ad_bytes = def_ad_bytes ^ (mode_64bit() ? 12 : 6);
            break;
        case 0x2e: /* CS override */
            override_seg = x86_seg_cs;
            break;
        case 0x3e: /* DS override */
            override_seg = x86_seg_ds;
            break;
        case 0x26: /* ES override */
            override_seg = x86_seg_es;
            break;
        case 0x64: /* FS override */
            override_seg = x86_seg_fs;
            break;
        case 0x65: /* GS override */
            override_seg = x86_seg_gs;
            break;
        case 0x36: /* SS override */
            override_seg = x86_seg_ss;
            break;
        case 0xf0: /* LOCK */
            lock_prefix = 1;
            break;
        case 0xf2: /* REPNE/REPNZ */
            rep_prefix = REPNE_PREFIX;
            break;
        case 0xf3: /* REP/REPE/REPZ */
            rep_prefix = REPE_PREFIX;
            break;
        case 0x40 ... 0x4f: /* REX */
            if ( !mode_64bit() )
                goto done_prefixes;
            rex_prefix = b;
            continue;
        default:
            goto done_prefixes;
        }

        /* Any legacy prefix after a REX prefix nullifies its effect. */
        rex_prefix = 0;
    }
 done_prefixes:

    if ( rex_prefix & 8 ) /* REX.W */
        op_bytes = 8;

    /* Opcode byte(s). */
    d = opcode_table[b];
    if ( d == 0 )
    {
        /* Two-byte opcode? */
        if ( b == 0x0f )
        {
            twobyte = 1;
            b = insn_fetch_type(uint8_t);
            d = twobyte_table[b];
        }

        /* Unrecognised? */
        if ( d == 0 )
            goto cannot_emulate;
    }

    /* Lock prefix is allowed only on RMW instructions. */
    generate_exception_if((d & Mov) && lock_prefix, EXC_GP);

    /* ModRM and SIB bytes. */
    if ( d & ModRM )
    {
        modrm = insn_fetch_type(uint8_t);
        modrm_mod = (modrm & 0xc0) >> 6;
        modrm_reg = ((rex_prefix & 4) << 1) | ((modrm & 0x38) >> 3);
        modrm_rm  = modrm & 0x07;

        if ( modrm_mod == 3 )
        {
            modrm_rm |= (rex_prefix & 1) << 3;
            ea.type = OP_REG;
            ea.reg  = decode_register(
                modrm_rm, &_regs, (d & ByteOp) && (rex_prefix == 0));
        }
        else if ( ad_bytes == 2 )
        {
            /* 16-bit ModR/M decode. */
            switch ( modrm_rm )
            {
            case 0:
                ea.mem.off = _regs.ebx + _regs.esi;
                break;
            case 1:
                ea.mem.off = _regs.ebx + _regs.edi;
                break;
            case 2:
                ea.mem.seg = x86_seg_ss;
                ea.mem.off = _regs.ebp + _regs.esi;
                break;
            case 3:
                ea.mem.seg = x86_seg_ss;
                ea.mem.off = _regs.ebp + _regs.edi;
                break;
            case 4:
                ea.mem.off = _regs.esi;
                break;
            case 5:
                ea.mem.off = _regs.edi;
                break;
            case 6:
                if ( modrm_mod == 0 )
                    break;
                ea.mem.seg = x86_seg_ss;
                ea.mem.off = _regs.ebp;
                break;
            case 7:
                ea.mem.off = _regs.ebx;
                break;
            }
            switch ( modrm_mod )
            {
            case 0:
                if ( modrm_rm == 6 )
                    ea.mem.off = insn_fetch_type(int16_t);
                break;
            case 1:
                ea.mem.off += insn_fetch_type(int8_t);
                break;
            case 2:
                ea.mem.off += insn_fetch_type(int16_t);
                break;
            }
            ea.mem.off = truncate_ea(ea.mem.off);
        }
        else
        {
            /* 32/64-bit ModR/M decode. */
            if ( modrm_rm == 4 )
            {
                sib = insn_fetch_type(uint8_t);
                sib_index = ((sib >> 3) & 7) | ((rex_prefix << 2) & 8);
                sib_base  = (sib & 7) | ((rex_prefix << 3) & 8);
                if ( sib_index != 4 )
                    ea.mem.off = *(long*)decode_register(sib_index, &_regs, 0);
                ea.mem.off <<= (sib >> 6) & 3;
                if ( (modrm_mod == 0) && ((sib_base & 7) == 5) )
                    ea.mem.off += insn_fetch_type(int32_t);
                else if ( sib_base == 4 )
                {
                    ea.mem.seg  = x86_seg_ss;
                    ea.mem.off += _regs.esp;
                    if ( !twobyte && (b == 0x8f) )
                        /* POP <rm> computes its EA post increment. */
                        ea.mem.off += ((mode_64bit() && (op_bytes == 4))
                                       ? 8 : op_bytes);
                }
                else if ( sib_base == 5 )
                {
                    ea.mem.seg  = x86_seg_ss;
                    ea.mem.off += _regs.ebp;
                }
                else
                    ea.mem.off += *(long*)decode_register(sib_base, &_regs, 0);
            }
            else
            {
                modrm_rm |= (rex_prefix & 1) << 3;
                ea.mem.off = *(long *)decode_register(modrm_rm, &_regs, 0);
                if ( (modrm_rm == 5) && (modrm_mod != 0) )
                    ea.mem.seg = x86_seg_ss;
            }
            switch ( modrm_mod )
            {
            case 0:
                if ( (modrm_rm & 7) != 5 )
                    break;
                ea.mem.off = insn_fetch_type(int32_t);
                if ( !mode_64bit() )
                    break;
                /* Relative to RIP of next instruction. Argh! */
                ea.mem.off += _regs.eip;
                if ( (d & SrcMask) == SrcImm )
                    ea.mem.off += (d & ByteOp) ? 1 :
                        ((op_bytes == 8) ? 4 : op_bytes);
                else if ( (d & SrcMask) == SrcImmByte )
                    ea.mem.off += 1;
                else if ( ((b == 0xf6) || (b == 0xf7)) &&
                          ((modrm_reg & 7) <= 1) )
                    /* Special case in Grp3: test has immediate operand. */
                    ea.mem.off += (d & ByteOp) ? 1
                        : ((op_bytes == 8) ? 4 : op_bytes);
                break;
            case 1:
                ea.mem.off += insn_fetch_type(int8_t);
                break;
            case 2:
                ea.mem.off += insn_fetch_type(int32_t);
                break;
            }
            ea.mem.off = truncate_ea(ea.mem.off);
        }
    }

    if ( override_seg != -1 )
        ea.mem.seg = override_seg;

    /* Special instructions do their own operand decoding. */
    if ( (d & DstMask) == ImplicitOps )
        goto special_insn;

    /* Decode and fetch the source operand: register, memory or immediate. */
    switch ( d & SrcMask )
    {
    case SrcNone:
        break;
    case SrcReg:
        src.type = OP_REG;
        if ( d & ByteOp )
        {
            src.reg = decode_register(modrm_reg, &_regs, (rex_prefix == 0));
            src.val = *(uint8_t *)src.reg;
            src.bytes = 1;
        }
        else
        {
            src.reg = decode_register(modrm_reg, &_regs, 0);
            switch ( (src.bytes = op_bytes) )
            {
            case 2: src.val = *(uint16_t *)src.reg; break;
            case 4: src.val = *(uint32_t *)src.reg; break;
            case 8: src.val = *(uint64_t *)src.reg; break;
            }
        }
        break;
    case SrcMem16:
        ea.bytes = 2;
        goto srcmem_common;
    case SrcMem:
        ea.bytes = (d & ByteOp) ? 1 : op_bytes;
    srcmem_common:
        src = ea;
        if ( src.type == OP_REG )
        {
            switch ( src.bytes )
            {
            case 1: src.val = *(uint8_t  *)src.reg; break;
            case 2: src.val = *(uint16_t *)src.reg; break;
            case 4: src.val = *(uint32_t *)src.reg; break;
            case 8: src.val = *(uint64_t *)src.reg; break;
            }
        }
        else if ( (rc = ops->read(src.mem.seg, src.mem.off,
                                  &src.val, src.bytes, ctxt)) )
            goto done;
        break;
    case SrcImm:
        src.type  = OP_IMM;
        src.bytes = (d & ByteOp) ? 1 : op_bytes;
        if ( src.bytes == 8 ) src.bytes = 4;
        /* NB. Immediates are sign-extended as necessary. */
        switch ( src.bytes )
        {
        case 1: src.val = insn_fetch_type(int8_t);  break;
        case 2: src.val = insn_fetch_type(int16_t); break;
        case 4: src.val = insn_fetch_type(int32_t); break;
        }
        break;
    case SrcImmByte:
        src.type  = OP_IMM;
        src.bytes = 1;
        src.val   = insn_fetch_type(int8_t);
        break;
    }

    /* Decode and fetch the destination operand: register or memory. */
    switch ( d & DstMask )
    {
    case DstReg:
        dst.type = OP_REG;
        if ( d & ByteOp )
        {
            dst.reg = decode_register(modrm_reg, &_regs, (rex_prefix == 0));
            dst.val = *(uint8_t *)dst.reg;
            dst.bytes = 1;
        }
        else
        {
            dst.reg = decode_register(modrm_reg, &_regs, 0);
            switch ( (dst.bytes = op_bytes) )
            {
            case 2: dst.val = *(uint16_t *)dst.reg; break;
            case 4: dst.val = *(uint32_t *)dst.reg; break;
            case 8: dst.val = *(uint64_t *)dst.reg; break;
            }
        }
        break;
    case DstBitBase:
        if ( ((d & SrcMask) == SrcImmByte) || (ea.type == OP_REG) )
        {
            src.val &= (op_bytes << 3) - 1;
        }
        else
        {
            /*
             * EA       += BitOffset DIV op_bytes*8
             * BitOffset = BitOffset MOD op_bytes*8
             * DIV truncates towards negative infinity.
             * MOD always produces a positive result.
             */
            if ( op_bytes == 2 )
                src.val = (int16_t)src.val;
            else if ( op_bytes == 4 )
                src.val = (int32_t)src.val;
            if ( (long)src.val < 0 )
            {
                unsigned long byte_offset;
                byte_offset = op_bytes + (((-src.val-1) >> 3) & ~(op_bytes-1));
                ea.mem.off -= byte_offset;
                src.val = (byte_offset << 3) + src.val;
            }
            else
            {
                ea.mem.off += (src.val >> 3) & ~(op_bytes - 1);
                src.val &= (op_bytes << 3) - 1;
            }
        }
        /* Becomes a normal DstMem operation from here on. */
        d = (d & ~DstMask) | DstMem;
    case DstMem:
        ea.bytes = (d & ByteOp) ? 1 : op_bytes;
        dst = ea;
        if ( dst.type == OP_REG )
        {
            switch ( dst.bytes )
            {
            case 1: dst.val = *(uint8_t  *)dst.reg; break;
            case 2: dst.val = *(uint16_t *)dst.reg; break;
            case 4: dst.val = *(uint32_t *)dst.reg; break;
            case 8: dst.val = *(uint64_t *)dst.reg; break;
            }
        }
        else if ( !(d & Mov) ) /* optimisation - avoid slow emulated read */
        {
            if ( (rc = ops->read(dst.mem.seg, dst.mem.off,
                                 &dst.val, dst.bytes, ctxt)) )
                goto done;
            dst.orig_val = dst.val;
        }
        break;
    }

    /* LOCK prefix allowed only on instructions with memory destination. */
    generate_exception_if(lock_prefix && (dst.type != OP_MEM), EXC_GP);

    if ( twobyte )
        goto twobyte_insn;

    switch ( b )
    {
    case 0x04 ... 0x05: /* add imm,%%eax */
        dst.reg = (unsigned long *)&_regs.eax;
        dst.val = _regs.eax;
    case 0x00 ... 0x03: add: /* add */
        emulate_2op_SrcV("add", src, dst, _regs.eflags);
        break;

    case 0x0c ... 0x0d: /* or imm,%%eax */
        dst.reg = (unsigned long *)&_regs.eax;
        dst.val = _regs.eax;
    case 0x08 ... 0x0b: or:  /* or */
        emulate_2op_SrcV("or", src, dst, _regs.eflags);
        break;

    case 0x14 ... 0x15: /* adc imm,%%eax */
        dst.reg = (unsigned long *)&_regs.eax;
        dst.val = _regs.eax;
    case 0x10 ... 0x13: adc: /* adc */
        emulate_2op_SrcV("adc", src, dst, _regs.eflags);
        break;

    case 0x1c ... 0x1d: /* sbb imm,%%eax */
        dst.reg = (unsigned long *)&_regs.eax;
        dst.val = _regs.eax;
    case 0x18 ... 0x1b: sbb: /* sbb */
        emulate_2op_SrcV("sbb", src, dst, _regs.eflags);
        break;

    case 0x24 ... 0x25: /* and imm,%%eax */
        dst.reg = (unsigned long *)&_regs.eax;
        dst.val = _regs.eax;
    case 0x20 ... 0x23: and: /* and */
        emulate_2op_SrcV("and", src, dst, _regs.eflags);
        break;

    case 0x2c ... 0x2d: /* sub imm,%%eax */
        dst.reg = (unsigned long *)&_regs.eax;
        dst.val = _regs.eax;
    case 0x28 ... 0x2b: sub: /* sub */
        emulate_2op_SrcV("sub", src, dst, _regs.eflags);
        break;

    case 0x34 ... 0x35: /* xor imm,%%eax */
        dst.reg = (unsigned long *)&_regs.eax;
        dst.val = _regs.eax;
    case 0x30 ... 0x33: xor: /* xor */
        emulate_2op_SrcV("xor", src, dst, _regs.eflags);
        break;

    case 0x3c ... 0x3d: /* cmp imm,%%eax */
        dst.reg = (unsigned long *)&_regs.eax;
        dst.val = _regs.eax;
    case 0x38 ... 0x3b: cmp: /* cmp */
        emulate_2op_SrcV("cmp", src, dst, _regs.eflags);
        break;

    case 0x62: /* bound */ {
        unsigned long src_val2;
        int lb, ub, idx;
        generate_exception_if(mode_64bit() || (src.type != OP_MEM), EXC_UD);
        if ( (rc = ops->read(src.mem.seg, src.mem.off + op_bytes,
                             &src_val2, op_bytes, ctxt)) )
            goto done;
        ub  = (op_bytes == 2) ? (int16_t)src_val2 : (int32_t)src_val2;
        lb  = (op_bytes == 2) ? (int16_t)src.val  : (int32_t)src.val;
        idx = (op_bytes == 2) ? (int16_t)dst.val  : (int32_t)dst.val;
        generate_exception_if((idx < lb) || (idx > ub), EXC_BR);
        dst.type = OP_NONE;
        break;
    }

    case 0x63: /* movsxd (x86/64) / arpl (x86/32) */
        if ( mode_64bit() )
        {
            /* movsxd */
            if ( src.type == OP_REG )
                src.val = *(int32_t *)src.reg;
            else if ( (rc = ops->read(src.mem.seg, src.mem.off,
                                      &src.val, 4, ctxt)) )
                goto done;
            dst.val = (int32_t)src.val;
        }
        else
        {
            /* arpl */
            uint16_t src_val = dst.val;
            dst = src;
            _regs.eflags &= ~EFLG_ZF;
            _regs.eflags |= ((src_val & 3) > (dst.val & 3)) ? EFLG_ZF : 0;
            if ( _regs.eflags & EFLG_ZF )
                dst.val  = (dst.val & ~3) | (src_val & 3);
            else
                dst.type = OP_NONE;
            generate_exception_if(in_realmode(ctxt, ops), EXC_UD);
        }
        break;

    case 0x69: /* imul imm16/32 */
    case 0x6b: /* imul imm8 */ {
        unsigned long reg = *(long *)decode_register(modrm_reg, &_regs, 0);
        _regs.eflags &= ~(EFLG_OF|EFLG_CF);
        switch ( dst.bytes )
        {
        case 2:
            dst.val = ((uint32_t)(int16_t)src.val *
                       (uint32_t)(int16_t)reg);
            if ( (int16_t)dst.val != (uint32_t)dst.val )
                _regs.eflags |= EFLG_OF|EFLG_CF;
            break;
#ifdef __x86_64__
        case 4:
            dst.val = ((uint64_t)(int32_t)src.val *
                       (uint64_t)(int32_t)reg);
            if ( (int32_t)dst.val != dst.val )
                _regs.eflags |= EFLG_OF|EFLG_CF;
            break;
#endif
        default: {
            unsigned long m[2] = { src.val, reg };
            if ( imul_dbl(m) )
                _regs.eflags |= EFLG_OF|EFLG_CF;
            dst.val = m[0];
            break;
        }
        }
        dst.type = OP_REG;
        dst.reg  = decode_register(modrm_reg, &_regs, 0);
        break;
    }

    case 0x82: /* Grp1 (x86/32 only) */
        generate_exception_if(mode_64bit(), EXC_UD);
    case 0x80: case 0x81: case 0x83: /* Grp1 */
        switch ( modrm_reg & 7 )
        {
        case 0: goto add;
        case 1: goto or;
        case 2: goto adc;
        case 3: goto sbb;
        case 4: goto and;
        case 5: goto sub;
        case 6: goto xor;
        case 7: goto cmp;
        }
        break;

    case 0xa8 ... 0xa9: /* test imm,%%eax */
        dst.reg = (unsigned long *)&_regs.eax;
        dst.val = _regs.eax;
    case 0x84 ... 0x85: test: /* test */
        emulate_2op_SrcV("test", src, dst, _regs.eflags);
        break;

    case 0x86 ... 0x87: xchg: /* xchg */
        /* Write back the register source. */
        switch ( dst.bytes )
        {
        case 1: *(uint8_t  *)src.reg = (uint8_t)dst.val; break;
        case 2: *(uint16_t *)src.reg = (uint16_t)dst.val; break;
        case 4: *src.reg = (uint32_t)dst.val; break; /* 64b reg: zero-extend */
        case 8: *src.reg = dst.val; break;
        }
        /* Write back the memory destination with implicit LOCK prefix. */
        dst.val = src.val;
        lock_prefix = 1;
        break;

    case 0xc6 ... 0xc7: /* mov (sole member of Grp11) */
        generate_exception_if((modrm_reg & 7) != 0, EXC_UD);
    case 0x88 ... 0x8b: /* mov */
        dst.val = src.val;
        break;

    case 0x8c: /* mov Sreg,r/m */ {
        struct segment_register reg;
        enum x86_segment seg = decode_segment(modrm_reg);
        generate_exception_if(seg == decode_segment_failed, EXC_UD);
        fail_if(ops->read_segment == NULL);
        if ( (rc = ops->read_segment(seg, &reg, ctxt)) != 0 )
            goto done;
        dst.val = reg.sel;
        if ( dst.type == OP_MEM )
            dst.bytes = 2;
        break;
    }

    case 0x8e: /* mov r/m,Sreg */ {
        enum x86_segment seg = decode_segment(modrm_reg);
        generate_exception_if(seg == decode_segment_failed, EXC_UD);
        if ( (rc = load_seg(seg, (uint16_t)src.val, ctxt, ops)) != 0 )
            goto done;
        dst.type = OP_NONE;
        break;
    }

    case 0x8d: /* lea */
        dst.val = ea.mem.off;
        break;

    case 0x8f: /* pop (sole member of Grp1a) */
        generate_exception_if((modrm_reg & 7) != 0, EXC_UD);
        /* 64-bit mode: POP defaults to a 64-bit operand. */
        if ( mode_64bit() && (dst.bytes == 4) )
            dst.bytes = 8;
        if ( (rc = ops->read(x86_seg_ss, sp_post_inc(dst.bytes),
                             &dst.val, dst.bytes, ctxt)) != 0 )
            goto done;
        break;

    case 0xb0 ... 0xb7: /* mov imm8,r8 */
        dst.reg = decode_register(
            (b & 7) | ((rex_prefix & 1) << 3), &_regs, (rex_prefix == 0));
        dst.val = src.val;
        break;

    case 0xb8 ... 0xbf: /* mov imm{16,32,64},r{16,32,64} */
        if ( dst.bytes == 8 ) /* Fetch more bytes to obtain imm64 */
            src.val = ((uint32_t)src.val |
                       ((uint64_t)insn_fetch_type(uint32_t) << 32));
        dst.reg = decode_register(
            (b & 7) | ((rex_prefix & 1) << 3), &_regs, 0);
        dst.val = src.val;
        break;

    case 0xc0 ... 0xc1: grp2: /* Grp2 */
        switch ( modrm_reg & 7 )
        {
        case 0: /* rol */
            emulate_2op_SrcB("rol", src, dst, _regs.eflags);
            break;
        case 1: /* ror */
            emulate_2op_SrcB("ror", src, dst, _regs.eflags);
            break;
        case 2: /* rcl */
            emulate_2op_SrcB("rcl", src, dst, _regs.eflags);
            break;
        case 3: /* rcr */
            emulate_2op_SrcB("rcr", src, dst, _regs.eflags);
            break;
        case 4: /* sal/shl */
        case 6: /* sal/shl */
            emulate_2op_SrcB("sal", src, dst, _regs.eflags);
            break;
        case 5: /* shr */
            emulate_2op_SrcB("shr", src, dst, _regs.eflags);
            break;
        case 7: /* sar */
            emulate_2op_SrcB("sar", src, dst, _regs.eflags);
            break;
        }
        break;

    case 0xc4: /* les */ {
        unsigned long sel;
        dst.val = x86_seg_es;
    les:
        generate_exception_if(src.type != OP_MEM, EXC_UD);
        if ( (rc = ops->read(src.mem.seg, src.mem.off + src.bytes,
                             &sel, 2, ctxt)) != 0 )
            goto done;
        if ( (rc = load_seg(dst.val, (uint16_t)sel, ctxt, ops)) != 0 )
            goto done;
        dst.val = src.val;
        break;
    }

    case 0xc5: /* lds */
        dst.val = x86_seg_ds;
        goto les;

    case 0xd0 ... 0xd1: /* Grp2 */
        src.val = 1;
        goto grp2;

    case 0xd2 ... 0xd3: /* Grp2 */
        src.val = _regs.ecx;
        goto grp2;

    case 0xf6 ... 0xf7: /* Grp3 */
        switch ( modrm_reg & 7 )
        {
        case 0 ... 1: /* test */
            /* Special case in Grp3: test has an immediate source operand. */
            src.type = OP_IMM;
            src.bytes = (d & ByteOp) ? 1 : op_bytes;
            if ( src.bytes == 8 ) src.bytes = 4;
            switch ( src.bytes )
            {
            case 1: src.val = insn_fetch_type(int8_t);  break;
            case 2: src.val = insn_fetch_type(int16_t); break;
            case 4: src.val = insn_fetch_type(int32_t); break;
            }
            goto test;
        case 2: /* not */
            dst.val = ~dst.val;
            break;
        case 3: /* neg */
            emulate_1op("neg", dst, _regs.eflags);
            break;
        case 4: /* mul */
            src = dst;
            dst.type = OP_REG;
            dst.reg  = (unsigned long *)&_regs.eax;
            dst.val  = *dst.reg;
            _regs.eflags &= ~(EFLG_OF|EFLG_CF);
            switch ( src.bytes )
            {
            case 1:
                dst.val *= src.val;
                if ( (uint8_t)dst.val != (uint16_t)dst.val )
                    _regs.eflags |= EFLG_OF|EFLG_CF;
                break;
            case 2:
                dst.val *= src.val;
                if ( (uint16_t)dst.val != (uint32_t)dst.val )
                    _regs.eflags |= EFLG_OF|EFLG_CF;
                *(uint16_t *)&_regs.edx = dst.val >> 16;
                break;
#ifdef __x86_64__
            case 4:
                dst.val *= src.val;
                if ( (uint32_t)dst.val != dst.val )
                    _regs.eflags |= EFLG_OF|EFLG_CF;
                _regs.edx = (uint32_t)(dst.val >> 32);
                break;
#endif
            default: {
                unsigned long m[2] = { src.val, dst.val };
                if ( mul_dbl(m) )
                    _regs.eflags |= EFLG_OF|EFLG_CF;
                _regs.edx = m[1];
                dst.val  = m[0];
                break;
            }
            }
            break;
        case 5: /* imul */
            src = dst;
            dst.type = OP_REG;
            dst.reg  = (unsigned long *)&_regs.eax;
            dst.val  = *dst.reg;
            _regs.eflags &= ~(EFLG_OF|EFLG_CF);
            switch ( src.bytes )
            {
            case 1:
                dst.val = ((uint16_t)(int8_t)src.val *
                           (uint16_t)(int8_t)dst.val);
                if ( (int8_t)dst.val != (uint16_t)dst.val )
                    _regs.eflags |= EFLG_OF|EFLG_CF;
                break;
            case 2:
                dst.val = ((uint32_t)(int16_t)src.val *
                           (uint32_t)(int16_t)dst.val);
                if ( (int16_t)dst.val != (uint32_t)dst.val )
                    _regs.eflags |= EFLG_OF|EFLG_CF;
                *(uint16_t *)&_regs.edx = dst.val >> 16;
                break;
#ifdef __x86_64__
            case 4:
                dst.val = ((uint64_t)(int32_t)src.val *
                           (uint64_t)(int32_t)dst.val);
                if ( (int32_t)dst.val != dst.val )
                    _regs.eflags |= EFLG_OF|EFLG_CF;
                _regs.edx = (uint32_t)(dst.val >> 32);
                break;
#endif
            default: {
                unsigned long m[2] = { src.val, dst.val };
                if ( imul_dbl(m) )
                    _regs.eflags |= EFLG_OF|EFLG_CF;
                _regs.edx = m[1];
                dst.val  = m[0];
                break;
            }
            }
            break;
        case 6: /* div */ {
            unsigned long u[2], v;
            src = dst;
            dst.type = OP_REG;
            dst.reg  = (unsigned long *)&_regs.eax;
            switch ( src.bytes )
            {
            case 1:
                u[0] = (uint16_t)_regs.eax;
                u[1] = 0;
                v    = (uint8_t)src.val;
                generate_exception_if(
                    div_dbl(u, v) || ((uint8_t)u[0] != (uint16_t)u[0]),
                    EXC_DE);
                dst.val = (uint8_t)u[0];
                ((uint8_t *)&_regs.eax)[1] = u[1];
                break;
            case 2:
                u[0] = ((uint32_t)_regs.edx << 16) | (uint16_t)_regs.eax;
                u[1] = 0;
                v    = (uint16_t)src.val;
                generate_exception_if(
                    div_dbl(u, v) || ((uint16_t)u[0] != (uint32_t)u[0]),
                    EXC_DE);
                dst.val = (uint16_t)u[0];
                *(uint16_t *)&_regs.edx = u[1];
                break;
#ifdef __x86_64__
            case 4:
                u[0] = (_regs.edx << 32) | (uint32_t)_regs.eax;
                u[1] = 0;
                v    = (uint32_t)src.val;
                generate_exception_if(
                    div_dbl(u, v) || ((uint32_t)u[0] != u[0]),
                    EXC_DE);
                dst.val   = (uint32_t)u[0];
                _regs.edx = (uint32_t)u[1];
                break;
#endif
            default:
                u[0] = _regs.eax;
                u[1] = _regs.edx;
                v    = src.val;
                generate_exception_if(div_dbl(u, v), EXC_DE);
                dst.val   = u[0];
                _regs.edx = u[1];
                break;
            }
            break;
        }
        case 7: /* idiv */ {
            unsigned long u[2], v;
            src = dst;
            dst.type = OP_REG;
            dst.reg  = (unsigned long *)&_regs.eax;
            switch ( src.bytes )
            {
            case 1:
                u[0] = (int16_t)_regs.eax;
                u[1] = ((long)u[0] < 0) ? ~0UL : 0UL;
                v    = (int8_t)src.val;
                generate_exception_if(
                    idiv_dbl(u, v) || ((int8_t)u[0] != (int16_t)u[0]),
                    EXC_DE);
                dst.val = (int8_t)u[0];
                ((int8_t *)&_regs.eax)[1] = u[1];
                break;
            case 2:
                u[0] = (int32_t)((_regs.edx << 16) | (uint16_t)_regs.eax);
                u[1] = ((long)u[0] < 0) ? ~0UL : 0UL;
                v    = (int16_t)src.val;
                generate_exception_if(
                    idiv_dbl(u, v) || ((int16_t)u[0] != (int32_t)u[0]),
                    EXC_DE);
                dst.val = (int16_t)u[0];
                *(int16_t *)&_regs.edx = u[1];
                break;
#ifdef __x86_64__
            case 4:
                u[0] = (_regs.edx << 32) | (uint32_t)_regs.eax;
                u[1] = ((long)u[0] < 0) ? ~0UL : 0UL;
                v    = (int32_t)src.val;
                generate_exception_if(
                    idiv_dbl(u, v) || ((int32_t)u[0] != u[0]),
                    EXC_DE);
                dst.val   = (int32_t)u[0];
                _regs.edx = (uint32_t)u[1];
                break;
#endif
            default:
                u[0] = _regs.eax;
                u[1] = _regs.edx;
                v    = src.val;
                generate_exception_if(idiv_dbl(u, v), EXC_DE);
                dst.val   = u[0];
                _regs.edx = u[1];
                break;
            }
            break;
        }
        default:
            goto cannot_emulate;
        }
        break;

    case 0xfe: /* Grp4 */
        generate_exception_if((modrm_reg & 7) >= 2, EXC_UD);
    case 0xff: /* Grp5 */
        switch ( modrm_reg & 7 )
        {
        case 0: /* inc */
            emulate_1op("inc", dst, _regs.eflags);
            break;
        case 1: /* dec */
            emulate_1op("dec", dst, _regs.eflags);
            break;
        case 2: /* call (near) */
        case 4: /* jmp (near) */
            if ( ((op_bytes = dst.bytes) != 8) && mode_64bit() )
            {
                dst.bytes = op_bytes = 8;
                if ( dst.type == OP_REG )
                    dst.val = *dst.reg;
                else if ( (rc = ops->read(dst.mem.seg, dst.mem.off,
                                          &dst.val, 8, ctxt)) != 0 )
                    goto done;
            }
            src.val = _regs.eip;
            _regs.eip = dst.val;
            if ( (modrm_reg & 7) == 2 )
                goto push; /* call */
            break;
        case 3: /* call (far, absolute indirect) */
        case 5: /* jmp (far, absolute indirect) */ {
            unsigned long sel, eip = dst.val;

            if ( (rc = ops->read(dst.mem.seg, dst.mem.off+dst.bytes,
                                 &sel, 2, ctxt)) )
                goto done;

            if ( (modrm_reg & 7) == 3 ) /* call */
            {
                struct segment_register reg;
                fail_if(ops->read_segment == NULL);
                if ( (rc = ops->read_segment(x86_seg_cs, &reg, ctxt)) ||
                     (rc = ops->write(x86_seg_ss, sp_pre_dec(op_bytes),
                                      reg.sel, op_bytes, ctxt)) ||
                     (rc = ops->write(x86_seg_ss, sp_pre_dec(op_bytes),
                                      _regs.eip, op_bytes, ctxt)) )
                    goto done;
            }

            if ( (rc = load_seg(x86_seg_cs, sel, ctxt, ops)) != 0 )
                goto done;
            _regs.eip = eip;

            dst.type = OP_NONE;
            break;
        }
        case 6: /* push */
            /* 64-bit mode: PUSH defaults to a 64-bit operand. */
            if ( mode_64bit() && (dst.bytes == 4) )
            {
                dst.bytes = 8;
                if ( dst.type == OP_REG )
                    dst.val = *dst.reg;
                else if ( (rc = ops->read(dst.mem.seg, dst.mem.off,
                                          &dst.val, 8, ctxt)) != 0 )
                    goto done;
            }
            if ( (rc = ops->write(x86_seg_ss, sp_pre_dec(dst.bytes),
                                  dst.val, dst.bytes, ctxt)) != 0 )
                goto done;
            dst.type = OP_NONE;
            break;
        case 7:
            generate_exception_if(1, EXC_UD);
        default:
            goto cannot_emulate;
        }
        break;
    }

 writeback:
    switch ( dst.type )
    {
    case OP_REG:
        /* The 4-byte case *is* correct: in 64-bit mode we zero-extend. */
        switch ( dst.bytes )
        {
        case 1: *(uint8_t  *)dst.reg = (uint8_t)dst.val; break;
        case 2: *(uint16_t *)dst.reg = (uint16_t)dst.val; break;
        case 4: *dst.reg = (uint32_t)dst.val; break; /* 64b: zero-ext */
        case 8: *dst.reg = dst.val; break;
        }
        break;
    case OP_MEM:
        if ( !(d & Mov) && (dst.orig_val == dst.val) )
            /* nothing to do */;
        else if ( lock_prefix )
            rc = ops->cmpxchg(
                dst.mem.seg, dst.mem.off, dst.orig_val,
                dst.val, dst.bytes, ctxt);
        else
            rc = ops->write(
                dst.mem.seg, dst.mem.off, dst.val, dst.bytes, ctxt);
        if ( rc != 0 )
            goto done;
    default:
        break;
    }

    /* Commit shadow register state. */
    _regs.eflags &= ~EFLG_RF;
    *ctxt->regs = _regs;

    if ( (_regs.eflags & EFLG_TF) &&
         (rc == X86EMUL_OKAY) &&
         (ops->inject_hw_exception != NULL) )
        rc = ops->inject_hw_exception(EXC_DB, ctxt) ? : X86EMUL_EXCEPTION;

 done:
    return rc;

 special_insn:
    dst.type = OP_NONE;

    /*
     * The only implicit-operands instructions allowed a LOCK prefix are
     * CMPXCHG{8,16}B, MOV CRn, MOV DRn.
     */
    generate_exception_if(lock_prefix &&
                          ((b < 0x20) || (b > 0x23)) && /* MOV CRn/DRn */
                          (b != 0xc7),                  /* CMPXCHG{8,16}B */
                          EXC_GP);

    if ( twobyte )
        goto twobyte_special_insn;

    switch ( b )
    {
    case 0x06: /* push %%es */ {
        struct segment_register reg;
        src.val = x86_seg_es;
    push_seg:
        fail_if(ops->read_segment == NULL);
        if ( (rc = ops->read_segment(src.val, &reg, ctxt)) != 0 )
            return rc;
        /* 64-bit mode: PUSH defaults to a 64-bit operand. */
        if ( mode_64bit() && (op_bytes == 4) )
            op_bytes = 8;
        if ( (rc = ops->write(x86_seg_ss, sp_pre_dec(op_bytes),
                              reg.sel, op_bytes, ctxt)) != 0 )
            goto done;
        break;
    }

    case 0x07: /* pop %%es */
        src.val = x86_seg_es;
    pop_seg:
        fail_if(ops->write_segment == NULL);
        /* 64-bit mode: PUSH defaults to a 64-bit operand. */
        if ( mode_64bit() && (op_bytes == 4) )
            op_bytes = 8;
        if ( (rc = ops->read(x86_seg_ss, sp_post_inc(op_bytes),
                             &dst.val, op_bytes, ctxt)) != 0 )
            goto done;
        if ( (rc = load_seg(src.val, (uint16_t)dst.val, ctxt, ops)) != 0 )
            return rc;
        break;

    case 0x0e: /* push %%cs */
        src.val = x86_seg_cs;
        goto push_seg;

    case 0x16: /* push %%ss */
        src.val = x86_seg_ss;
        goto push_seg;

    case 0x17: /* pop %%ss */
        src.val = x86_seg_ss;
        goto pop_seg;

    case 0x1e: /* push %%ds */
        src.val = x86_seg_ds;
        goto push_seg;

    case 0x1f: /* pop %%ds */
        src.val = x86_seg_ds;
        goto pop_seg;

    case 0x27: /* daa */ {
        uint8_t al = _regs.eax;
        unsigned long eflags = _regs.eflags;
        generate_exception_if(mode_64bit(), EXC_UD);
        _regs.eflags &= ~(EFLG_CF|EFLG_AF);
        if ( ((al & 0x0f) > 9) || (eflags & EFLG_AF) )
        {
            *(uint8_t *)&_regs.eax += 6;
            _regs.eflags |= EFLG_AF;
        }
        if ( (al > 0x99) || (eflags & EFLG_CF) )
        {
            *(uint8_t *)&_regs.eax += 0x60;
            _regs.eflags |= EFLG_CF;
        }
        _regs.eflags &= ~(EFLG_SF|EFLG_ZF|EFLG_PF);
        _regs.eflags |= ((uint8_t)_regs.eax == 0) ? EFLG_ZF : 0;
        _regs.eflags |= (( int8_t)_regs.eax <  0) ? EFLG_SF : 0;
        _regs.eflags |= even_parity(_regs.eax) ? EFLG_PF : 0;
        break;
    }

    case 0x2f: /* das */ {
        uint8_t al = _regs.eax;
        unsigned long eflags = _regs.eflags;
        generate_exception_if(mode_64bit(), EXC_UD);
        _regs.eflags &= ~(EFLG_CF|EFLG_AF);
        if ( ((al & 0x0f) > 9) || (eflags & EFLG_AF) )
        {
            _regs.eflags |= EFLG_AF;
            if ( (al < 6) || (eflags & EFLG_CF) )
                _regs.eflags |= EFLG_CF;
            *(uint8_t *)&_regs.eax -= 6;
        }
        if ( (al > 0x99) || (eflags & EFLG_CF) )
        {
            *(uint8_t *)&_regs.eax -= 0x60;
            _regs.eflags |= EFLG_CF;
        }
        _regs.eflags &= ~(EFLG_SF|EFLG_ZF|EFLG_PF);
        _regs.eflags |= ((uint8_t)_regs.eax == 0) ? EFLG_ZF : 0;
        _regs.eflags |= (( int8_t)_regs.eax <  0) ? EFLG_SF : 0;
        _regs.eflags |= even_parity(_regs.eax) ? EFLG_PF : 0;
        break;
    }

    case 0x37: /* aaa */
    case 0x3f: /* aas */
        generate_exception_if(mode_64bit(), EXC_UD);
        _regs.eflags &= ~EFLG_CF;
        if ( ((uint8_t)_regs.eax > 9) || (_regs.eflags & EFLG_AF) )
        {
            ((uint8_t *)&_regs.eax)[0] += (b == 0x37) ? 6 : -6;
            ((uint8_t *)&_regs.eax)[1] += (b == 0x37) ? 1 : -1;
            _regs.eflags |= EFLG_CF | EFLG_AF;
        }
        ((uint8_t *)&_regs.eax)[0] &= 0x0f;
        break;

    case 0x40 ... 0x4f: /* inc/dec reg */
        dst.type  = OP_REG;
        dst.reg   = decode_register(b & 7, &_regs, 0);
        dst.bytes = op_bytes;
        dst.val   = *dst.reg;
        if ( b & 8 )
            emulate_1op("dec", dst, _regs.eflags);
        else
            emulate_1op("inc", dst, _regs.eflags);
        break;

    case 0x50 ... 0x57: /* push reg */
        src.val = *(unsigned long *)decode_register(
            (b & 7) | ((rex_prefix & 1) << 3), &_regs, 0);
        goto push;

    case 0x58 ... 0x5f: /* pop reg */
        dst.type  = OP_REG;
        dst.reg   = decode_register(
            (b & 7) | ((rex_prefix & 1) << 3), &_regs, 0);
        dst.bytes = op_bytes;
        if ( mode_64bit() && (dst.bytes == 4) )
            dst.bytes = 8;
        if ( (rc = ops->read(x86_seg_ss, sp_post_inc(dst.bytes),
                             &dst.val, dst.bytes, ctxt)) != 0 )
            goto done;
        break;

    case 0x60: /* pusha */ {
        int i;
        unsigned long regs[] = {
            _regs.eax, _regs.ecx, _regs.edx, _regs.ebx,
            _regs.esp, _regs.ebp, _regs.esi, _regs.edi };
        generate_exception_if(mode_64bit(), EXC_UD);
        for ( i = 0; i < 8; i++ )
            if ( (rc = ops->write(x86_seg_ss, sp_pre_dec(op_bytes),
                                  regs[i], op_bytes, ctxt)) != 0 )
            goto done;
        break;
    }

    case 0x61: /* popa */ {
        int i;
        unsigned long dummy_esp, *regs[] = {
            (unsigned long *)&_regs.edi, (unsigned long *)&_regs.esi,
            (unsigned long *)&_regs.ebp, (unsigned long *)&dummy_esp,
            (unsigned long *)&_regs.ebx, (unsigned long *)&_regs.edx,
            (unsigned long *)&_regs.ecx, (unsigned long *)&_regs.eax };
        generate_exception_if(mode_64bit(), EXC_UD);
        for ( i = 0; i < 8; i++ )
            if ( (rc = ops->read(x86_seg_ss, sp_post_inc(op_bytes),
                                 regs[i], op_bytes, ctxt)) != 0 )
            goto done;
        break;
    }

    case 0x68: /* push imm{16,32,64} */
        src.val = ((op_bytes == 2)
                   ? (int32_t)insn_fetch_type(int16_t)
                   : insn_fetch_type(int32_t));
        goto push;

    case 0x6a: /* push imm8 */
        src.val = insn_fetch_type(int8_t);
    push:
        d |= Mov; /* force writeback */
        dst.type  = OP_MEM;
        dst.bytes = op_bytes;
        if ( mode_64bit() && (dst.bytes == 4) )
            dst.bytes = 8;
        dst.val = src.val;
        dst.mem.seg = x86_seg_ss;
        dst.mem.off = sp_pre_dec(dst.bytes);
        break;

    case 0x6c ... 0x6d: /* ins %dx,%es:%edi */
        handle_rep_prefix();
        generate_exception_if(!mode_iopl(), EXC_GP);
        dst.type  = OP_MEM;
        dst.bytes = !(b & 1) ? 1 : (op_bytes == 8) ? 4 : op_bytes;
        dst.mem.seg = x86_seg_es;
        dst.mem.off = truncate_ea(_regs.edi);
        fail_if(ops->read_io == NULL);
        if ( (rc = ops->read_io((uint16_t)_regs.edx, dst.bytes,
                                &dst.val, ctxt)) != 0 )
            goto done;
        register_address_increment(
            _regs.edi, (_regs.eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
        break;

    case 0x6e ... 0x6f: /* outs %esi,%dx */
        handle_rep_prefix();
        generate_exception_if(!mode_iopl(), EXC_GP);
        dst.bytes = !(b & 1) ? 1 : (op_bytes == 8) ? 4 : op_bytes;
        if ( (rc = ops->read(ea.mem.seg, truncate_ea(_regs.esi),
                             &dst.val, dst.bytes, ctxt)) != 0 )
            goto done;
        fail_if(ops->write_io == NULL);
        if ( (rc = ops->write_io((uint16_t)_regs.edx, dst.bytes,
                                 dst.val, ctxt)) != 0 )
            goto done;
        register_address_increment(
            _regs.esi, (_regs.eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
        break;

    case 0x70 ... 0x7f: /* jcc (short) */ {
        int rel = insn_fetch_type(int8_t);
        if ( test_cc(b, _regs.eflags) )
            jmp_rel(rel);
        break;
    }

    case 0x90: /* nop / xchg %%r8,%%rax */
        if ( !(rex_prefix & 1) )
            break; /* nop */

    case 0x91 ... 0x97: /* xchg reg,%%rax */
        src.type = dst.type = OP_REG;
        src.bytes = dst.bytes = op_bytes;
        src.reg  = (unsigned long *)&_regs.eax;
        src.val  = *src.reg;
        dst.reg  = decode_register(
            (b & 7) | ((rex_prefix & 1) << 3), &_regs, 0);
        dst.val  = *dst.reg;
        goto xchg;

    case 0x98: /* cbw/cwde/cdqe */
        switch ( op_bytes )
        {
        case 2: *(int16_t *)&_regs.eax = (int8_t)_regs.eax; break; /* cbw */
        case 4: _regs.eax = (uint32_t)(int16_t)_regs.eax; break; /* cwde */
        case 8: _regs.eax = (int32_t)_regs.eax; break; /* cdqe */
        }
        break;

    case 0x99: /* cwd/cdq/cqo */
        switch ( op_bytes )
        {
        case 2:
            *(int16_t *)&_regs.edx = ((int16_t)_regs.eax < 0) ? -1 : 0;
            break;
        case 4:
            _regs.edx = (uint32_t)(((int32_t)_regs.eax < 0) ? -1 : 0);
            break;
        case 8:
            _regs.edx = (_regs.eax < 0) ? -1 : 0;
            break;
        }
        break;

    case 0x9a: /* call (far, absolute) */ {
        struct segment_register reg;
        uint16_t sel;
        uint32_t eip;

        fail_if(ops->read_segment == NULL);
        generate_exception_if(mode_64bit(), EXC_UD);

        eip = insn_fetch_bytes(op_bytes);
        sel = insn_fetch_type(uint16_t);

        if ( (rc = ops->read_segment(x86_seg_cs, &reg, ctxt)) ||
             (rc = ops->write(x86_seg_ss, sp_pre_dec(op_bytes),
                              reg.sel, op_bytes, ctxt)) ||
             (rc = ops->write(x86_seg_ss, sp_pre_dec(op_bytes),
                              _regs.eip, op_bytes, ctxt)) )
            goto done;

        if ( (rc = load_seg(x86_seg_cs, sel, ctxt, ops)) != 0 )
            goto done;
        _regs.eip = eip;
        break;
    }

    case 0x9c: /* pushf */
        src.val = _regs.eflags;
        goto push;

    case 0x9d: /* popf */ {
        uint32_t mask = EFLG_VIP | EFLG_VIF | EFLG_VM;
        if ( !mode_iopl() )
            mask |= EFLG_IOPL;
        fail_if(ops->write_rflags == NULL);
        /* 64-bit mode: POP defaults to a 64-bit operand. */
        if ( mode_64bit() && (op_bytes == 4) )
            op_bytes = 8;
        if ( (rc = ops->read(x86_seg_ss, sp_post_inc(op_bytes),
                             &dst.val, op_bytes, ctxt)) != 0 )
            goto done;
        if ( op_bytes == 2 )
            dst.val = (uint16_t)dst.val | (_regs.eflags & 0xffff0000u);
        dst.val &= 0x257fd5;
        _regs.eflags &= mask;
        _regs.eflags |= (uint32_t)(dst.val & ~mask) | 0x02;
        if ( (rc = ops->write_rflags(_regs.eflags, ctxt)) != 0 )
            goto done;
        break;
    }

    case 0x9e: /* sahf */
        *(uint8_t *)_regs.eflags = (((uint8_t *)&_regs.eax)[1] & 0xd7) | 0x02;
        break;

    case 0x9f: /* lahf */
        ((uint8_t *)&_regs.eax)[1] = (_regs.eflags & 0xd7) | 0x02;
        break;

    case 0xa0 ... 0xa1: /* mov mem.offs,{%al,%ax,%eax,%rax} */
        /* Source EA is not encoded via ModRM. */
        dst.type  = OP_REG;
        dst.reg   = (unsigned long *)&_regs.eax;
        dst.bytes = (d & ByteOp) ? 1 : op_bytes;
        if ( (rc = ops->read(ea.mem.seg, insn_fetch_bytes(ad_bytes),
                             &dst.val, dst.bytes, ctxt)) != 0 )
            goto done;
        break;

    case 0xa2 ... 0xa3: /* mov {%al,%ax,%eax,%rax},mem.offs */
        /* Destination EA is not encoded via ModRM. */
        dst.type  = OP_MEM;
        dst.mem.seg = ea.mem.seg;
        dst.mem.off = insn_fetch_bytes(ad_bytes);
        dst.bytes = (d & ByteOp) ? 1 : op_bytes;
        dst.val   = (unsigned long)_regs.eax;
        break;

    case 0xa4 ... 0xa5: /* movs */
        handle_rep_prefix();
        dst.type  = OP_MEM;
        dst.bytes = (d & ByteOp) ? 1 : op_bytes;
        dst.mem.seg = x86_seg_es;
        dst.mem.off = truncate_ea(_regs.edi);
        if ( (rc = ops->read(ea.mem.seg, truncate_ea(_regs.esi),
                             &dst.val, dst.bytes, ctxt)) != 0 )
            goto done;
        register_address_increment(
            _regs.esi, (_regs.eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
        register_address_increment(
            _regs.edi, (_regs.eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
        break;

    case 0xa6 ... 0xa7: /* cmps */ {
        unsigned long next_eip = _regs.eip;
        handle_rep_prefix();
        src.bytes = dst.bytes = (d & ByteOp) ? 1 : op_bytes;
        if ( (rc = ops->read(ea.mem.seg, truncate_ea(_regs.esi),
                             &dst.val, dst.bytes, ctxt)) ||
             (rc = ops->read(x86_seg_es, truncate_ea(_regs.edi),
                             &src.val, src.bytes, ctxt)) )
            goto done;
        register_address_increment(
            _regs.esi, (_regs.eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
        register_address_increment(
            _regs.edi, (_regs.eflags & EFLG_DF) ? -src.bytes : src.bytes);
        /* cmp: dst - src ==> src=*%%edi,dst=*%%esi ==> *%%esi - *%%edi */
        emulate_2op_SrcV("cmp", src, dst, _regs.eflags);
        if ( ((rep_prefix == REPE_PREFIX) && !(_regs.eflags & EFLG_ZF)) ||
             ((rep_prefix == REPNE_PREFIX) && (_regs.eflags & EFLG_ZF)) )
            _regs.eip = next_eip;
        break;
    }

    case 0xaa ... 0xab: /* stos */
        handle_rep_prefix();
        dst.type  = OP_MEM;
        dst.bytes = (d & ByteOp) ? 1 : op_bytes;
        dst.mem.seg = x86_seg_es;
        dst.mem.off = truncate_ea(_regs.edi);
        dst.val   = _regs.eax;
        register_address_increment(
            _regs.edi, (_regs.eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
        break;

    case 0xac ... 0xad: /* lods */
        handle_rep_prefix();
        dst.type  = OP_REG;
        dst.bytes = (d & ByteOp) ? 1 : op_bytes;
        dst.reg   = (unsigned long *)&_regs.eax;
        if ( (rc = ops->read(ea.mem.seg, truncate_ea(_regs.esi),
                             &dst.val, dst.bytes, ctxt)) != 0 )
            goto done;
        register_address_increment(
            _regs.esi, (_regs.eflags & EFLG_DF) ? -dst.bytes : dst.bytes);
        break;

    case 0xae ... 0xaf: /* scas */ {
        unsigned long next_eip = _regs.eip;
        handle_rep_prefix();
        src.bytes = dst.bytes = (d & ByteOp) ? 1 : op_bytes;
        dst.val = _regs.eax;
        if ( (rc = ops->read(x86_seg_es, truncate_ea(_regs.edi),
                             &src.val, src.bytes, ctxt)) != 0 )
            goto done;
        register_address_increment(
            _regs.edi, (_regs.eflags & EFLG_DF) ? -src.bytes : src.bytes);
        /* cmp: dst - src ==> src=*%%edi,dst=%%eax ==> %%eax - *%%edi */
        emulate_2op_SrcV("cmp", src, dst, _regs.eflags);
        if ( ((rep_prefix == REPE_PREFIX) && !(_regs.eflags & EFLG_ZF)) ||
             ((rep_prefix == REPNE_PREFIX) && (_regs.eflags & EFLG_ZF)) )
            _regs.eip = next_eip;
        break;
    }

    case 0xc2: /* ret imm16 (near) */
    case 0xc3: /* ret (near) */ {
        int offset = (b == 0xc2) ? insn_fetch_type(uint16_t) : 0;
        op_bytes = mode_64bit() ? 8 : op_bytes;
        if ( (rc = ops->read(x86_seg_ss, sp_post_inc(op_bytes + offset),
                             &dst.val, op_bytes, ctxt)) != 0 )
            goto done;
        _regs.eip = dst.val;
        break;
    }

    case 0xc8: /* enter imm16,imm8 */ {
        uint16_t size = insn_fetch_type(uint16_t);
        uint8_t depth = insn_fetch_type(uint8_t) & 31;
        int i;

        dst.type = OP_REG;
        dst.bytes = (mode_64bit() && (op_bytes == 4)) ? 8 : op_bytes;
        dst.reg = (unsigned long *)&_regs.ebp;
        if ( (rc = ops->write(x86_seg_ss, sp_pre_dec(dst.bytes),
                              _regs.ebp, dst.bytes, ctxt)) )
            goto done;
        dst.val = _regs.esp;

        if ( depth > 0 )
        {
            for ( i = 1; i < depth; i++ )
            {
                unsigned long ebp, temp_data;
                ebp = _truncate_ea(_regs.ebp - i*dst.bytes, ctxt->sp_size/8);
                if ( (rc = ops->read(x86_seg_ss, ebp,
                                     &temp_data, dst.bytes, ctxt)) ||
                     (rc = ops->write(x86_seg_ss, sp_pre_dec(dst.bytes),
                                      temp_data, dst.bytes, ctxt)) )
                    goto done;
            }
            if ( (rc = ops->write(x86_seg_ss, sp_pre_dec(dst.bytes),
                                  dst.val, dst.bytes, ctxt)) )
                goto done;
        }

        sp_pre_dec(size);
        break;
    }

    case 0xc9: /* leave */
        /* First writeback, to %%esp. */
        dst.type = OP_REG;
        dst.bytes = (mode_64bit() && (op_bytes == 4)) ? 8 : op_bytes;
        dst.reg = (unsigned long *)&_regs.esp;
        dst.val = _regs.ebp;

        /* Flush first writeback, since there is a second. */
        switch ( dst.bytes )
        {
        case 1: *(uint8_t  *)dst.reg = (uint8_t)dst.val; break;
        case 2: *(uint16_t *)dst.reg = (uint16_t)dst.val; break;
        case 4: *dst.reg = (uint32_t)dst.val; break; /* 64b: zero-ext */
        case 8: *dst.reg = dst.val; break;
        }

        /* Second writeback, to %%ebp. */
        dst.reg = (unsigned long *)&_regs.ebp;
        if ( (rc = ops->read(x86_seg_ss, sp_post_inc(dst.bytes),
                             &dst.val, dst.bytes, ctxt)) )
            goto done;
        break;

    case 0xca: /* ret imm16 (far) */
    case 0xcb: /* ret (far) */ {
        int offset = (b == 0xca) ? insn_fetch_type(uint16_t) : 0;
        op_bytes = mode_64bit() ? 8 : op_bytes;
        if ( (rc = ops->read(x86_seg_ss, sp_post_inc(op_bytes),
                             &dst.val, op_bytes, ctxt)) || 
             (rc = ops->read(x86_seg_ss, sp_post_inc(op_bytes + offset),
                             &src.val, op_bytes, ctxt)) ||
             (rc = load_seg(x86_seg_cs, (uint16_t)src.val, ctxt, ops)) )
            goto done;
        _regs.eip = dst.val;
        break;
    }

    case 0xcc: /* int3 */
        src.val = EXC_BP;
        goto swint;

    case 0xcd: /* int imm8 */
        src.val = insn_fetch_type(uint8_t);
    swint:
        fail_if(ops->inject_sw_interrupt == NULL);
        rc = ops->inject_sw_interrupt(src.val, _regs.eip - ctxt->regs->eip,
                                      ctxt) ? : X86EMUL_EXCEPTION;
        goto done;

    case 0xce: /* into */
        generate_exception_if(mode_64bit(), EXC_UD);
        if ( !(_regs.eflags & EFLG_OF) )
            break;
        src.val = EXC_OF;
        goto swint;

    case 0xcf: /* iret */ {
        unsigned long cs, eip, eflags;
        uint32_t mask = EFLG_VIP | EFLG_VIF | EFLG_VM;
        if ( !mode_iopl() )
            mask |= EFLG_IOPL;
        fail_if(!in_realmode(ctxt, ops));
        fail_if(ops->write_rflags == NULL);
        if ( (rc = ops->read(x86_seg_ss, sp_post_inc(op_bytes),
                             &eip, op_bytes, ctxt)) ||
             (rc = ops->read(x86_seg_ss, sp_post_inc(op_bytes),
                             &cs, op_bytes, ctxt)) ||
             (rc = ops->read(x86_seg_ss, sp_post_inc(op_bytes),
                             &eflags, op_bytes, ctxt)) )
            goto done;
        if ( op_bytes == 2 )
            eflags = (uint16_t)eflags | (_regs.eflags & 0xffff0000u);
        eflags &= 0x257fd5;
        _regs.eflags &= mask;
        _regs.eflags |= (uint32_t)(eflags & ~mask) | 0x02;
        if ( (rc = ops->write_rflags(_regs.eflags, ctxt)) != 0 )
            goto done;
        _regs.eip = eip;
        if ( (rc = load_seg(x86_seg_cs, (uint16_t)cs, ctxt, ops)) != 0 )
            goto done;
        break;
    }

    case 0xd4: /* aam */ {
        unsigned int base = insn_fetch_type(uint8_t);
        uint8_t al = _regs.eax;
        generate_exception_if(mode_64bit(), EXC_UD);
        generate_exception_if(base == 0, EXC_DE);
        *(uint16_t *)&_regs.eax = ((al / base) << 8) | (al % base);
        _regs.eflags &= ~(EFLG_SF|EFLG_ZF|EFLG_PF);
        _regs.eflags |= ((uint8_t)_regs.eax == 0) ? EFLG_ZF : 0;
        _regs.eflags |= (( int8_t)_regs.eax <  0) ? EFLG_SF : 0;
        _regs.eflags |= even_parity(_regs.eax) ? EFLG_PF : 0;
        break;
    }

    case 0xd5: /* aad */ {
        unsigned int base = insn_fetch_type(uint8_t);
        uint16_t ax = _regs.eax;
        generate_exception_if(mode_64bit(), EXC_UD);
        *(uint16_t *)&_regs.eax = (uint8_t)(ax + ((ax >> 8) * base));
        _regs.eflags &= ~(EFLG_SF|EFLG_ZF|EFLG_PF);
        _regs.eflags |= ((uint8_t)_regs.eax == 0) ? EFLG_ZF : 0;
        _regs.eflags |= (( int8_t)_regs.eax <  0) ? EFLG_SF : 0;
        _regs.eflags |= even_parity(_regs.eax) ? EFLG_PF : 0;
        break;
    }

    case 0xd6: /* salc */
        generate_exception_if(mode_64bit(), EXC_UD);
        *(uint8_t *)&_regs.eax = (_regs.eflags & EFLG_CF) ? 0xff : 0x00;
        break;

    case 0xd7: /* xlat */ {
        unsigned long al = (uint8_t)_regs.eax;
        if ( (rc = ops->read(ea.mem.seg, truncate_ea(_regs.ebx + al),
                             &al, 1, ctxt)) != 0 )
            goto done;
        *(uint8_t *)&_regs.eax = al;
        break;
    }

    case 0xe0 ... 0xe2: /* loop{,z,nz} */ {
        int rel = insn_fetch_type(int8_t);
        int do_jmp = !(_regs.eflags & EFLG_ZF); /* loopnz */
        if ( b == 0xe1 )
            do_jmp = !do_jmp; /* loopz */
        else if ( b == 0xe2 )
            do_jmp = 1; /* loop */
        switch ( ad_bytes )
        {
        case 2:
            do_jmp &= --(*(uint16_t *)&_regs.ecx) != 0;
            break;
        case 4:
            do_jmp &= --(*(uint32_t *)&_regs.ecx) != 0;
            _regs.ecx = (uint32_t)_regs.ecx; /* zero extend in x86/64 mode */
            break;
        default: /* case 8: */
            do_jmp &= --_regs.ecx != 0;
            break;
        }
        if ( do_jmp )
            jmp_rel(rel);
        break;
    }

    case 0xe3: /* jcxz/jecxz (short) */ {
        int rel = insn_fetch_type(int8_t);
        if ( (ad_bytes == 2) ? !(uint16_t)_regs.ecx :
             (ad_bytes == 4) ? !(uint32_t)_regs.ecx : !_regs.ecx )
            jmp_rel(rel);
        break;
    }

    case 0xe4: /* in imm8,%al */
    case 0xe5: /* in imm8,%eax */
    case 0xe6: /* out %al,imm8 */
    case 0xe7: /* out %eax,imm8 */
    case 0xec: /* in %dx,%al */
    case 0xed: /* in %dx,%eax */
    case 0xee: /* out %al,%dx */
    case 0xef: /* out %eax,%dx */ {
        unsigned int port = ((b < 0xe8)
                             ? insn_fetch_type(uint8_t)
                             : (uint16_t)_regs.edx);
        generate_exception_if(!mode_iopl(), EXC_GP);
        op_bytes = !(b & 1) ? 1 : (op_bytes == 8) ? 4 : op_bytes;
        if ( b & 2 )
        {
            /* out */
            fail_if(ops->write_io == NULL);
            rc = ops->write_io(port, op_bytes, _regs.eax, ctxt);
            
        }
        else
        {
            /* in */
            dst.type  = OP_REG;
            dst.bytes = op_bytes;
            dst.reg   = (unsigned long *)&_regs.eax;
            fail_if(ops->read_io == NULL);
            rc = ops->read_io(port, dst.bytes, &dst.val, ctxt);
        }
        if ( rc != 0 )
            goto done;
        break;
    }

    case 0xe8: /* call (near) */ {
        int rel = (((op_bytes == 2) && !mode_64bit())
                   ? (int32_t)insn_fetch_type(int16_t)
                   : insn_fetch_type(int32_t));
        op_bytes = mode_64bit() ? 8 : op_bytes;
        src.val = _regs.eip;
        jmp_rel(rel);
        goto push;
    }

    case 0xe9: /* jmp (near) */ {
        int rel = (((op_bytes == 2) && !mode_64bit())
                   ? (int32_t)insn_fetch_type(int16_t)
                   : insn_fetch_type(int32_t));
        jmp_rel(rel);
        break;
    }

    case 0xea: /* jmp (far, absolute) */ {
        uint16_t sel;
        uint32_t eip;
        generate_exception_if(mode_64bit(), EXC_UD);
        eip = insn_fetch_bytes(op_bytes);
        sel = insn_fetch_type(uint16_t);
        if ( (rc = load_seg(x86_seg_cs, sel, ctxt, ops)) != 0 )
            goto done;
        _regs.eip = eip;
        break;
    }

    case 0xeb: /* jmp (short) */
        jmp_rel(insn_fetch_type(int8_t));
        break;

    case 0xf1: /* int1 (icebp) */
        src.val = EXC_DB;
        goto swint;

    case 0xf4: /* hlt */
        fail_if(ops->hlt == NULL);
        if ( (rc = ops->hlt(ctxt)) != 0 )
            goto done;
        break;

    case 0xf5: /* cmc */
        _regs.eflags ^= EFLG_CF;
        break;

    case 0xf8: /* clc */
        _regs.eflags &= ~EFLG_CF;
        break;

    case 0xf9: /* stc */
        _regs.eflags |= EFLG_CF;
        break;

    case 0xfa: /* cli */
    case 0xfb: /* sti */
        generate_exception_if(!mode_iopl(), EXC_GP);
        fail_if(ops->write_rflags == NULL);
        _regs.eflags &= ~EFLG_IF;
        if ( b == 0xfb ) /* sti */
            _regs.eflags |= EFLG_IF;
        if ( (rc = ops->write_rflags(_regs.eflags, ctxt)) != 0 )
            goto done;
        break;

    case 0xfc: /* cld */
        _regs.eflags &= ~EFLG_DF;
        break;

    case 0xfd: /* std */
        _regs.eflags |= EFLG_DF;
        break;
    }
    goto writeback;

 twobyte_insn:
    switch ( b )
    {
    case 0x40 ... 0x4f: /* cmovcc */
        dst.val = src.val;
        if ( !test_cc(b, _regs.eflags) )
            dst.type = OP_NONE;
        break;

    case 0x90 ... 0x9f: /* setcc */
        dst.val = test_cc(b, _regs.eflags);
        break;

    case 0xb0 ... 0xb1: /* cmpxchg */
        /* Save real source value, then compare EAX against destination. */
        src.orig_val = src.val;
        src.val = _regs.eax;
        emulate_2op_SrcV("cmp", src, dst, _regs.eflags);
        /* Always write back. The question is: where to? */
        d |= Mov;
        if ( _regs.eflags & EFLG_ZF )
        {
            /* Success: write back to memory. */
            dst.val = src.orig_val;
        }
        else
        {
            /* Failure: write the value we saw to EAX. */
            dst.type = OP_REG;
            dst.reg  = (unsigned long *)&_regs.eax;
        }
        break;

    case 0xa3: bt: /* bt */
        emulate_2op_SrcV_nobyte("bt", src, dst, _regs.eflags);
        break;

    case 0xb3: btr: /* btr */
        emulate_2op_SrcV_nobyte("btr", src, dst, _regs.eflags);
        break;

    case 0xab: bts: /* bts */
        emulate_2op_SrcV_nobyte("bts", src, dst, _regs.eflags);
        break;

    case 0xaf: /* imul */
        _regs.eflags &= ~(EFLG_OF|EFLG_CF);
        switch ( dst.bytes )
        {
        case 2:
            dst.val = ((uint32_t)(int16_t)src.val *
                       (uint32_t)(int16_t)dst.val);
            if ( (int16_t)dst.val != (uint32_t)dst.val )
                _regs.eflags |= EFLG_OF|EFLG_CF;
            break;
#ifdef __x86_64__
        case 4:
            dst.val = ((uint64_t)(int32_t)src.val *
                       (uint64_t)(int32_t)dst.val);
            if ( (int32_t)dst.val != dst.val )
                _regs.eflags |= EFLG_OF|EFLG_CF;
            break;
#endif
        default: {
            unsigned long m[2] = { src.val, dst.val };
            if ( imul_dbl(m) )
                _regs.eflags |= EFLG_OF|EFLG_CF;
            dst.val = m[0];
            break;
        }
        }
        break;

    case 0xb2: /* lss */
        dst.val = x86_seg_ss;
        goto les;

    case 0xb4: /* lfs */
        dst.val = x86_seg_fs;
        goto les;

    case 0xb5: /* lgs */
        dst.val = x86_seg_gs;
        goto les;

    case 0xb6: /* movzx rm8,r{16,32,64} */
        /* Recompute DstReg as we may have decoded AH/BH/CH/DH. */
        dst.reg   = decode_register(modrm_reg, &_regs, 0);
        dst.bytes = op_bytes;
        dst.val   = (uint8_t)src.val;
        break;

    case 0xbc: /* bsf */ {
        int zf;
        asm ( "bsf %2,%0; setz %b1"
              : "=r" (dst.val), "=q" (zf)
              : "r" (src.val), "1" (0) );
        _regs.eflags &= ~EFLG_ZF;
        _regs.eflags |= zf ? EFLG_ZF : 0;
        break;
    }

    case 0xbd: /* bsr */ {
        int zf;
        asm ( "bsr %2,%0; setz %b1"
              : "=r" (dst.val), "=q" (zf)
              : "r" (src.val), "1" (0) );
        _regs.eflags &= ~EFLG_ZF;
        _regs.eflags |= zf ? EFLG_ZF : 0;
        break;
    }

    case 0xb7: /* movzx rm16,r{16,32,64} */
        dst.val = (uint16_t)src.val;
        break;

    case 0xbb: btc: /* btc */
        emulate_2op_SrcV_nobyte("btc", src, dst, _regs.eflags);
        break;

    case 0xba: /* Grp8 */
        switch ( modrm_reg & 7 )
        {
        case 4: goto bt;
        case 5: goto bts;
        case 6: goto btr;
        case 7: goto btc;
        default: generate_exception_if(1, EXC_UD);
        }
        break;

    case 0xbe: /* movsx rm8,r{16,32,64} */
        /* Recompute DstReg as we may have decoded AH/BH/CH/DH. */
        dst.reg   = decode_register(modrm_reg, &_regs, 0);
        dst.bytes = op_bytes;
        dst.val   = (int8_t)src.val;
        break;

    case 0xbf: /* movsx rm16,r{16,32,64} */
        dst.val = (int16_t)src.val;
        break;

    case 0xc0 ... 0xc1: /* xadd */
        /* Write back the register source. */
        switch ( dst.bytes )
        {
        case 1: *(uint8_t  *)src.reg = (uint8_t)dst.val; break;
        case 2: *(uint16_t *)src.reg = (uint16_t)dst.val; break;
        case 4: *src.reg = (uint32_t)dst.val; break; /* 64b reg: zero-extend */
        case 8: *src.reg = dst.val; break;
        }
        goto add;
    }
    goto writeback;

 twobyte_special_insn:
    switch ( b )
    {
    case 0x01: /* Grp7 */ {
        struct segment_register reg;
        unsigned long base, limit, cr0, cr0w;

        switch ( modrm_reg & 7 )
        {
        case 0: /* sgdt */
        case 1: /* sidt */
            generate_exception_if(ea.type != OP_MEM, EXC_UD);
            fail_if(ops->read_segment == NULL);
            if ( (rc = ops->read_segment((modrm_reg & 1) ?
                                         x86_seg_idtr : x86_seg_gdtr,
                                         &reg, ctxt)) )
                goto done;
            if ( op_bytes == 2 )
                reg.base &= 0xffffff;
            if ( (rc = ops->write(ea.mem.seg, ea.mem.off+0,
                                  reg.limit, 2, ctxt)) ||
                 (rc = ops->write(ea.mem.seg, ea.mem.off+2,
                                  reg.base, mode_64bit() ? 8 : 4, ctxt)) )
                goto done;
            break;
        case 2: /* lgdt */
        case 3: /* lidt */
            generate_exception_if(ea.type != OP_MEM, EXC_UD);
            fail_if(ops->write_segment == NULL);
            memset(&reg, 0, sizeof(reg));
            if ( (rc = ops->read(ea.mem.seg, ea.mem.off+0,
                                 &limit, 2, ctxt)) ||
                 (rc = ops->read(ea.mem.seg, ea.mem.off+2,
                                 &base, mode_64bit() ? 8 : 4, ctxt)) )
                goto done;
            reg.base = base;
            reg.limit = limit;
            if ( op_bytes == 2 )
                reg.base &= 0xffffff;
            if ( (rc = ops->write_segment((modrm_reg & 1) ?
                                          x86_seg_idtr : x86_seg_gdtr,
                                          &reg, ctxt)) )
                goto done;
            break;
        case 4: /* smsw */
            ea.bytes = 2;
            dst = ea;
            fail_if(ops->read_cr == NULL);
            if ( (rc = ops->read_cr(0, &dst.val, ctxt)) )
                goto done;
            d |= Mov; /* force writeback */
            break;
        case 6: /* lmsw */
            fail_if(ops->read_cr == NULL);
            fail_if(ops->write_cr == NULL);
            if ( (rc = ops->read_cr(0, &cr0, ctxt)) )
                goto done;
            if ( ea.type == OP_REG )
                cr0w = *ea.reg;
            else if ( (rc = ops->read(ea.mem.seg, ea.mem.off,
                                      &cr0w, 2, ctxt)) )
                goto done;
            cr0 &= 0xffff0000;
            cr0 |= (uint16_t)cr0w;
            if ( (rc = ops->write_cr(0, cr0, ctxt)) )
                goto done;
            break;
        default:
            goto cannot_emulate;
        }
        break;
    }

    case 0x06: /* clts */
        generate_exception_if(!mode_ring0(), EXC_GP);
        fail_if((ops->read_cr == NULL) || (ops->write_cr == NULL));
        if ( (rc = ops->read_cr(0, &dst.val, ctxt)) ||
             (rc = ops->write_cr(0, dst.val&~8, ctxt)) )
            goto done;
        break;

    case 0x08: /* invd */
    case 0x09: /* wbinvd */
        generate_exception_if(!mode_ring0(), EXC_GP);
        fail_if(ops->wbinvd == NULL);
        if ( (rc = ops->wbinvd(ctxt)) != 0 )
            goto done;
        break;

    case 0x0d: /* GrpP (prefetch) */
    case 0x18: /* Grp16 (prefetch/nop) */
    case 0x19 ... 0x1f: /* nop (amd-defined) */
        break;

    case 0x20: /* mov cr,reg */
    case 0x21: /* mov dr,reg */
    case 0x22: /* mov reg,cr */
    case 0x23: /* mov reg,dr */
        generate_exception_if(!mode_ring0(), EXC_GP);
        modrm_rm  |= (rex_prefix & 1) << 3;
        modrm_reg |= lock_prefix << 3;
        if ( b & 2 )
        {
            /* Write to CR/DR. */
            src.val = *(unsigned long *)decode_register(modrm_rm, &_regs, 0);
            if ( !mode_64bit() )
                src.val = (uint32_t)src.val;
            rc = ((b & 1)
                  ? (ops->write_dr
                     ? ops->write_dr(modrm_reg, src.val, ctxt)
                     : X86EMUL_UNHANDLEABLE)
                  : (ops->write_cr
                     ? ops->write_cr(modrm_reg, src.val, ctxt)
                     : X86EMUL_UNHANDLEABLE));
        }
        else
        {
            /* Read from CR/DR. */
            dst.type  = OP_REG;
            dst.bytes = mode_64bit() ? 8 : 4;
            dst.reg   = decode_register(modrm_rm, &_regs, 0);
            rc = ((b & 1)
                  ? (ops->read_dr
                     ? ops->read_dr(modrm_reg, &dst.val, ctxt)
                     : X86EMUL_UNHANDLEABLE)
                  : (ops->read_cr
                     ? ops->read_cr(modrm_reg, &dst.val, ctxt)
                     : X86EMUL_UNHANDLEABLE));
        }
        if ( rc != 0 )
            goto done;
        break;

    case 0x30: /* wrmsr */ {
        uint64_t val = ((uint64_t)_regs.edx << 32) | (uint32_t)_regs.eax;
        generate_exception_if(!mode_ring0(), EXC_GP);
        fail_if(ops->write_msr == NULL);
        if ( (rc = ops->write_msr((uint32_t)_regs.ecx, val, ctxt)) != 0 )
            goto done;
        break;
    }

    case 0x32: /* rdmsr */ {
        uint64_t val;
        generate_exception_if(!mode_ring0(), EXC_GP);
        fail_if(ops->read_msr == NULL);
        if ( (rc = ops->read_msr((uint32_t)_regs.ecx, &val, ctxt)) != 0 )
            goto done;
        _regs.edx = (uint32_t)(val >> 32);
        _regs.eax = (uint32_t)(val >>  0);
        break;
    }

    case 0x80 ... 0x8f: /* jcc (near) */ {
        int rel = (((op_bytes == 2) && !mode_64bit())
                   ? (int32_t)insn_fetch_type(int16_t)
                   : insn_fetch_type(int32_t));
        if ( test_cc(b, _regs.eflags) )
            jmp_rel(rel);
        break;
    }

    case 0xa0: /* push %%fs */
        src.val = x86_seg_fs;
        goto push_seg;

    case 0xa1: /* pop %%fs */
        src.val = x86_seg_fs;
        goto pop_seg;

    case 0xa2: /* cpuid */ {
        unsigned int eax = _regs.eax, ebx = _regs.ebx;
        unsigned int ecx = _regs.ecx, edx = _regs.edx;
        fail_if(ops->cpuid == NULL);
        if ( (rc = ops->cpuid(&eax, &ebx, &ecx, &edx, ctxt)) != 0 )
            goto done;
        _regs.eax = eax; _regs.ebx = ebx;
        _regs.ecx = ecx; _regs.edx = edx;
        break;
    }

    case 0xa8: /* push %%gs */
        src.val = x86_seg_gs;
        goto push_seg;

    case 0xa9: /* pop %%gs */
        src.val = x86_seg_gs;
        goto pop_seg;

    case 0xc7: /* Grp9 (cmpxchg8b) */
#if defined(__i386__)
    {
        unsigned long old_lo, old_hi;
        generate_exception_if((modrm_reg & 7) != 1, EXC_UD);
        if ( (rc = ops->read(ea.mem.seg, ea.mem.off+0, &old_lo, 4, ctxt)) ||
             (rc = ops->read(ea.mem.seg, ea.mem.off+4, &old_hi, 4, ctxt)) )
            goto done;
        if ( (old_lo != _regs.eax) || (old_hi != _regs.edx) )
        {
            _regs.eax = old_lo;
            _regs.edx = old_hi;
            _regs.eflags &= ~EFLG_ZF;
        }
        else if ( ops->cmpxchg8b == NULL )
        {
            rc = X86EMUL_UNHANDLEABLE;
            goto done;
        }
        else
        {
            if ( (rc = ops->cmpxchg8b(ea.mem.seg, ea.mem.off, old_lo, old_hi,
                                      _regs.ebx, _regs.ecx, ctxt)) != 0 )
                goto done;
            _regs.eflags |= EFLG_ZF;
        }
        break;
    }
#elif defined(__x86_64__)
    {
        unsigned long old, new;
        generate_exception_if((modrm_reg & 7) != 1, EXC_UD);
        if ( (rc = ops->read(ea.mem.seg, ea.mem.off, &old, 8, ctxt)) != 0 )
            goto done;
        if ( ((uint32_t)(old>>0) != (uint32_t)_regs.eax) ||
             ((uint32_t)(old>>32) != (uint32_t)_regs.edx) )
        {
            _regs.eax = (uint32_t)(old>>0);
            _regs.edx = (uint32_t)(old>>32);
            _regs.eflags &= ~EFLG_ZF;
        }
        else
        {
            new = (_regs.ecx<<32)|(uint32_t)_regs.ebx;
            if ( (rc = ops->cmpxchg(ea.mem.seg, ea.mem.off, old,
                                    new, 8, ctxt)) != 0 )
                goto done;
            _regs.eflags |= EFLG_ZF;
        }
        break;
    }
#endif

    case 0xc8 ... 0xcf: /* bswap */
        dst.type = OP_REG;
        dst.reg  = decode_register(
            (b & 7) | ((rex_prefix & 1) << 3), &_regs, 0);
        switch ( dst.bytes = op_bytes )
        {
        default: /* case 2: */
            /* Undefined behaviour. Writes zero on all tested CPUs. */
            dst.val = 0;
            break;
        case 4:
#ifdef __x86_64__
            asm ( "bswap %k0" : "=r" (dst.val) : "0" (*dst.reg) );
            break;
        case 8:
#endif
            asm ( "bswap %0" : "=r" (dst.val) : "0" (*dst.reg) );
            break;
        }
        break;
    }
    goto writeback;

 cannot_emulate:
#if 0
    gdprintk(XENLOG_DEBUG, "Instr:");
    for ( ea.mem.off = ctxt->regs->eip; ea.mem.off < _regs.eip; ea.mem.off++ )
    {
        unsigned long x;
        ops->insn_fetch(x86_seg_cs, ea.mem.off, &x, 1, ctxt);
        printk(" %02x", (uint8_t)x);
    }
    printk("\n");
#endif
    return X86EMUL_UNHANDLEABLE;
}
