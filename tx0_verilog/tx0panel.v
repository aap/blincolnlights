/* verilator lint_off LITENDIAN */

/* verilator lint_off UNUSED */

module tx0panel(
	input wire clk,
	input wire reset,

	input wire [0:17] sw0,
	input wire [0:17] sw1,
	output reg [0:17] light0,
	output reg [0:17] light1,
	output wire [0:17] light2,
	output wire sw_power,

	input wire flexo_complete,
	input wire [0:5] flexo_in,
	input wire flexo_to_lr,
	output wire flexo_start_print,
	output wire flexo_start_punch,
	output wire [0:5] flexo_out,
	output wire flexo_7th_hole,

	input wire display_complete,
	output wire display_start,
	output wire [0:8] display_x,
	output wire [0:8] display_y
);
	reg [0:17] tac = 0;
	reg [0:17] tbr = 0;
	assign sw_power = sw1[12];
	wire sw_sup_alarm = 0;
	wire sw_sup_chime = 0;
	wire sw_auto_restart = 0;
	wire sw_auto_readin = 0;
	wire sw_auto_test = 0;
	wire sw_stop_c0 = sw1[13];
	wire sw_stop_c1 = sw1[14];
	wire sw_step = sw1[16];
	wire sw_repeat = sw1[15];
	wire sw_cm_sel = 0;
	wire sw_type_in = 0;
	wire btn_test = sw1[0];
	wire btn_readin = sw1[5];
	wire btn_stop = sw1[1];
	wire btn_restart = sw1[2];

	wire [0:17] ac;
	wire [2:17] mar;
	wire [0:1] ir;
	wire [0:17] lr;
	wire [2:17] pc;
	wire [0:17] mbr;
	wire c;
	wire r;
	wire t;
	wire par;
	wire ss;
	wire pbs;
	wire ios;
	wire ch;
	wire aff;
	wire [0:1] lp;
	wire [14:17] tss_addr;	// TSS unused for now

tx0 tx0(
		.clk(clk),
		.reset(reset),
		.tac(tac),
		.tbr(tbr),
		.sw_sup_alarm(sw_sup_alarm),
		.sw_sup_chime(sw_sup_chime),
		.sw_auto_restart(sw_auto_restart),
		.sw_auto_readin(sw_auto_readin),
		.sw_auto_test(sw_auto_test),
		.sw_stop_c0(sw_stop_c0),
		.sw_stop_c1(sw_stop_c1),
		.sw_step(sw_step),
		.sw_repeat(sw_repeat),
		.cm_sel(1'b1),
		.sw_type_in(sw_type_in),
		.btn_test(btn_test),
		.btn_readin(btn_readin),
		.btn_stop(btn_stop),
		.btn_restart(btn_restart),
		.btn_tape_feed(1'b0),
		.sw_tss(0),
		.sw_tss_cmlr(0),
		.tss_addr(tss_addr),
		.ir(ir),
		.c(c),
		.r(r),
		.t(t),
		.par(par),
		.ss(ss),
		.ios(ios),
		.pbs(pbs),
		.lp(lp),
		.ch(ch),
		.aff(aff),
		.ac(ac),
		.lr(lr),
		.mbr(mbr),
		.pc(pc),
		.mar(mar),

		.flexo_complete(flexo_complete_edge),
		.flexo_in(flexo_in),
		.flexo_to_lr(flexo_to_lr_edge),
		.flexo_start_print(flexo_start_print),
		.flexo_start_punch(flexo_start_punch),
		.flexo_out(flexo_out),
		.flexo_7th_hole(flexo_7th_hole),

		.display_complete(display_complete_edge),
		.display_start(display_start),
		.display_x(display_x),
		.display_y(display_y)
);
	wire [0:17] flags = 0;

	reg [3:0] bits;
	always @(*) begin
		case(sel1)
		2'b00: light0 = ac;
		2'b01: light0 = {ir, mar};
		2'b10: light0 = flags;
		2'b11: light0 = tac;
		endcase

		case(sel2)
		2'b00: light1 = lr;
		2'b01: light1 = {2'b00, pc};
		2'b10: light1 = mbr;
		2'b11: light1 = tbr;
		endcase

		case(sw1[6])
		1'b0: bits = {ss, ios, pbs, 1'b0};
		1'b1: bits = {1'b0, c, r, t};
		endcase
	end


	assign light2[12:17] = sw1[12:17];
	assign light2[8:11] = bits;
	assign light2[4:7] = 'b1000 >> sel1;
	assign light2[0:3] = 'b1000 >> sel2;

	reg [1:0] sel1 = 0;
	reg [1:0] sel2 = 0;
	wire cyc1, load1;
	wire cyc2, load2;
	edgedet e0(clk, reset, sw1[8], cyc1);
	edgedet e1(clk, reset, sw1[9], load1);
	edgedet e2(clk, reset, sw1[10], cyc2);
	edgedet e3(clk, reset, sw1[11], load2);

	always @(posedge clk) begin
		if(cyc1) sel1 <= sel1 + 1;
		if(cyc2) sel2 <= sel2 + 1;
		if(load1) tac <= sw0;
		if(load2) tbr <= sw0;
	end

	wire flexo_complete_edge;
	wire flexo_to_lr_edge;
	edgedet fe0(clk, reset, flexo_complete, flexo_complete_edge);
	edgedet fe1(clk, reset, flexo_to_lr, flexo_to_lr_edge);

	wire display_complete_edge;
	edgedet de0(clk, reset, display_complete, display_complete_edge);
endmodule
