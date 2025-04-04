/*
  header file for software emulation for FM sound generator

*/
#ifndef _H_FM_FM_
#define _H_FM_FM_

#include <stdlib.h>

#define DAC_SHIFT 6

/* compiler dependence */
#include "../pico_types.h"
#ifndef UINT8
typedef u8		UINT8;   /* unsigned  8bit */
typedef u16		UINT16;  /* unsigned 16bit */
typedef u32		UINT32;  /* unsigned 32bit */
#endif
#ifndef INT8
typedef s8		INT8;    /* signed  8bit   */
typedef s16		INT16;   /* signed 16bit   */
typedef s32		INT32;   /* signed 32bit   */
#endif

#if 1
/* struct describing a single operator (SLOT) */
typedef struct
{
	INT32	*DT;		/* #0x00 detune          :dt_tab[DT] */
	UINT8	ar;		/* #0x04 attack rate  */
	UINT8	d1r;		/* #0x05 decay rate   */
	UINT8	d2r;		/* #0x06 sustain rate */
	UINT8	rr;		/* #0x07 release rate */
	UINT32	mul;		/* #0x08 multiple        :ML_TABLE[ML] */

	/* Phase Generator */
	UINT32	phase;		/* #0x0c phase counter | need_save */
	UINT32	Incr;		/* #0x10 phase step */

	UINT8	KSR;		/* #0x14 key scale rate  :3-KSR */
	UINT8	ksr;		/* #0x15 key scale rate  :kcode>>(3-KSR) */

	UINT8	key;		/* #0x16 0=last key was KEY OFF, 1=KEY ON */

	/* Envelope Generator */
	UINT8	state;		/* #0x17 phase type: EG_OFF=0, EG_REL, EG_SUS, EG_DEC, EG_ATT | need_save */
	UINT16	tl;		/* #0x18 total level: TL << 3 */
	INT16	volume;		/* #0x1a envelope counter | need_save */
	UINT32	sl;		/* #0x1c sustain level:sl_table[SL] */

	/* asm relies on this order: */
	union {
		struct {
			UINT32 eg_pack_rr;  /* #0x20 1 (release state) */
			UINT32 eg_pack_d2r; /* #0x24 2 (sustain state) */
			UINT32 eg_pack_d1r; /* #0x28 3 (decay state) */
			UINT32 eg_pack_ar;  /* #0x2c 4 (attack state) */
		};
		UINT32 eg_pack[4];
	};

	UINT8	ssg;		/* 0x30 SSG-EG waveform */
	UINT8	ssgn;
	UINT16	ar_ksr;		/* 0x32 ar+ksr */
	UINT16	vol_out;	/* 0x34 current output from EG (without LFO) */
	UINT16	pad;
} FM_SLOT;


typedef struct
{
	FM_SLOT	SLOT[4];	/* four SLOTs (operators) */

	UINT8	ALGO;		/* +00 algorithm */
	UINT8	FB;		/* feedback shift */
	UINT8	pad[2];
	INT32	op1_out;	/* op1 output for feedback */

	INT32	mem_value;	/* +08 delayed sample (MEM) value */

	INT32	pms;		/* channel PMS */
	UINT8	ams;		/* channel AMS */

	UINT8	kcode;		/* +11 key code:                        */
	UINT8	pad2;
	UINT8	upd_cnt;	/* eg update counter */
	UINT32	fc;		/* fnum,blk:adjusted to sample rate */
	UINT32	block_fnum;	/* current blk/fnum value for this slot (can be different betweeen slots of one channel in 3slot mode) */

	/* LFO */
	UINT8	AMmasks;	/* AM enable flag */
	UINT8	pad3[3];
} FM_CH;

typedef struct
{
	int		clock;		/* master clock  (Hz)   */
	int		rate;		/* sampling rate (Hz)   */
	double	freqbase;	/* 08 frequency base       */
	UINT8	address;	/* 10 address register | need_save     */
	UINT8	status;		/* 11 status flag | need_save          */
	UINT8	mode;		/* mode  CSM / 3SLOT    */
	UINT8	flags;		/* operational flags	*/
	int		TA;			/* timer a              */
	//int		TAC;		/* timer a maxval       */
	//int		TAT;		/* timer a ticker | need_save */
	UINT8	TB;			/* timer b              */
	UINT8   fn_h;		/* freq latch           */
	UINT8	pad2[2];
	//int		TBC;		/* timer b maxval       */
	//int		TBT;		/* timer b ticker | need_save */
	/* local time tables */
	INT32	dt_tab[8][32];/* DeTune table       */
} FM_ST;

#define ST_SSG		1
#define ST_DAC		2

/***********************************************************/
/* OPN unit                                                */
/***********************************************************/

/* OPN 3slot struct */
typedef struct
{
	UINT32  fc[3];			/* fnum3,blk3: calculated */
	UINT8	fn_h;			/* freq3 latch */
	UINT8	kcode[3];		/* key code */
	UINT32	block_fnum[3];	/* current fnum value for this slot (can be different betweeen slots of one channel in 3slot mode) */
} FM_3SLOT;

/* OPN/A/B common state */
typedef struct
{
	FM_ST	ST;				/* general state */
	FM_3SLOT SL3;			/* 3 slot mode state */
	UINT32  pan;			/* fm channels output mask (bit 1 = enable) */

	UINT32	eg_cnt;			/* global envelope generator counter | need_save */
	UINT32	eg_timer;		/* global envelope generator counter works at frequency = chipclock/64/3 | need_save */
	UINT32	eg_timer_add;		/* step of eg_timer */

	/* LFO */
	UINT32	lfo_cnt;		/* need_save */
	UINT32	lfo_inc;
	UINT32  lfo_ampm;

	UINT32	lfo_freq[8];	/* LFO FREQ table */
} FM_OPN;

/* here's the virtual YM2612 */
typedef struct
{
	UINT8		REGS[0x200];			/* registers (for old save states) */
	INT32		addr_A1;			/* address line A1 | need_save       */

	FM_CH		CH[6];				/* channel state */

	/* dac output (YM2612) */
	int		dacen;
	INT32		dacout;

	FM_OPN		OPN;				/* OPN state            */

	UINT32		slot_mask;			/* active slot mask (performance hack) */
	UINT32		ssg_mask;			/* active ssg mask (performance hack) */
} YM2612;
#endif

#ifndef EXTERNAL_YM2612
extern YM2612 ym2612;
#endif

void YM2612Init_(int baseclock, int rate, int flags);
void YM2612ResetChip_(void);
int  YM2612UpdateOne_(s32 *buffer, int length, int stereo, int is_buf_empty);

int  YM2612Write_(unsigned int a, unsigned int v);
//unsigned char YM2612Read_(void);

int  YM2612PicoTick_(int n);
void YM2612PicoStateLoad_(void);

void *YM2612GetRegs(void);
int  YM2612PicoStateLoad2(int *tat, int *tbt, int *busy);
void YM2612PicoStateSave2(int tat, int tbt, int busy);
size_t YM2612PicoStateSave3(void *buf_, size_t size);
void   YM2612PicoStateLoad3(const void *buf_, size_t size);

/* NB must be macros for compiling GP2X 940 code */
#ifndef __GP2X__
#define YM2612Init          YM2612Init_
#define YM2612ResetChip     YM2612ResetChip_
#define YM2612UpdateOne     YM2612UpdateOne_
#define YM2612PicoStateLoad YM2612PicoStateLoad_
#else
/* GP2X specific */
#include <platform/gp2x/940ctl.h>
#define YM2612Init(baseclock, rate, flags) \
	(PicoIn.opt & POPT_EXT_FM ? YM2612Init_940 : YM2612Init_)(baseclock, rate, flags)
#define YM2612ResetChip() \
	(PicoIn.opt & POPT_EXT_FM ? YM2612ResetChip_940 : YM2612ResetChip_)()
#define YM2612PicoStateLoad() \
	(PicoIn.opt & POPT_EXT_FM ? YM2612PicoStateLoad_940 : YM2612PicoStateLoad_)()
#define YM2612UpdateOne(buffer, length, sterao, isempty) \
	(PicoIn.opt & POPT_EXT_FM ? YM2612UpdateOne_940 : YM2612UpdateOne_)(buffer, length, stereo, isempty)
#endif /* __GP2X__ */


#endif /* _H_FM_FM_ */
