enum {
	KEY_START       = 0x20000,
	KEY_STOP        = 0x10000,
	KEY_CONT        = 0x08000,
	KEY_EXAM        = 0x04000,
	KEY_DEP         = 0x02000,
	KEY_READIN      = 0x01000,
	KEY_SPARE       = 0x00800,
	KEY_MOD         = 0x00400,
	KEY_SEL1        = 0x00200,
	KEY_LOAD1       = 0x00100,
	KEY_SEL2        = 0x00080,
	KEY_LOAD2       = 0x00040,
	SW_POWER        = 0x00020,
	SW_SSTEP        = 0x00010,
	SW_SINST        = 0x00008,
	SW_REPEAT       = 0x00004,
	SW_SPARE1       = 0x00002,
	SW_SPARE2       = 0x00001,
};

typedef struct Panel Panel;
struct Panel
{
	int sw0;
	int sw1;
	int lights0;
	int lights1;
	int lights2;

	// just for convenience
	int psw1;
	int sel1;
	int sel2;
};
