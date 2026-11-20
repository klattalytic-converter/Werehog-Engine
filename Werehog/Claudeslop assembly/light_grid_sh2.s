! light_grid_sh2.s - Engine::Light::ShadeGridSh2(const GridArgs* a), in SH-2 assembly.
!
! What it does: the per-vertex loop of ShadeGridC (WHE/light_grid.cxx), with exactly the same
! results, bit for bit (tests/run.sh checks this with tools/sh2sim.py). Read the C version first:
! this file is that loop with the registers chosen by hand.
!
! Why assembly: this loop runs for every floor vertex and every nearby light each frame.
! The hand version keeps all loop state in registers (the C compiler spills some), uses the
! delay slots of the loop branches, and skips a light after the first axis that is out of reach.
!
! Target: SH-2 (SH7604), big endian. Assemble with: sh-elf-as --isa=sh2 --big
!         (or sh4-linux-gnu-as --isa=sh2 --big, which rejects anything an SH-2 can't run).
!
! Calling convention (GCC SH, which SRL uses): the argument arrives in r4; r0-r7 may be changed;
! r8-r14 and PR must be kept. MACH/MACL are saved too, so the routine is also safe when called
! from code built with Hitachi's compiler (which keeps them).
!
! Fixed-point multiply, the SGL way (slMulFX): Mul(a, b) = (a * b) >> 16 on 16.16 numbers.
!   dmuls.l a, b    -> MACH:MACL = 64-bit signed product
!   sts mach, hi
!   sts macl, lo
!   xtrct hi, lo    -> lo = middle 32 bits of the product = (a * b) >> 16
!
! GridArgs (WHE/light_grid.hpp)        GridLight (32 bytes each, from GridArgs + 0x30)
!   +0x00 vertices  (x, y, z per vertex)    +0x00 pos x    +0x0C r      +0x18 reach
!   +0x04 out       (RGB555 per vertex)     +0x04 pos y    +0x10 g      +0x1C invReach2
!   +0x08 count                             +0x08 pos z    +0x14 b
!   +0x0C lights    (0-4)
!   +0x10 base r    +0x14 base g    +0x18 base b
!   +0x1C normal x  +0x20 normal y  +0x24 normal z
!   +0x28 strength
!   +0x2C baseWord  (what an unlit vertex gets)
!   +0x30 light[0..3]
!
! Registers in the loops
!   r4  = args (never changes)        r8  = vertex pointer      r9  = out pointer
!   r10 = vertices left               r11 = light pointer       r12 = lights left
!   r1, r2, r3 = red, green, blue sums for this vertex
!   r13, r14, r0 = dx, dy, dz         r5, r6, r7 = scratch

        .text
        .align  2

! lo = (a * b) >> 16; hi is overwritten. a/b may be the same register as lo.
        .macro  MULFX a, b, hi, lo
        dmuls.l \a, \b
        sts     mach, \hi
        sts     macl, \lo
        xtrct   \hi, \lo
        .endm

! c = clamp(c >> 12, 0, 31): a 16.16 strength turned into a gouraud level (16 = as painted).
! Uses r5.
        .macro  LEVEL c
        cmp/pz  \c
        bt      .Lpos\@
        mov     #0, \c                  ! negative -> 0
.Lpos\@:
        shlr8   \c                      ! >> 12 (the value is >= 0 here, so a logical shift is fine)
        shlr2   \c
        shlr2   \c
        mov     #31, r5
        cmp/gt  r5, \c                  ! above 31?
        bf      .Lok\@
        mov     r5, \c
.Lok\@:
        .endm

! d = (L.pos[k] - v[k]) >> 4, then leave the light if |d| >= reach (r5 = reach, r6 = -reach).
! SH-2 has no multi-bit arithmetic shift, so >> 4 is four single-bit shifts.
        .macro  AXIS k, d, tmp
        mov.l   @(\k,r11), \d
        mov.l   @(\k,r8), \tmp
        sub     \tmp, \d
        shar    \d
        shar    \d
        shar    \d
        shar    \d
        cmp/ge  r5, \d                  ! d >= reach ?
        bt      .Lnext
        cmp/ge  \d, r6                  ! -reach >= d ?
        bt      .Lnext
        .endm

        .global _ShadeGridSh2
        .global ShadeGridSh2
_ShadeGridSh2:
ShadeGridSh2:
        mov.l   r8, @-r15               ! save what the caller expects to keep
        mov.l   r9, @-r15
        mov.l   r10, @-r15
        mov.l   r11, @-r15
        mov.l   r12, @-r15
        mov.l   r13, @-r15
        mov.l   r14, @-r15
        sts.l   mach, @-r15
        sts.l   macl, @-r15

        mov.l   @(0x08,r4), r10         ! r10 = vertex count
        tst     r10, r10
        bf      .Lstart                 ! some vertices: go
        bra     .Ldone                  ! none: return (bra reaches further than bt/bf)
        nop
.Lstart:
        mov.l   @(0x00,r4), r8          ! r8 = first vertex
        mov.l   @(0x04,r4), r9          ! r9 = first output word

! ---------------------------------------------------------------- one vertex
.Lvertex:
        mov.l   @(0x10,r4), r1          ! start from the flat ambient + sun colour
        mov.l   @(0x14,r4), r2
        mov.l   @(0x18,r4), r3
        mov.l   @(0x0C,r4), r12         ! r12 = lights to try
        tst     r12, r12
        bt      .Lpack                  ! no point lights this frame
        mov     r4, r11
        add     #0x30, r11              ! r11 = &light[0]

! ---------------------------------------------------------------- one light
.Llight:
        mov.l   @(0x18,r11), r5         ! r5 = reach
        mov     r5, r6
        neg     r6, r6                  ! r6 = -reach
        AXIS    8, r0, r7               ! r0  = dz  (each AXIS may jump to .Lnext; z first,
        AXIS    0, r13, r7              ! r13 = dx   because on the floor x was tested per column
        AXIS    4, r14, r7              ! r14 = dy   and the height nearly always passes)

        ! is the light in front of the surface? facing = n . d, must be > 0
        mov.l   @(0x1C,r4), r5
        MULFX   r5, r13, r6, r7         ! r7 = nx * dx
        mov.l   @(0x20,r4), r5
        MULFX   r5, r14, r6, r5
        add     r5, r7                  !    + ny * dy
        mov.l   @(0x24,r4), r5
        MULFX   r5, r0, r6, r5
        add     r5, r7                  !    + nz * dz
        cmp/pl  r7
        bf      .Lnext                  ! behind (or edge-on): no light

        ! d2 = dx^2 + dy^2 + dz^2
        MULFX   r13, r13, r6, r7
        MULFX   r14, r14, r6, r5
        add     r5, r7
        MULFX   r0, r0, r6, r5
        add     r5, r7                  ! r7 = d2

        ! att = 1.0 - d2 * invReach2; nothing if <= 0
        mov.l   @(0x1C,r11), r5
        MULFX   r7, r5, r6, r5          ! r5 = d2 / reach^2
        mov     #1, r7
        shll16  r7                      ! r7 = 1.0 (0x10000)
        sub     r5, r7                  ! r7 = att
        cmp/pl  r7
        bf      .Lnext

        MULFX   r7, r7, r6, r7          ! att = att^2 (smooth edge)
        mov.l   @(0x28,r4), r5
        MULFX   r7, r5, r6, r7          ! att *= strength

        ! add this light's colour
        mov.l   @(0x0C,r11), r5
        MULFX   r7, r5, r6, r5
        add     r5, r1                  ! red
        mov.l   @(0x10,r11), r5
        MULFX   r7, r5, r6, r5
        add     r5, r2                  ! green
        mov.l   @(0x14,r11), r5
        MULFX   r7, r5, r6, r5
        add     r5, r3                  ! blue

.Lnext:
        dt      r12                     ! one light fewer; T = (r12 == 0)
        bf/s    .Llight
        add     #32, r11                ! (delay slot, runs either way) next GridLight

! ---------------------------------------------------------------- pack RGB555 and store
! An unlit vertex still has exactly the base colour in r1-r3 (lights only ever add to them), so
! three compares tell whether any light reached it; if none did, store the precomputed word.
.Lpack:
        mov.l   @(0x10,r4), r5
        cmp/eq  r5, r1
        bf      .Lpacknow
        mov.l   @(0x14,r4), r5
        cmp/eq  r5, r2
        bf      .Lpacknow
        mov.l   @(0x18,r4), r5
        cmp/eq  r5, r3
        bf      .Lpacknow
        mov.l   @(0x2C,r4), r1          ! unlit: the base word
        bra     .Lstore
        nop
.Lpacknow:
        LEVEL   r1                      ! r1 = red level
        LEVEL   r2                      ! r2 = green level
        LEVEL   r3                      ! r3 = blue level
        shll2   r2                      ! green << 5
        shll2   r2
        shll    r2
        shll8   r3                      ! blue << 10
        shll2   r3
        or      r2, r1
        or      r3, r1
        mov     #-128, r5               ! 0xFFFFFF80
        shll8   r5                      ! 0xFFFF8000: bit 15 (VDP1 ignores it; kept like the C code)
        or      r5, r1
.Lstore:
        mov.w   r1, @r9                 ! store the low 16 bits
        add     #2, r9
        add     #12, r8                 ! next vertex
        dt      r10                     ! one vertex fewer; T = (r10 == 0)
        bt      .Ldone
        bra     .Lvertex                ! the loop is longer than bt/bf can jump back (+-256 bytes)
        nop

! ---------------------------------------------------------------- return
.Ldone:
        lds.l   @r15+, macl
        lds.l   @r15+, mach
        mov.l   @r15+, r14
        mov.l   @r15+, r13
        mov.l   @r15+, r12
        mov.l   @r15+, r11
        mov.l   @r15+, r10
        mov.l   @r15+, r9
        rts
        mov.l   @r15+, r8               ! (delay slot) last restore
