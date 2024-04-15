enum {
	// sw0	data right
	// sw1	data left
	// sw2	addr
	// sw3
	KEY_DEP		= 0001000,
	KEY_DEP_NEXT	= 0000400,
	KEY_READIN	= 0000200,
	KEY_START	= 0000100,
	KEY_CONT	= 0000040,
	KEY_STOP	= 0000020,
	KEY_RESET	= 0000010,
	KEY_XCT		= 0000004,
	KEY_EX		= 0000002,
	KEY_EX_NEXT	= 0000001,

	// sw4
	SW_SING_INST	= 0001000,
	SW_SING_CYC	= 0000400,
	SW_PAR_STOP	= 0000200,
	SW_NXM_STOP	= 0000100,
	SW_REPT		= 0000040,
	SW_ADR_INST	= 0000020,
	SW_ADR_RD	= 0000010,
	SW_ADR_WR	= 0000004,
	SW_ADR_STOP	= 0000002,
	SW_ADR_BREAK	= 0000001,

	// l0	MI right
	// l1	MI left
	// l2	MA
	// l3	IR
	// l4	PC
	// l5
	L5_POWER	= 0400000,
	L5_MC_STOP	= 0200000,
	L5_USER_MODE	= 0100000,
	L5_PROG_STOP	= 0040000,
	// pi iob req	037600
	// pio		000177

	// l6
	L6_MEM_DATA	= 0400000,
	L6_PROG_DATA	= 0200000,
	L6_PI_ON	= 0100000,
	L6_RUN		= 0040000,
	// pih		037600
	// pir		000177
};

typedef struct Panel Panel;
struct Panel
{
	int sw0;
	int sw1;
	int sw2;
	int sw3;
	int sw4;
	int lights0;
	int lights1;
	int lights2;
	int lights3;
	int lights4;
	int lights5;
	int lights6;
};
