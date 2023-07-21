/* verilator lint_off LITENDIAN */

/* verilator lint_off DECLFILENAME */
/* verilator lint_off WIDTH */
/* verilator lint_off UNUSED */

module tx0(
	input wire clk,
	input wire reset,

	/* switches */
	input wire [0:17] tac,
	input wire [0:17] tbr,
	input wire sw_sup_alarm,
	input wire sw_sup_chime,
	input wire sw_auto_restart,
	input wire sw_auto_readin,
	input wire sw_auto_test,
	input wire sw_stop_c0,
	input wire sw_stop_c1,
	input wire sw_step,
	input wire sw_repeat,
	input wire cm_sel,
	input wire sw_type_in,
	/* push buttons */
	input wire btn_test,
	input wire btn_readin,
	input wire btn_stop,
	input wire btn_restart,
	input wire btn_tape_feed,
	input wire [0:17] sw_tss,
	input wire [0:1] sw_tss_cmlr,
	output wire [14:17] tss_addr,

        output reg [0:1] ir,
        output reg c,
        output reg r,
        output reg t,
        output reg par,
        output reg ss,
        output reg ios,
        output reg pbs,
        output reg [0:1] lp,
        output reg ch,
        output reg aff,

        output reg [0:17] ac,
        output reg [0:17] lr,
        output reg [0:17] mbr,
        output reg [2:17] pc,
        output reg [2:17] mar,

	/* flexowriter */
	input wire flexo_complete,
	input wire [0:5] flexo_in,
	input wire flexo_to_lr,
	output wire flexo_start_print,
	output wire flexo_start_punch,
	output wire [0:5] flexo_out,
	output wire flexo_7th_hole,

	/* display */
	input wire display_complete,
	output wire display_start,
	output wire [0:8] display_x,
	output wire [0:8] display_y
);
	wire test_pulse;
	wire readin_pulse;
	wire stop_pulse;
	wire restart_pulse;
	edgedet pb1(clk, reset, btn_test, test_pulse);
	edgedet pb2(clk, reset, btn_readin, readin_pulse);
	edgedet pb3(clk, reset, btn_stop, stop_pulse);
	edgedet pb4(clk, reset, btn_restart, restart_pulse);

	/* Chime */
//	reg ch = 0;
	initial ch = 0;
	wire ch_clr = 0;	// TODO
	wire ch_set = halt;
	always @(posedge clk) begin
		if(ch_clr) ch <= 0;
		if(ch_set) ch <= 1;
	end
	/* Alarm */
//	reg aff = 0;
	initial aff = 0;
	wire aff_clr = tp1 & ss;
	wire aff_set = tp6 & ss & par_error;	// TODO: same in C1?
	always @(posedge clk) begin
		if(aff_clr) aff <= 0;
		if(aff_set) aff <= 1;
	end

	/* Timing */
	reg [0:2] t_cnt = 0;
	always @(posedge clk)
		t_cnt <= t_cnt + 1;
	wire tp1 = t_cnt == 0;
	wire tp2 = t_cnt == 1;
	wire tp3 = t_cnt == 2;
	wire tp4 = t_cnt == 3;
	wire tp5 = t_cnt == 4;
	wire tp6 = t_cnt == 5;
	wire tp7 = t_cnt == 6;
	wire tp8 = t_cnt == 7;

	/* Control */
//	reg [0:1] ir;
	wire ir_comp = tp6 & c0 & r & ir[1];
	wire mbr_to_ir = tp5 & c0;
	wire tn_to_ir = readin_pulse;
	always @(posedge clk) begin
		if(ir_comp) ir <= ~ir;
		if(mbr_to_ir) ir <= mbr[0:1];
		if(tn_to_ir) ir <= 2'b10;
	end
	wire ir_st = ir == 0;
	wire ir_ad = ir == 1;
	wire ir_tn = ir == 2;
	wire ir_op = ir == 3;
	wire op8 = tp8 & c0 & ~aff & ir_op;
	// Modes
//	reg c = 0;	// cycle
//	reg r = 0;	// read mode
//	reg t = 0;	// test mode
	initial begin
		c = 0;
		r = 0;
		t = 0;
	end
	wire c_clr = tp8 & c1 & (~t | r | ir_op & sw_repeat);
	wire c_set = tp8 & c0 & (~t & ~ac[0] | ~ir_tn);
	wire set_normal = tp8 & c0 & ir_tn;
	always @(posedge clk) begin
		if(c_clr) c <= 0;
		if(c_set) c <= 1;
		if(test_pulse) begin
			c <= 0;
			r <= 0;
			t <= 1;
		end
		if(readin_pulse) begin	
			c <= 1;
			r <= 1;
			t <= 1;
		end
		if(set_normal) begin
			r <= 0;
			t <= 0;
		end
	end
	wire normal = ~t;
	wire test = ~r & t;
	wire read = r & t;
	wire c0 = ss & ~c;
	wire c1 = ss & c;

//	reg ss = 0;	// start-stop (0 stop; 1 start)
//	reg ios = 0;	// IO stop
//	reg pbs = 0;	// push button synch
	initial begin
		ss = 0;
		ios = 0;
		pbs = 0;
	end
	wire io_stop = op8 & mar[4] & ~mar[5] | read_read3;
	wire io_restart = flexo_complete | display_complete;	// TODO
	wire ss_clr = tp8 & ~pbs;
	wire ss_set = tp8 & ~ss & ios & pbs;
	wire pbs_clr = halt;	// TODO
	wire halt = tp6 &
		(c0 & (r & ir_ad | sw_stop_c0) |
		 c1 & (test & ~sw_repeat | sw_stop_c1 | ir_op & mar[4] & mar[5]) |
		 ss & par_error);	// TODO: same in C1?
	wire read_read3 = tp8 & (c0 & read & ir_st | c1 & read);
	always @(posedge clk) begin
		if(test_pulse | stop_pulse | restart_pulse)
			pbs <= 1;
		if(stop_pulse)
			pbs <= 0;
		if(test_pulse | readin_pulse | restart_pulse)
			ios <= 1;
		if(io_stop) begin
			ios <= 0;
			ss <= 0;
		end
		if(io_restart) begin
			ios <= 1;
		end
		if(ss_clr) ss <= 0;
		if(ss_set) ss <= 1;
		if(pbs_clr) pbs <= 0;
	end

	/* AC */
//	reg [0:17] ac = 'o000120;
	initial ac = 0;
	reg cry_wrap;
	wire [0:17] cry_in = ~ac & mbr;
	wire tac_to_ac = tp1 & c1 & (test & ir_st | ir_op & ~mar[11] & mar[15]);
	wire strobe_petr = 0;	// TODO
	wire read_light_pen = tp1 & c1 & ir_op & mar[11] & ~mar[15];
	wire ac_clr_l = tp8 & (c0 & (~aff & ir_op & mar[2] | t & ir_st) | c1 & read);
	wire ac_clr_r = tp8 & (c0 & (~aff & ir_op & mar[3] | t & ir_st) | c1 & read);
	wire ac_comp = tp2 & c1 & ir_op & mar[12];
	wire pad = tp4 & c1 & (ir_ad | ir_op & mar[13]);
	wire carry = tp7 & c1 & (ir_ad | ir_op & mar[14]);
	wire cycle = ss & mar[10];
	wire shift = tp4 & c1 & ir_op & mar[9];	// TODO: petr
	wire [0:17] shift_in = { cycle ? ac[17] : ac[0], ac[0:16] };
	always @(posedge clk) begin
		cry_wrap <= 0;
		if(ac_clr_l) ac[0:8] <= 0;
		if(ac_clr_r) ac[9:17] <= 0;
		if(ac_comp) ac <= ~ac;
		if(tac_to_ac) ac <= ac | tac;
		if(pad) ac <= ac ^ mbr;
		if(carry) { cry_wrap, ac } <= ac + { cry_in[1:17], cry_in[0] };
		if(cry_wrap) ac <= ac + 1;
		if(shift) ac <= shift_in;
		// Not quite sure if ones or jam
		if(strobe_petr) {ac[0],ac[3],ac[6],ac[9],ac[12],ac[15]} <= petr;
		if(read_light_pen) ac[0:1] <= lp;
	end

	/* LR */
//	reg [0:17] lr = 0;
	initial lr = 0;
	wire mbr_to_lr = tp3 & c1 & ir_op & ~mar[9] & mar[10];
	always @(posedge clk) begin
		if(mbr_to_lr) lr <= mbr;
		if(flexo_to_lr) lr <= { 1'b1, flexo_in, 11'b0 };
	end

	/* MBR */
//	reg [0:17] mbr = 'o000000;
//	reg par = 0;
	initial begin
		mbr = 0;
		par = 0;
	end
	wire parity = (^mbr);
	wire parity_odd = parity ^ par;
	wire mbr_clr = tp1 & ss;
	wire tbr_to_mbr = tp2 & (c0 & test | c1 & ir_op & mar[17] & mar[16]);
	wire ac_to_mbr = tp2 & (c0 & read | c1 & (ir_st | ir_op & mar[17] & ~mar[16]));
	wire read_pulse = tp3 & (c0 & ~t | c1 & ir_ad);
	wire lr_to_mbr = read_pulse & sel_tss & sel_lr |
		tp3 & c1 & ir_op & mar[16] & ~mar[17];
	wire tss_to_mbr = read_pulse & sel_tss & ~sel_lr;
	wire sa_strobe = read_pulse & ~sel_tss;
	wire correct_parity = tp3 & (c0 & t | c1 & ~ir_ad);
	always @(posedge clk) begin
		if(mbr_clr) {par, mbr} <= 0;
		// probably transferring 1s here
		if(tbr_to_mbr) mbr <= mbr | tbr;
		if(ac_to_mbr) mbr <= mbr | ac;
		if(lr_to_mbr) mbr <= mbr | lr;
		if(tss_to_mbr) mbr <= mbr | tss;
		if(sa_strobe) {par, mbr} <= {par,mbr} | sa;
		if(correct_parity) par <= ~parity;
	end

	/* PC */
//	reg [2:17] pc = 'o000000;
	initial pc = 0;
	wire pc_inc = c0 | c1 & t;
	wire pc_clr = tp4 & pc_inc;
	wire mar_inc_to_pc = tp6 & pc_inc;
	always @(posedge clk) begin
		if(pc_clr) pc <= 0;
		if(mar_inc_to_pc) pc <= pc | (mar+1);
	end

	/* MAR */
//	reg [2:17] mar = 'o000000;
	initial mar = 0;
	wire pc_to_mar = tp7 & c1 & ~aff & (~t | sw_step);
	wire mbr_to_mar = tp7 & c0;
	always @(posedge clk) begin
		if(pc_to_mar) mar <= pc;
		if(mbr_to_mar) mar <= mbr;
	end

	/* Storage */
	wire sel_tss = ~(cm_sel | sw_tss_cmlr[0]);
	wire sel_lr = sw_tss_cmlr[1];
	// read (why two?)
	reg mrffu = 0;
	reg mrffv = 0;
	// inhibit
	reg miff = 0;
	wire memaccess = c0 & ~t | c1 & ~ir[0];
	wire mrffu_clr = tp5;
	wire mrffv_clr = tp2 & memaccess;
	wire mrff_set = tp1 & memaccess;
	wire miff_clr = tp7;
	wire miff_set = tp4 & memaccess;
	wire par_error = 0; //~parity_odd & ~sw_sup_alarm & ~sel_tss;			// TODO
	always @(posedge clk) begin
		if(mrffu_clr) mrffu <= 0;
		if(mrffv_clr) mrffv <= 0;
		if(mrff_set) begin
			mrffu <= 1;
			mrffv <= 1;
		end
		if(miff_clr) miff <= 0;
		if(miff_set) miff <= 1;
		if(miff) core[mar] <= {par, mbr};
	end
	assign tss_addr = mar;
	wire [0:17] tss = sw_tss;
	wire [-1:17] sa = core[mar];
	reg [-1:17] core[0:0177777];


	/* Flexowriter */
	assign flexo_out = { ac[2], ac[5], ac[8], ac[11], ac[14], ac[17] };
	assign flexo_7th_hole = mar[8];
	assign flexo_start_print = op8 & mar[6] & ~mar[7] & ~mar[8];
	assign flexo_start_punch = op8 & mar[6] & mar[7];

	/* Display - Pen */
	assign display_start = op8 & ~mar[6] & mar[7] & ~mar[8];
	assign display_x = ac[0:8];
	assign display_y = ac[9:17];
//	reg [0:1] lp = 2'b11;
	initial lp = 2'b00;






	/* PETR */
	reg [0:5] petr = 6'b111111;
	wire read_1_line = op8 & ~mar[6] & ~mar[7] & mar[8];
	wire read_3_lines = op8 & ~mar[6] & mar[7] & mar[8] | read_read3;

	/* Punch */
	wire start_punch = op8 & mar[6] & mar[7];

	/* Typewriter */
	wire start_print = op8 & mar[6] & ~mar[7] & ~mar[8];

	wire spare = op8 & mar[6] & ~mar[7] & mar[8];
endmodule
