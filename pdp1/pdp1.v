/* verilator lint_off LITENDIAN */

/* verilator lint_off DECLFILENAME */
/* verilator lint_off WIDTH */
/* verilator lint_off UNUSED */

module pdp1(
	input wire clk,
	input wire reset,

	input wire start_sw,
	input wire sbm_start_sw,
	input wire stop_sw,
	input wire continue_sw,
	input wire examine_sw,
	input wire deposit_sw,
	input wire read_in_sw,

	input wire power_sw,
	input wire single_cyc_sw,
	input wire single_inst_sw,

	input wire [6:17] ta,
	input wire [0:17] tw,
	input wire [6:1] ss,

	output reg [0:4] ir,
	output reg [6:17] pc,
	output reg [6:17] ma,
	output reg [0:17] mb,
	output reg [0:17] ac,
	output reg [0:17] io,
	output reg [6:1] pf,

	output reg run,
	output reg cyc,
	output reg df1,
	output reg df2,
	output reg bc1,
	output reg bc2,
	output reg ov1,
	output reg ov2,
	output reg rim,
	output reg sbm,
	output reg ioh,

	output reg ioc,
	output reg ihs,
	output reg ios,
	output reg hsc,

	output reg r,
	output reg rs,
	output reg w,
	output reg i
);
	// TODO: this is wrong
	wire power_clear = reset;

	/*
	 * Basic control and timing
	 */

	wire sp1, sp1_dly, sp2, sp2_dly, sp3, sp3_dly, sp4;
	wire sc;

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
/**/	pa pa2(clk, reset, pb_dly, sp1);
/**/	pa pa3(clk, reset, sp1_dly & ~rim, sp2);
	pa pa4(clk, reset, sp2_dly, sp3);
	pa pa5(clk, reset, sp3_dly, sp4);

	pa pa6(clk, reset, sp1 & (start_sw | examine_sw | deposit_sw | rim), sc);

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
/**/	pa pa10(clk, reset, tp10_dly & run | sp4 & (continue_sw | any_start_sw), tp0);
	pa pa11(clk, reset, tp0_dly, tp1);
/**/	pa pa12(clk, reset, tp1_dly | sp4 & (examine_sw|deposit_sw), tp2);
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

	wire inst_done = (ir[0]&ir[1] & (~cyc & ~df1 | DF & ~df2) | CY1) & ~BC1_BC2_BC3;
	wire manual_run = single_cyc_sw | single_inst_sw & inst_done;

//	reg run;
	reg run_enable;
	wire halt_cond = OPR & mb[9] | manual_run | incorr_op_sel | ~run_enable;
	always @(posedge clk) begin
		if(start | stop | cntinue | examine | deposit | read_in)
			run_enable <= 0;
		if(sp1)
			run_enable <= 1;
// TODO: can't make sense of run_enable at all
// but this seems like it's doing the right thing?
		if(~run_enable) run <= 0;
		if(sp4 & (any_start_sw | continue_sw | JMP_rim))
			run <= 1;
		if(tp9 & halt_cond)
			run <= 0;
	end

//	reg cyc;
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

//	reg df1, df2;
	always @(posedge clk) begin
		if(sc | tp10 & DF & ~df2 | pc_dec)
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

//	reg sbm;
	always @(posedge clk) begin
		if(sc) sbm <= sbm_start_sw;
	end

//	reg hsc;
	always @(posedge clk) begin
		if(sc) hsc <= 0;
	end

//	reg bc2, bc1;
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

//	reg ioc;
//	reg ihs;
//	reg ios;
//	reg ioh;
	always @(posedge clk) begin
		if(sc) begin
			ioc <= 0;
			ihs <= 0;
			ios <= 0;
			ioh <= 0;
		end
	end

	/*
	 * Instruction register
	 */

//	reg [0:4] ir;
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

//	reg rim;
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

//	reg [6:1] pf;
	wire [0:7] mbda;
	wire [0:7] mbdb;
	decode8 deca(mb[15:17], mbda);
	decode8 decb(mb[12:14], mbdb);
	always @(posedge clk)
		if(tp8 & OPR) begin
			if(mbda[6] | mbda[7]) pf[6] <= mb[14];
			if(mbda[5] | mbda[7]) pf[5] <= mb[14];
			if(mbda[4] | mbda[7]) pf[4] <= mb[14];
			if(mbda[3] | mbda[7]) pf[3] <= mb[14];
			if(mbda[2] | mbda[7]) pf[2] <= mb[14];
			if(mbda[1] | mbda[7]) pf[1] <= mb[14];
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
	wire mb_pc = ~cyc & ~df1 & JMP_JSP | DF & ~df2 & JMP_JSP;
	wire pc_clr = sc | tp8 & (mb_pc | BC1_JDA_CY1);
	wire mb_pc_1 = sp4 & JMP_rim | tp9 & mb_pc;
	wire ta_pc_1 = sp2 & (any_start_sw | deposit_sw | examine_sw);
	wire ma_pc_1 = tp9 & BC1_JDA_CY1;
/**/	wire pc_inc = tp2 & ~cyc | tp8 & (cy1_skip | SKP & (enable != mb[5])) |
		tp9a & (BC1_BC2_BC3 | BC1_JDA_CY1);
/**/	wire pc_dec = 0;
//	reg [6:17] pc;
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
//	reg [6:17] ma;
	always @(posedge clk) begin
		if(ma_clr) ma <= 0;
		if(mb_ma_1) ma <= ma | mb[6:17];
		if(pc_ma_1) ma <= ma | pc;
		if(set_ma & ma_100) ma[11] <= 1;
// TODO: BE, HSAM
	end


	/* MB */
/**/	wire mb_clr = tp3 | tp5 & (CY1 & DZM | CY1 & DIO | BC3) | hsc_mb_clr;
/**/	wire ac_mb_0_5 = tp7 & (CY1 & (DIP | DAC | IDX | ISP | JDA) | BC1 | BC2);
/**/	wire ac_mb_6_17 = tp7 & (CY1 & (DAP | DAC | IDX | ISP | JDA) | BC1 | BC2);
/**/	wire io_mb_1 = sp2 & rim | tp7 & (CY1 & DIO | BC3);
//	reg [0:17] mb;
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
	wire pc_ac = JSP & ~cyc & ~df1 | JSP & DF & ~df2 | BC1_JDA_CY1;
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
//	reg [0:17] ac;
	reg cry_wrap;
	wire [0:17] cry_in = ~ac & mb;
	wire ac_pos_zero = ac == 0;
	wire ac_neg_zero = ac == 'o777777;
	wire ac_neg_one = ac == 'o777776;
//	reg ov1, ov2;
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
/**/	wire io_clr = tp4 & LIO & CY1 | tp7 & OPR & mb[6];
/**/	wire mb_io_1 = tp5 & LIO & CY1;
/**/	wire io_shro_l = shro_pulse & ((shro_l & mb[7]) | DIS);
/**/	wire io_shro_r = shro_pulse & ((shro_r & mb[7]) | MUS);
//	reg [0:17] io;
	always @(posedge clk) begin
		if(io_clr) io <= 0;
		if(mb_io_1) io <= io | mb;

		if(io_shro_l) io <= { io_0_l, io[2:17], io_17_l };
		if(io_shro_r) io <= { io_0_r, io[0:16] };
	end


	/* Memory - no extension */
	wire mop_2379 = tp2 | tp3 | tp7 | tp9;
	wire mem_clr = power_clear | tp10;
	wire inhibit = i;
	wire read = r;
	wire write = w & ~rs;
//	reg r, rs, w, i;
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
	wire rs_p, rs_dly;
	pa pa30(clk, reset, rs, rs_p);
	dly300ns dly30(clk, reset, rs_p, rs_dly);
	pa pa31(clk, reset, rs_p, memory_strobe);

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

