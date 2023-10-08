typedef struct GPIO GPIO;
struct GPIO
{
	u32 fsel0;	// 00
	u32 fsel1;
	u32 fsel2;
	u32 fsel3;
	u32 fsel4;	// 10
	u32 fsel5;
	u32 res__18;
	u32 set0;
	u32 set1;	// 20
	u32 res__24;
	u32 clr0;
	u32 clr1;
	u32 res__30;	// 30
	u32 lev0;
	u32 lev1;
	u32 res__3c;
	u32 eds0;	// 40
	u32 eds1;
	u32 res__48;
	u32 ren0;
	u32 ren1;	// 50
	u32 res__54;
	u32 fen0;
	u32 fen1;
	u32 res__60;	// 60
	u32 hen0;
	u32 hen1;
	u32 res__6c;
	u32 len0;	// 70
	u32 len1;
	u32 res__78;
	u32 aren0;
	u32 aren1;	// 80
	u32 res__84;
	u32 afen0;
	u32 afen1;
	u32 res__90;	// 90

	// not PI 4
	u32 pud;
	u32 pudclk0;
	u32 pudclk1;
	u32 res__a0;	// a0
	u32 space[16];

	// PI 4 only
	u32 pup_pdn_cntrl_reg0;	// e4
	u32 pup_pdn_cntrl_reg1;
	u32 pup_pdn_cntrl_reg2;
	u32 pup_pdn_cntrl_reg3;
};
extern volatile GPIO *gpio;

// 10 pin settings per FSEL register
#define INP_GPIO(pin) (&gpio->fsel0)[(pin)/10] &= ~(7<<(((pin)%10)*3))
#define OUT_GPIO(pin) (&gpio->fsel0)[(pin)/10] |=  (1<<(((pin)%10)*3))

void opengpio(void);
