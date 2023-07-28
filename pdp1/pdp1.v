/* verilator lint_off LITENDIAN */

/* verilator lint_off DECLFILENAME */
/* verilator lint_off WIDTH */
/* verilator lint_off UNUSED */

`define FAST

module pdp1(
	input clk,
	input reset,

	input start_sw,
	input sbm_start_sw,
	input stop_sw,
	input continue_sw,
	input examine_sw,
	input deposit_sw,
	input read_in_sw,

	input power_sw,
	input single_cyc_sw,
	input single_inst_sw,

	input [6:17] ta,
	input [0:17] tw,
	input [6:1] ss,

	output [0:4] ir,
	output [6:17] pc,
	output [6:17] ma,
	output [0:17] mb,
	output [0:17] ac,
	output [0:17] io,
	output [6:1] pf,

	output run,
	output cyc,
	output df1,
	output df2,
	output bc1,
	output bc2,
	output ov1,
	output ov2,
	output rim,
	output sbm,
	output ioh,

	output ioc,
	output ihs,
	output ios,
	output hsc,

	output r,
	output rs,
	output w,
	output i,

	output power_clear,
	output sc,

	/* Display */
	output wire dpy,
	output wire clr_db,
	input dpy_done,

	/* Typewriter */
	output wire tyo,
	output wire clr_tb,
	input tyo_done,
	output wire tyi,
	input type_sync,
	input [12:17] tb,

	/* Reader */
	output wire rpa,
	output wire rpb,
	output rim_or_rcp,
	input reader_return,
	input [0:17] rb,
	input clr_io_reader,

	/* Punch */
	output wire ppa,
	output wire ppb,
	output wire clr_pb_on_ppa,
	output wire clr_pb_ppb,
	input punch_done,

	/* Debug */
	output [31:0] ncycle
);
	// TODO: this is wrong
	wire power_clear = reset;

	/*
	 * Basic control and timing
	 */

	wire sp1, sp1_dly, sp2, sp2_dly, sp3, sp3_dly, sp4;

	wire start, stop, cntinue, examine, deposit, read_in;
	wire any_start_sw = start_sw|sbm_start_sw;
	pg pg1(clk, reset, any_start_sw, start);
	pg pg2(clk, reset, stop_sw, stop);
	pg pg3(clk, reset, continue_sw, cntinue);
	pg pg4(clk, reset, examine_sw, examine);
	pg pg5(clk, reset, deposit_sw, deposit);
	pg pg6(clk, reset, read_in_sw, read_in);

	wire pb, pb_dly;
	pa pa1(clk, reset, start|cntinue|examine|deposit|read_in, pb);
	dly500us dly1(clk, reset, pb, pb_dly);
	dly2us dly2(clk, reset, sp1, sp1_dly);
	dly2us dly3(clk, reset, sp2, sp2_dly);
	dly1us dly4(clk, reset, sp3, sp3_dly);
	pa pa2(clk, reset, pb_dly | tp10 & CY1 & rim & DIO, sp1);
	pa pa3(clk, reset, sp1_dly & ~rim | reader_return_delayed & rim & ~stop_sw & IR_00, sp2);
	pa pa4(clk, reset, sp2_dly, sp3);
	pa pa5(clk, reset, sp3_dly, sp4);
	wire sc;
	pa pa6(clk, reset, sp1 & (start_sw | examine_sw | deposit_sw | rim), sc);
	wire reader_return_delayed;
	dly10us dly5(clk, reset, reader_return, reader_return_delayed);

	reg [31:0] ncycle = 0;
	always @(posedge clk) begin
		if(sp4) ncycle <= 0;
		if(tp10) ncycle <= ncycle + 1;
	end

	wire DIO_rim = DIO & rim;
	wire rpb_rim = sp1 & rim | sp4 & DIO_rim;
`ifdef FAST
	reg [0:12] timer = 0;
	always @(posedge clk) begin
		timer <= { 1'b0, timer[0:11] };
		if(tp10 & run |
		   sp4 & (continue_sw | any_start_sw) |
		   reader_return_delayed & DIO_rim)
			timer <= 'b1000000000000;
		if(sp4 & (examine_sw | deposit_sw | JMP_rim))
			timer <= 'b0100000000000;
	end
	wire tp0 = timer[0];
	wire tp1 = timer[1];
	wire tp2 = timer[2];
	wire tp3 = timer[3];
	wire tp4 = timer[4];
	wire tp5 = timer[5];
	wire tp6 = timer[6];
	wire tp6a = timer[7];
	wire tp7 = timer[8];
	wire tp8 = timer[9];
	wire tp9 = timer[10];
	wire tp9a = timer[11];
	wire tp10 = timer[12];
`else
	wire tp0, tp0_dly, tp1, tp1_dly, tp2, tp2_dly, tp3, tp3_dly;
	wire tp4, tp4_dly, tp5, tp5_dly, tp6;
	wire tp6a, tp6a_dly, tp7, tp7_dly, tp8, tp8_dly;
	wire tp9, tp9_dly, tp9a, tp9a_dly, tp10, tp10_dly;
	wire memory_strobe;
	dly200ns dly10(clk, reset, tp10, tp10_dly);
	dly200ns dly11(clk, reset, tp0, tp0_dly);
	dly300ns dly12(clk, reset, tp1, tp1_dly);
	dly550ns dly13(clk, reset, tp2, tp2_dly);
	dly1us dly14(clk, reset, tp3, tp3_dly);
	dly200ns dly15(clk, reset, tp4, tp4_dly);
	dly250ns dly16(clk, reset, tp5, tp5_dly);
	dly400ns dly17(clk, reset, tp6a, tp6a_dly);
	dly200ns dly18(clk, reset, tp7, tp7_dly);
	dly200ns dly19(clk, reset, tp8, tp8_dly);
	dly1_2us dly20(clk, reset, tp9, tp9_dly);
	dly750ns dly21(clk, reset, tp9a, tp9a_dly);
	pa pa10(clk, reset,
		tp10_dly & run |
		sp4 & (continue_sw | any_start_sw) |
		reader_return_delayed & DIO_rim,
		tp0);
	pa pa11(clk, reset, tp0_dly | sp4 & (examine_sw | deposit_sw | JMP_rim), tp1);
	pa pa12(clk, reset, tp1_dly, tp2);
	pa pa13(clk, reset, tp2_dly, tp3);
	pa pa14(clk, reset, memory_strobe, tp4);
	pa pa15(clk, reset, tp4_dly, tp5);
	pa pa16(clk, reset, tp5_dly, tp6);
	pa pa17(clk, reset, tp3_dly, tp6a);
	pa pa18(clk, reset, tp6a_dly, tp7);
	pa pa19(clk, reset, tp7_dly, tp8);
	pa pa20(clk, reset, tp8_dly, tp9);
	pa pa21(clk, reset, tp9_dly, tp9a);
	pa pa22(clk, reset, tp9a_dly, tp10);
`endif

	wire inst_done = (ir[0]&ir[1] & (~cyc & ~df1 | DF_ndf2) | CY1) & ~BC1_BC2_BC3;
	wire manual_run = single_cyc_sw | single_inst_sw & inst_done;

	reg run;
	reg run_enable;
	wire halt_cond = OPR & mb[9] | manual_run | incorr_op_sel | ~run_enable;
	always @(posedge clk) begin
		if(start | stop | cntinue | examine | deposit | read_in)
			run_enable <= 0;
		if(sp1)
			run_enable <= 1;
/**/		if(sp4 & (any_start_sw | continue_sw | JMP_rim))
			run <= 1;
/**/		if(tp9 & halt_cond)
			run <= 0;
	end

	reg cyc;
	wire CY1 = cyc & ~hsc & ~df1 & ~BC1_BC2_BC3;	// TODO? confusing drawings
	always @(posedge clk) begin
		if(sp1 & any_start_sw |
		   sp4 & JMP_rim |
		   tp3 & CY1 & XCT |
/**/		   tp10 & inst_done)
			cyc <= 0;
		if(sp2 & (deposit_sw | examine_sw | rim) |
/**/		   tp10 & ~cyc & (df1 | ~ir[0] | ~ir[1]))
			cyc <= 1;
	end

	reg df1, df2;
	always @(posedge clk) begin
		if(sc | tp10 & DF_ndf2 | pc_dec)
			df1 <= 0;
		if(tp6 & ~cyc & mb[5] & ~(SH_RO | SKP | LAW | OPR | IOT) |
		   tp10 & BC3)		// TODO: eh?
			df1 <= 1;

		if(sc | tp10 | pc_dec)
			df2 <= 0;
/**/		if(tp6 & DF & mb[5])
			df2 <= 1;
	end
	wire DF = df1 & cyc;
	wire DF_ndf2 = DF & ~df2;

	reg sbm;
	always @(posedge clk) begin
		if(sc) sbm <= sbm_start_sw;
	end

	reg hsc;
	always @(posedge clk) begin
		if(sc) hsc <= 0;
	end

	reg bc2, bc1;
	wire BC1 = ~bc2 & bc1;
	wire BC2 = bc2 & ~bc1;
	wire BC3 = bc2 & bc1;
	wire BC1_BC2_BC3 = bc2 | bc1;
	always @(posedge clk) begin
		if(sc) begin
			bc1 <= 0;
			bc2 <= 0;
		end
	end

	reg ioc;
	reg ihs;
	reg ios;
	reg ioh;
	wire io_halt_done = IOT & ~ioh & ~ihs;
	wire io_halt_not_done = IOT & ~io_halt_done;
	wire iot_ioc = IOT & ioc;
	always @(posedge clk) begin
		if(sc) begin
			ioc <= 1;
			ihs <= 0;
			ios <= 0;
			ioh <= 0;
		end
		if(tp2 & io_halt_not_done) ioc <= 0;
		if(tp2 & io_halt_done) ioc <= 1;
		if(tp2) ihs <= 0;
		if(tp10 & io_halt_done)
			ios <= 0;
		if(iot_done)
			ios <= 1;
		if(tp9 & IOT & ~ihs & ios)
			ioh <= 0;
		if(tp7 & mb[5] & io_halt_done |
		   tp10 & ihs)
			ioh <= 1;
	end

	/*
	 * Instruction register
	 */

	reg [0:4] ir;
	wire ir_clr = sc | tp4 & (BC1 | hsc | ~cyc);
	wire ir_mb_1 = sp3 & rim | tp5 & ~cyc;
	always @(posedge clk) begin
		if(ir_clr) ir <= 0;
		if(ir_mb_1) ir <= ir | mb[0:4];
		if(sp2) begin
			if(examine_sw | deposit_sw) ir[1] <= 1;
			if(deposit_sw) ir[3] <= 1;
		end
		if(tp10 & BC3) begin
			// JSP
			ir[0] <= 1;
			ir[1] <= 1;
			ir[4] <= 1;
		end
	end

	reg rim;
	always @(posedge clk) begin
		if(read_in) rim <= 1;
		if(start | deposit | examine | sp4 & JMP_rim) rim <= 0;
	end

	wire IR_00 = ir == 5'b00000;
	wire AND = ir == 5'b00001;
	wire IOR = ir == 5'b00010;
	wire XOR = ir == 5'b00011;
	wire XCT = ir == 5'b00100;
	wire IR_12 = ir == 5'b00101;
	wire XAM = ir == 5'b00110;		// documented nowhere, is this even working?
	wire JDA = ir == 5'b00111;
	wire LAC = ir == 5'b01000;
	wire LIO = ir == 5'b01001;
	wire DAC = ir == 5'b01010;
	wire DAP = ir == 5'b01011;
	wire DIP = ir == 5'b01100;
	wire DIO = ir == 5'b01101;
	wire DZM = ir == 5'b01110;
	wire IR_36 = ir == 5'b01111;
	wire ADD = ir == 5'b10000;
	wire SUB = ir == 5'b10001;
	wire IDX = ir == 5'b10010;
	wire ISP = ir == 5'b10011;
	wire SAD = ir == 5'b10100;
	wire SAS = ir == 5'b10101;
	wire MUS_MUL = ir == 5'b10110;
	wire DIS_DIV = ir == 5'b10111;
	wire JMP = ir == 5'b11000;
	wire JSP = ir == 5'b11001;
	wire SKP = ir == 5'b11010;
	wire SH_RO = ir == 5'b11011;
	wire LAW = ir == 5'b11100;
	wire IOT = ir == 5'b11101;
	wire IR_74 = ir == 5'b11110;
	wire OPR = ir == 5'b11111;
	wire JMP_rim = JMP & rim;
	wire JMP_JSP = JMP | JSP;
	wire BC1_JDA_CY1 = BC1 | JDA & CY1;
	// BUG? added 74, also 14 because what the heck is XAM?
	wire incorr_op_sel = (IR_00 | IR_12 | XAM | IR_36 | IR_74) & ~BC1_BC2_BC3 & ~hsc;
// TODO: type 10?
wire MUS = MUS_MUL;
wire DIS = DIS_DIV;

	/* RO/SH decoding */
	wire ai = SH_RO & mb[8] & mb[7];
	wire ac_only = SH_RO & mb[8] & ~mb[7];
	wire io_only = SH_RO & ~mb[8] & mb[7];
	wire rotate = SH_RO & ~mb[6];
	wire ai_rotate = ai & rotate;
	wire ac_rotate = ac_only & rotate;
	wire io_rotate = io_only & rotate;
	wire shro_l = SH_RO & ~mb[5];
	wire shro_r = SH_RO & mb[5];
	wire not_io_shift = SH_RO & (mb[8] | ~mb[6]) | DIS_DIV;
	wire ai_mus_mul_dis_div = ai | MUS_MUL | DIS_DIV;
	wire rotate_dis_div = rotate | DIS_DIV;
	wire shro_pulse = SH_RO & (tp0&mb[12] | tp1&mb[11] | tp2&mb[10] | tp3&mb[9] |
			tp7&mb[17] | tp8&mb[16] | tp9&mb[15] | tp9a&mb[14] | tp10&mb[13]) |
		tp8 & MUS & CY1 |
		tp0 & DIS & CY1;

	// shift inputs - done slightly differently for cleaner verilog
	wire io_0_l = not_io_shift ? io[1] : io[0];
	wire io_0_r = io_rotate ? io[17] : ai_mus_mul_dis_div ? ac[17] : io[0];
	wire ac_0_l = rotate_dis_div ? ac[1] : ac[0];
// TODO: drawings have DIV...?
	wire ac_0_r = ac_rotate ? ac[17] : ai_rotate ? io[17] : (MUS_MUL | DIS_DIV) ? 0 : ac[0];
	wire io_17_l = io_only ? io[0] : ai ? ac[0] : DIS_DIV ? ~ac[0] : io[17];
	wire ac_17_l = ac_only ? ac[0] : ai_mus_mul_dis_div ? io[0] : ac[17];


	/* Program flags */

	reg [6:1] pf;
	wire [0:7] mbda;
	wire [0:7] mbdb;
	wire [0:7] mbdc;
	wire [0:7] mbdd;
	decode8 deca(mb[15:17], mbda);
	decode8 decb(mb[12:14], mbdb);
	decode8 decc(mb[9:11], mbdc);
	decode8 decd(mb[6:8], mbdd);
	always @(posedge clk) begin
		if(tp8 & OPR) begin
			if(mbda[1] | mbda[7]) pf[1] <= mb[14];
			if(mbda[2] | mbda[7]) pf[2] <= mb[14];
			if(mbda[3] | mbda[7]) pf[3] <= mb[14];
			if(mbda[4] | mbda[7]) pf[4] <= mb[14];
			if(mbda[5] | mbda[7]) pf[5] <= mb[14];
			if(mbda[6] | mbda[7]) pf[6] <= mb[14];
		end
		if(set_pf1) pf[1] <= 1;
		if(set_pf2) pf[2] <= 1;
		if(set_pf3) pf[3] <= 1;
		if(set_pf4) pf[4] <= 1;
		if(set_pf5) pf[5] <= 1;
		if(set_pf6) pf[6] <= 1;
	end

	/* PC */
	wire cy1_skip = CY1 & (SAS & ac_pos_zero | SAD & ~ac_pos_zero | ISP & ~ac[0]);
	wire enable =
		mbda[7] & (pf == 0) |
		mbda[6] & ~pf[6] | mbda[5] & ~pf[5] | mbda[4] & ~pf[4] |
		mbda[3] & ~pf[3] | mbda[2] & ~pf[2] | mbda[1] & ~pf[1] |
		mbdb[7] & (ss == 0) |
		mbdb[6] & ~ss[6] | mbdb[5] & ~ss[5] | mbdb[4] & ~ss[4] |
		mbdb[3] & ~ss[3] | mbdb[2] & ~ss[2] | mbdb[1] & ~ss[1] |
		mb[7] & ~io[0] |
		mb[8] & ~ov1 |
		mb[9] & ac[0] |
		mb[10] & ~ac[0] |
		mb[11] & ac_pos_zero;
	wire mb_pc = ~cyc & ~df1 & JMP_JSP | DF_ndf2 & JMP_JSP;
	wire pc_clr = sc | tp8 & (mb_pc | BC1_JDA_CY1);
	wire mb_pc_1 = sp4 & JMP_rim | tp9 & mb_pc;
	wire ta_pc_1 = sp2 & (any_start_sw | deposit_sw | examine_sw);
	wire ma_pc_1 = tp9 & BC1_JDA_CY1;
/**/	wire pc_inc = tp2 & ~cyc | tp8 & (cy1_skip | SKP & (enable != mb[5])) |
		tp9a & (BC1_BC2_BC3 | BC1_JDA_CY1);
/**/	wire pc_dec = tp9a & IOT & ioh;
	reg [6:17] pc;
	always @(posedge clk) begin
		if(pc_clr) pc <= 0;
		if(mb_pc_1) pc <= pc | mb[6:17];
		if(ta_pc_1) pc <= pc | ta[6:17];
		if(ma_pc_1) pc <= pc | ma;
		if(pc_inc) pc <= pc + 1;
		if(pc_dec) pc <= pc - 1;
	end


	/* MA */
	wire ma_clr = sp1 | tp10 & run;
	wire set_ma = tp0 & cyc & ~hsc & ~BC1_BC2_BC3;
	wire ma_100 = JDA & ~mb[5];				// MISSING in the PDP-1C drawings
	wire mb_ma_1 = sp3 & rim | set_ma & ~ma_100;
	wire be_ma_1 = tp0 & BC1;
	wire pc_ma_1 = sp3 & (examine_sw|deposit_sw) | tp0 & (~cyc | bc2);
	reg [6:17] ma;
	always @(posedge clk) begin
		if(ma_clr) ma <= 0;
		if(mb_ma_1) ma <= ma | mb[6:17];
		if(pc_ma_1) ma <= ma | pc;
		if(set_ma & ma_100) ma[11] <= 1;
// TODO: BE, HSAM
	end


	/* MB */
/**/	wire mb_clr = sp1 & rim | tp3 | tp5 & (CY1 & DZM | CY1 & DIO | BC3) | hsc_mb_clr;
/**/	wire ac_mb_0_5 = tp7 & (CY1 & (DIP | DAC | IDX | ISP | JDA) | BC1 | BC2);
/**/	wire ac_mb_6_17 = tp7 & (CY1 & (DAP | DAC | IDX | ISP | JDA) | BC1 | BC2);
/**/	wire io_mb_1 = sp2 & rim | tp7 & (CY1 & DIO | BC3);
	reg [0:17] mb;
	always @(posedge clk) begin
		if(mb_clr) mb <= 0;
		if(ac_mb_0_5) mb[0:5] <= ac[0:5];
		if(ac_mb_6_17) mb[6:17] <= ac[6:17];
		if(io_mb_1) mb <= mb | io;
// a bit wrong
if(tp4) mb <= mb | mbm;
	end


	/* AC */
/**/	wire mb_ac_0 = tp5 & CY1 & (AND | LAC | IDX | ISP);
/**/	wire mb_ac_1_0_5 = tp5 & CY1 & (IOR | LAC | IDX | ISP);
/**/	wire mb_ac_1_6_17 = tp5 & CY1 & (IOR | LAC | IDX | ISP) | tp8 & LAW;
	wire pc_ac = JSP & ~cyc & ~df1 | JSP & DF_ndf2 | BC1_JDA_CY1;
/**/	wire ac_clr = sp1 & deposit_sw | tp7 & (pc_ac | LAW | OPR & mb[10]) |
		tp9a & (ADD | DIS) & CY1 & ac_neg_zero |
		tp6 & ac_neg_one & inc;
	wire pc_ac_1 = tp8 & (pc_ac | OPR & mb[11]);
	wire inc = (IDX | ISP) & CY1;
/**/	wire ac_inc = tp6 & ~ac_neg_one & inc | tp2 & DIS & CY1 & ~io[17];
	wire ac_sub = (DIS & io[17] | SUB) & CY1;
/**/	wire ac_comp = tp4 & ac_sub | tp9 & (ac_sub | OPR & mb[8] | LAW & mb[5]);
	wire tw_ac_1 = sp2 & deposit_sw | tp8 & (OPR & mb[7]);
	wire padd = CY1 & (XOR | ADD | SUB | SAD | SAS | DIS | MUS & io[17]);
/**/	wire mb_pad_ac = tp5 & padd | tp9 & CY1 & (SAD | SAS);
/**/	wire carry_ac = tp6 & (padd & ~XOR & ~SAD & ~SAS);
/**/	wire ac_shro_l = shro_pulse & ((shro_l & mb[8]) | DIS);
/**/	wire ac_shro_r = shro_pulse & ((shro_r & mb[8]) | MUS);	// drawings have MUS_MUL here, but this is cleaner
	reg [0:17] ac;
	reg cry_wrap;
	wire [0:17] cry_in = ~ac & mb;
	wire ac_pos_zero = ac == 0;
	wire ac_neg_zero = ac == 'o777777;
	wire ac_neg_one = ac == 'o777776;
	reg ov1, ov2;
	wire set_ov2 = (ADD | SUB) & CY1 & (mb[0] != ac[0]);
	always @(posedge clk) begin
		cry_wrap <= 0;
		if(ac_clr) ac <= 0;
		if(ac_comp) ac <= ~ac;
		if(mb_ac_0) ac <= ac & mb;
		if(mb_ac_1_0_5) ac[0:5] <= ac[0:5] | mb[0:5];
		if(mb_ac_1_6_17) ac[6:17] <= ac[6:17] | mb[6:17];
		// this sucks
		if(mb_ac_0 & mb_ac_1_0_5) ac[0:5] <= mb[0:5];
		if(mb_ac_0 & mb_ac_1_6_17) ac[6:17] <= mb[6:17];

		if(pc_ac_1) begin
			ac[0] <= ac[0] | ov1;
			// TODO: ac[1:5] from exd/epc
			ac[6:17] <= ac[6:17] | pc;
		end
		if(tw_ac_1) ac <= ac | tw;
		if(mb_pad_ac) ac <= ac ^ mb;
		if(carry_ac) { cry_wrap, ac } <= ac + { cry_in[1:17], cry_in[0] };
		if(ac_inc) { cry_wrap, ac } <= ac + 1;
		if(cry_wrap) ac <= ac + 1;

		if(ac_shro_l) ac <= { ac_0_l, ac[2:17], ac_17_l };
		if(ac_shro_r) ac <= { ac_0_r, ac[0:16] };

		if(sc) begin
			ov1 <= 0;
			ov2 <= 0;
		end
		if(tp10 | tp9 & set_ov2) ov2 <= 0;
		if(tp5 & set_ov2) ov2 <= 1; 
/**/		if(tp10 & ov2) ov1 <= 1;
		if(tp9 & SKP & mb[8] |
		   tp10 & BC3)
			ov1 <= 0;
	end


	/* IO */
/**/	wire io_clr = tp4 & LIO & CY1 | tp7 & OPR & mb[6] | clr_io_on_iot;
/**/	wire mb_io_1 = tp5 & LIO & CY1;
/**/	wire io_shro_l = shro_pulse & ((shro_l & mb[7]) | DIS);
/**/	wire io_shro_r = shro_pulse & ((shro_r & mb[7]) | MUS);
	// EXPANDABLE
/**/	wire [0:17] iom =
		{18{tyi}} & tb |
		{18{rb_to_io}} & rb |
		{18{cks}} & status;
	wire [0:17] status;
	assign status[0:17] = 0;		// TODO
	reg [0:17] io;
	always @(posedge clk) begin
		if(io_clr) io <= 0;
		if(mb_io_1) io <= io | mb;

		if(io_shro_l) io <= { io_0_l, io[2:17], io_17_l };
		if(io_shro_r) io <= { io_0_r, io[0:16] };
		if(iom) io <= io | iom;
	end


	/* IO transfer */
	wire tp7_xx, tp7_0x, tp7_1x, tp7_2x, tp7_3x, tp7_4x, tp7_5x, tp7_6x, tp7_7x;
	wire tp10_xx, tp10_0x, tp10_1x, tp10_2x, tp10_3x, tp10_4x, tp10_5x, tp10_6x, tp10_7x;
	pa pa40(clk, reset, tp7 & iot_ioc, tp7_xx);
	pa pa41(clk, reset, tp7_xx & mbdb[0], tp7_0x);
	pa pa42(clk, reset, tp7_xx & mbdb[1], tp7_1x);
	pa pa43(clk, reset, tp7_xx & mbdb[2], tp7_2x);
	pa pa44(clk, reset, tp7_xx & mbdb[3], tp7_3x);
	pa pa45(clk, reset, tp7_xx & mbdb[4], tp7_4x);
	pa pa46(clk, reset, tp7_xx & mbdb[5], tp7_5x);
	pa pa47(clk, reset, tp7_xx & mbdb[6], tp7_6x);
	pa pa48(clk, reset, tp7_xx & mbdb[7], tp7_7x);
	pa pa50(clk, reset, tp10 & iot_ioc, tp10_xx);
	pa pa51(clk, reset, tp10_xx & mbdb[0], tp10_0x);
	pa pa52(clk, reset, tp10_xx & mbdb[1], tp10_1x);
	pa pa53(clk, reset, tp10_xx & mbdb[2], tp10_2x);
	pa pa54(clk, reset, tp10_xx & mbdb[3], tp10_3x);
	pa pa55(clk, reset, tp10_xx & mbdb[4], tp10_4x);
	pa pa56(clk, reset, tp10_xx & mbdb[5], tp10_5x);
	pa pa57(clk, reset, tp10_xx & mbdb[6], tp10_6x);
	pa pa58(clk, reset, tp10_xx & mbdb[7], tp10_7x);
	// TODO: more
	wire clr_io_on_tyi, rrb, cks;
	wire rpb_iot;
	assign rpb = rpb_iot | rpb_rim;
	pa iopa00(clk, reset, tp10_0x & mbda[1], rpa);
	pa iopa01(clk, reset, tp10_0x & mbda[2], rpb_iot);
	pa iopa02(clk, reset, tp7_0x & mbda[3], clr_tb);
	pa iopa03(clk, reset, tp10_0x & mbda[3], tyo);
	pa iopa04(clk, reset, tp7_0x & mbda[4], clr_io_on_tyi);
	pa iopa06(clk, reset, tp10_0x & mbda[4], tyi);
	pa iopa07(clk, reset, tp7_0x & mbda[5], clr_pb_on_ppa);
	pa iopa08(clk, reset, tp10_0x & mbda[5], ppa);
	pa iopa09(clk, reset, tp7_0x & mbda[6], clr_pb_ppb);
	pa iopa10(clk, reset, tp10_0x & mbda[6], ppb);
	pa iopa11(clk, reset, tp7_0x & mbda[7], clr_db);
	pa iopa12(clk, reset, tp10_0x & mbda[7], dpy);
	pa iopa13(clk, reset, tp10_3x & mbda[0], rrb);
	pa iopa14(clk, reset, tp10_3x & mbda[3], cks);

	wire clr_io_on_iot = clr_io_on_tyi | clr_io_reader | tp7_3x;	// EXTENDABLE

	reg rcp, pcp, tcp, dcp;
	wire nac = IOT & (~mb[5] & mb[6] | mb[5] & ~mb[6]);
	wire rim_or_rcp = rim | rcp;			// TODO: can't find this in drawings
	always @(posedge clk) begin
		if(rpa | rpb) rcp <= nac;
		if(ppa | ppb) pcp <= nac;
		if(tyo) tcp <= nac;
		if(dpy) dcp <= nac;
	end

	wire iot_done;
	pa iopa20(clk, reset,				// EXTENDABLE
		reader_return & rcp |
		punch_done & pcp |
		tyo_done & tcp |
		dpy_done & dcp,
		iot_done);
	wire ext_inc_pc = 0;				// EXTENDABLE

	// EXTENDABLE
	wire set_pf1 = type_sync;
	wire set_pf2 = 0;
	wire set_pf3 = 0;
	wire set_pf4 = 0;
	wire set_pf5 = 0;
	wire set_pf6 = 0;

	/* Extra reader logic */
	reg rbs;
	wire rb_to_io = reader_return & (rcp | rim) | rrb;
	always @(posedge clk) begin
		if(power_clear | rb_to_io) rbs <= 0;
		if(reader_return & ~rcp) rbs <= 1;
	end


	/* Memory - no extension */
	wire mop_2379 = tp2 | tp3 | tp7 | tp9;
	wire mem_clr = power_clear | tp10;
	wire inhibit = i;
	wire read = r;
	wire write = w & ~rs;
	reg r, rs, w, i;
	wire [0:17] mbm = memory[ma];
	reg [0:17] memory[0:4*1024-1];
	always @(posedge clk) begin
		if(mem_clr) begin
			r <= 0;
			rs <= 0;
			w <= 0;
			i <= 0;
		end
		if(mop_2379) begin
			r <= ~rs;
			rs <= r;
			w <= rs;
			i <= w;
		end
		if(tp8)
			i <= 1;
		if(write)
			memory[ma] <= mb;
	end
`ifndef FAST
	wire rs_p, rs_dly;
	pa pa30(clk, reset, rs, rs_p);
	dly300ns dly30(clk, reset, rs_p, rs_dly);
	pa pa31(clk, reset, rs_p, memory_strobe);
`endif

/* TODO stuff */
	wire hsc_mb_clr = 0;

endmodule






/* Pulse Generator,
 * to synchronize "external" pulses. */
module pg(
	input clk,
	input reset,
	input in,
	output p
);
	reg [1:0] x;
	always @(posedge clk or posedge reset)
		if(reset)
			x <= 0;
		else
			x <= { x[0], in };
	assign p = x[0] & !x[1];
endmodule

/* Pulse Amplifier
 * Essentially an edge detector */
module pa(input wire clk, input wire reset, input wire in, output wire p);
	reg [1:0] x;
	reg [1:0] init = 0;
	always @(posedge clk or posedge reset)
		if(reset)
			init <= 0;
		else begin
			x <= { x[0], in };
			init <= { init[0], 1'b1 };
		end
	assign p = (&init) & x[0] & !x[1];
endmodule

module decode8(
	input wire [0:2] in,
	output reg [0:7] out
);
	always @(*) begin
		case(in)
		3'b000: out = 8'b10000000;
		3'b001: out = 8'b01000000;
		3'b010: out = 8'b00100000;
		3'b011: out = 8'b00010000;
		3'b100: out = 8'b00001000;
		3'b101: out = 8'b00000100;
		3'b110: out = 8'b00000010;
		3'b111: out = 8'b00000001;
		endcase
	end
endmodule

