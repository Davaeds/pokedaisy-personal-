// The DS games' function caller (pk_nds_call.cpp): ARM9 code that runs inside the game, from the unused top of its
// ITCM. pk_nds_call points one function pointer the game calls every frame from its main loop (Platinum: the field
// map's ApplicationManager main) at pk_nds_hook; the next frame the game calls it, it puts the pointer back, runs the
// posted job's ops and goes on into the function it replaced, as if nothing happened. No interrupt context, no
// registers to save: an ordinary call between two of the game's own.
//
// Built by scripts/gen_nds_stub.py (clang, armv5te, ARM mode) into pk_nds_stub_gen.h; it has to stay position-
// dependent on PK_NDS_JOB only.

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#include "../pk_nds_call_abi.h"

typedef u32 (*Fn)(u32, u32, u32, u32);

static u32 arg(volatile PkNdsJob* j, u32 v)
{
    return (v & ~(u32) (PK_NDS_MAX_RESULTS - 1)) == PK_NDS_RESULT_REF ? j->res[v & (PK_NDS_MAX_RESULTS - 1)] : v;
}

__attribute__((section(".text.entry"))) u32 pk_nds_hook(u32 r0, u32 r1, u32 r2, u32 r3)
{
    volatile PkNdsJob* j = (volatile PkNdsJob*) PK_NDS_JOB;
    u32 orig = j->orig;
    *(volatile u32*) j->slot = orig;
    for (u32 i = 0; i < PK_NDS_MAX_OPS; i++) {
        u32 kind = j->ops[i].kind;
        if (kind == PK_NDS_OP_END) break;
        u32 a = arg(j, j->ops[i].a[0]), b = arg(j, j->ops[i].a[1]), c = arg(j, j->ops[i].a[2]), d = arg(j, j->ops[i].a[3]);
        u32 r = 0;
        if (kind == PK_NDS_OP_STORE8) *(volatile u8*) (a + b) = (u8) c;
        else if (kind == PK_NDS_OP_STORE16) *(volatile u16*) (a + b) = (u16) c;
        else if (kind == PK_NDS_OP_STORE32) *(volatile u32*) (a + b) = c;
        else if (kind == PK_NDS_OP_LOAD32) r = *(volatile u32*) (a + b);
        else r = ((Fn) kind)(a, b, c, d);
        if (i < PK_NDS_MAX_RESULTS) j->res[i] = r;
    }
    j->done = j->seq;
    return ((Fn) orig)(r0, r1, r2, r3);
}
