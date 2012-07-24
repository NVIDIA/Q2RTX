//
// r_varsa.s
//

#include "common/x86/asm.h"
#include "sw.h"

#if USE_ASM

	.data

//-------------------------------------------------------
// ASM-only variables
//-------------------------------------------------------
.globl	float_1, float_particle_z_clip, float_point5
.globl	float_minus_1, float_0
float_0:		.single	0.0
float_1:		.single	1.0
float_minus_1:	.single	-1.0
float_particle_z_clip:	.single	PARTICLE_Z_CLIP
float_point5:	.single	0.5

.globl	fp_16, fp_64k, fp_1m, fp_64kx64k
.globl	fp_1m_minus_1
.globl	fp_8 
fp_1m:			.single	1048576.0
fp_1m_minus_1:	.single	1048575.0
fp_64k:			.single	65536.0
fp_8:			.single	8.0
fp_16:			.single	16.0
fp_64kx64k:		.long	0x4f000000	// (float)0x8000*0x10000


.globl	FloatZero, Float2ToThe31nd, FloatMinus2ToThe31nd
FloatZero:				.long	0
Float2ToThe31nd:		.long	0x4f000000
FloatMinus2ToThe31nd:	.long	0xcf000000

.globl	C(r_bmodelactive)
C(r_bmodelactive):	.long	0

//-------------------------------------------------------
// global refresh variables
//-------------------------------------------------------

// FIXME: put all refresh variables into one contiguous block. Make into one
// big structure, like cl or sv?

	.align	4
.globl	C(d_sdivzstepu)
.globl	C(d_tdivzstepu)
.globl	C(d_zistepu)
.globl	C(d_sdivzstepv)
.globl	C(d_tdivzstepv)
.globl	C(d_zistepv)
.globl	C(d_sdivzorigin)
.globl	C(d_tdivzorigin)
.globl	C(d_ziorigin)
C(d_sdivzstepu):	.single	0
C(d_tdivzstepu):	.single	0
C(d_zistepu):		.single	0
C(d_sdivzstepv):	.single	0
C(d_tdivzstepv):	.single	0
C(d_zistepv):		.single	0
C(d_sdivzorigin):	.single	0
C(d_tdivzorigin):	.single	0
C(d_ziorigin):		.single	0

.globl	C(sadjust)
.globl	C(tadjust)
.globl	C(bbextents)
.globl	C(bbextentt)
C(sadjust):			.long	0
C(tadjust):			.long	0
C(bbextents):		.long	0
C(bbextentt):		.long	0

.globl	C(cacheblock)
.globl	C(d_viewbuffer)
.globl	C(cachewidth)
.globl	C(d_pzbuffer)
.globl	C(d_zrowbytes)
.globl	C(d_zwidth)
C(cacheblock):		.long	0
C(cachewidth):		.long	0
C(d_viewbuffer):	.long	0
C(d_pzbuffer):		.long	0
C(d_zrowbytes):		.long	0
C(d_zwidth):		.long	0


//-------------------------------------------------------
// ASM-only variables
//-------------------------------------------------------
.globl	izi
izi:			.long	0

.globl	pbase, s, t, sfracf, tfracf, snext, tnext
.globl	spancountminus1, zi16stepu, sdivz16stepu, tdivz16stepu
.globl	zi8stepu, sdivz8stepu, tdivz8stepu, pz
s:				.long	0
t:				.long	0
snext:			.long	0
tnext:			.long	0
sfracf:			.long	0
tfracf:			.long	0
pbase:			.long	0
zi8stepu:		.long	0
sdivz8stepu:	.long	0
tdivz8stepu:	.long	0
zi16stepu:		.long	0
sdivz16stepu:	.long	0
tdivz16stepu:	.long	0
spancountminus1: .long	0
pz:				.long	0

.globl	izistep
izistep:				.long	0

//-------------------------------------------------------
// local variables for d_draw16.s
//-------------------------------------------------------

.globl	reciprocal_table_16, entryvec_table_16
// 1/2, 1/3, 1/4, 1/5, 1/6, 1/7, 1/8, 1/9, 1/10, 1/11, 1/12, 1/13,
// 1/14, and 1/15 in 0.32 form
reciprocal_table_16:	.long	0x40000000, 0x2aaaaaaa, 0x20000000
						.long	0x19999999, 0x15555555, 0x12492492
						.long	0x10000000, 0xe38e38e, 0xccccccc, 0xba2e8ba
						.long	0xaaaaaaa, 0x9d89d89, 0x9249249, 0x8888888

entryvec_table_16:	.long	0, Entry2_16, Entry3_16, Entry4_16
					.long	Entry5_16, Entry6_16, Entry7_16, Entry8_16
					.long	Entry9_16, Entry10_16, Entry11_16, Entry12_16
					.long	Entry13_16, Entry14_16, Entry15_16, Entry16_16

//
// advancetable is 8 bytes, but points to the middle of that range so negative
// offsets will work
//
.globl	advancetable, sstep, tstep, pspantemp, counttemp, jumptemp
advancetable:	.long	0, 0
sstep:			.long	0
tstep:			.long	0

pspantemp:		.long	0
counttemp:		.long	0
jumptemp:		.long	0

// 1/2, 1/3, 1/4, 1/5, 1/6, and 1/7 in 0.32 form
.globl	reciprocal_table, entryvec_table
reciprocal_table:	.long	0x40000000, 0x2aaaaaaa, 0x20000000
					.long	0x19999999, 0x15555555, 0x12492492

#endif	// USE_ASM


