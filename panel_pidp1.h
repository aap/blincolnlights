enum {
	// sw0
	SW_EXTEND	= 0400000,
	SW_POWER	= 0200000,

	// sw2
	SW_SSTEP	= 0400000,
	SW_SINST	= 0200000,
	// SS:	100000 - 002000
	KEY_START	= 0001000,
	KEY_START_UP	= 0000400,
	KEY_STOP	= 0000200,
	KEY_CONT	= 0000100,
	KEY_EXAM	= 0000040,
	KEY_DEP		= 0000020,
	KEY_READIN	= 0000010,
	KEY_READER	= 0000004,
	KEY_READER_UP	= 0000002,
	KEY_FEED	= 0000001,

	L5_RUN		= 0400000,
	L5_CYC		= 0200000,
	L5_DF1		= 0100000,
	L5_HSC		= 0040000,
	L5_BC1		= 0020000,
	L5_BC2		= 0010000,
	L5_OV1		= 0004000,
	L5_RIM		= 0002000,
	L5_SBM		= 0001000,
	L5_EXD		= 0000400,
	L5_IOH		= 0000200,
	L5_IOC		= 0000100,
	L5_IOS		= 0000040,
	L5_PWR		= 0000004,
	L5_SSTEP	= 0000002,
	L5_SINST	= 0000001,
	// L6:
	// IR:	400000-020000
	// SS:	004000-000100
	// PF:	000040-000001
};

typedef struct Panel Panel;
struct Panel
{
	int sw0;
	int sw1;
	int sw2;
	int sw3;	// controllers
	int lights0;
	int lights1;
	int lights2;
	int lights3;
	int lights4;
	int lights5;
	int lights6;
	// IO panel
	int lights7;
	int lights8;
	int lights9;

	// just for convenience
	int psw2;
};
