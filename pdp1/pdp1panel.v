/* verilator lint_off LITENDIAN */

/* verilator lint_off UNUSED */

module pdp1panel(
	input clk,
	input reset,

	input [0:17] sw0,
	input [0:17] sw1,
	output reg [0:17] light0,
	output reg [0:17] light1,
	output wire [0:17] light2,
	output wire sw_power,

	output reg [0:9] dbx,
	output reg [0:9] dby,
	output wire dpy_intensify,

	input [6:0] key,
	output tbb,
	output reg [12:17] tb,
	output wire typeout,

	input [9:1] hole,
	output reg rcl,

	output wire punch,
	output reg [10:17] pb,

	output wire [31:0] ncycle
);
	wire start_sw = sw1[0];
	wire sbm_start_sw = 0;
	wire stop_sw = sw1[1];
	wire continue_sw = sw1[2];
	wire examine_sw = sw1[3];
	wire deposit_sw = sw1[4];
	wire read_in_sw = sw1[5];
	wire mod_sw = sw1[7];

	wire power_sw = sw1[12];
	assign sw_power = power_sw;
	wire single_cyc_sw = sw1[13];
	wire single_inst_sw = sw1[14];

	reg [6:17] ta;
	reg [0:17] tw;
	reg [1:6] ss;

	wire [0:4] ir;
	wire [6:17] pc;
	wire [6:17] ma;
	wire [0:17] mb;
	wire [0:17] ac;
	wire [0:17] io;
	wire [1:6] pf;

	reg run;
	reg cyc;
	reg df1;
	reg df2;
	reg bc1;
	reg bc2;
	reg ov1;
	reg ov2;
	reg rim;
	reg sbm;
	reg ioh;

	reg ioc;
	reg ihs;
	reg ios;
	reg hsc;

	reg r;
	reg rs;
	reg w;
	reg i;

	wire power_clear;
	wire sc;
	pdp1 pdp1(
		.clk(clk),
		.reset(reset),
		.start_sw(start_sw),
		.sbm_start_sw(sbm_start_sw),
		.stop_sw(stop_sw),
		.continue_sw(continue_sw),
		.examine_sw(examine_sw),
		.deposit_sw(deposit_sw),
		.read_in_sw(read_in_sw),
		.power_sw(power_sw),
		.single_cyc_sw(single_cyc_sw),
		.single_inst_sw(single_inst_sw),
.spare1_sw(sw1[16]),
.spare2_sw(sw1[17]),

		.ta(ta),
		.tw(tw),
		.ss(ss),
		.ir(ir),
		.pc(pc),
		.ma(ma),
		.mb(mb),
		.ac(ac),
		.io(io),
		.pf(pf),
		.run(run),
		.cyc(cyc),
		.df1(df1),
		.df2(df2),
		.bc1(bc1),
		.bc2(bc2),
		.ov1(ov1),
		.ov2(ov2),
		.rim(rim),
		.sbm(sbm),
		.ioh(ioh),
		.ioc(ioc),
		.ihs(ihs),
		.ios(ios),
		.hsc(hsc),
		.r(r),
		.rs(rs),
		.w(w),
		.i(i),

		.power_clear(power_clear),
		.sc(sc),

		.dpy(dpy),
		.clr_db(clr_db),
		.dpy_done(dpy_done),

		.tyo(tyo),
		.clr_tb(clr_tb),
		.tyo_done(tyo_done),
		.tyi(tyi),
		.type_sync(type_sync),
		.tb(tb),
		.tyo_ff(tyo_ff),
		.tbs(tbs),

		.rpa(rpa),
		.rpb(rpb),
		.rim_or_rcp(rim_or_rcp),
		.reader_return(reader_return),
		.clr_io_reader(clr_io_reader),
		.rb(rb),

		.ppa(ppa),
		.ppb(ppb),
		.clr_pb_on_ppa(clr_pb_on_ppa),
		.clr_pb_ppb(clr_pb_ppb),
		.punch_done(punch_done),

		.ncycle(ncycle)
	);
	wire [0:17] flags;
	assign flags[0:5] = 0;
	assign flags[6:11] = pf;
	assign flags[12:17] = ss;

	reg [3:0] bits;
	always @(*) begin
		case(sel1)
		2'b00: light0 = ac;
		2'b01: light0 = {ir, 1'b0, ma};
		2'b10: light0 = flags;
		2'b11: light0 = {6'b0, ta};
		endcase

		case(sel2)
		2'b00: light1 = io;
		2'b01: light1 = {6'b0, pc};
		2'b10: light1 = mb;
		2'b11: light1 = tw;
		endcase

		case(sw1[6])
		1'b0: bits = {run, cyc, df1, df2};
		1'b1: bits = {1'b0, rim, ioh, sbm};
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
		if(load1 & ~mod_sw) ta <= sw0[6:17];
		if(load1 & mod_sw) ss <= sw0[12:17];
		if(load2) tw <= sw0;
	end


	/* Display */
	wire dpy;
	wire clr_db;
	wire dpy_done;
	always @(posedge clk) begin
		if(clr_db) begin
			dbx <= 0;
			dby <= 0;
		end
		if(dpy) begin
			dbx <= dbx | ac[0:9];
			dby <= dby | io[0:9];
		end
	end
	reg [0:2] dpyphase = 0;
	always @(posedge clk) begin
		dpyphase <= { 1'b0, dpyphase[0:1] };
		if(dpy) dpyphase <= 3'b100;
	end
	assign dpy_intensify = dpyphase[1];
	assign dpy_done = dpyphase[2];



	/* Typewriter */
	wire color = tb[12:16] == 5'b01110;
	wire shift = tb == 6'o74 | tb == 6'o72;
	wire cr = tb == 6'o77;
	wire tyo;
	wire clr_tb;
	wire tyo_done = type_return_done & tyo_ff & ~cr & ~shift | tyo_d2 | tyo_d4;
	wire tyi;
	wire type_return = type_returnA | type_returnB;
	wire strobe_type = tyiphase[1];
	wire type_return_done = tyiphase[3];
	wire type_sync = type_return_done & ~tyo_ff;
	reg tbs;
	reg tbb;
	reg tyo_ff;
	reg [12:17] tb;
	reg tyo_d1;
	wire tyo_d2 = tyophase[6];
	wire tyo_d4 = tyiphase[4] & tyo_ff & cr;
	reg [0:6] tyophase = 0;
	reg [0:6] tyiphase = 0;
	assign typeout = tyo_d1 & ~color;
	wire type_returnA, type_returnB;
	edgedet tye0(clk, reset, key[6], type_returnA);
	edgedet tye1(clk, reset, typeout, type_returnB);
	always @(posedge clk) begin
		tyo_d1 <= tyo;
		tyophase <= { tyo_d1 & (color | shift), tyophase[0:5] };
		tyiphase <= { type_return, tyiphase[0:5] };

		if(power_clear) begin
			tbs <= 0;
			tbb <= 0;
			tyo_ff <= 0;
		end
		if(sc | tyo_done) tyo_ff <= 0;
		if(clr_tb & ~tyo_ff | type_return) tb <= 0;
		if(tyo & ~tyo_ff) begin
			tyo_ff <= 1;
			tb <= tb | io[12:17];
		end
		if(tyo_d1) begin
			if(color & tb[17]) tbb <= 1;
			if(color & ~tb[17]) tbb <= 0;
		end
		if(strobe_type) tb <= tb | key[5:0];
		if(type_return_done & ~tyo_ff)
			tbs <= 1;
	end


	/* Reader */
	wire rpa;
	wire rpb;
	wire rim_or_rcp;
	wire reader_return = inc_rc & (rc == 2'b11);
	wire clear_rb = rpa | rpb;
	reg [2:1] rc;
	reg rby;
	reg [0:17] rb;
	wire feed_hole;
	wire strobe_petr = feed_hole & (rc != 0) & (~rby | hole[8]);
	reg strobe_petr_dly;
	wire inc_rc = strobe_petr_dly;
	wire shift_rb = strobe_petr_dly & (rc != 2'b11);
	wire clr_io_reader = strobe_petr_dly & rim_or_rcp;
	edgedet re1(clk, reset, hole[9], feed_hole);
	always @(posedge clk) begin
		strobe_petr_dly <= strobe_petr;
		if(power_clear) begin
			rc <= 0;
			rby <= 0;
			rcl <= 0;
		end
		if(rpa) begin
			rc <= 2'b11;
			rby <= 0;
			rcl <= ~rcl;
		end
		if(rpb) begin
			rc <= 2'b01;
			rby <= 1;
			rcl <= 1;
		end
		if(inc_rc)
			rc <= rc + 1;
		if(clear_rb)
			rb <= 0;
		if(strobe_petr) begin
			rb[12:17] <= rb[12:17] | hole[6:1];
			if(~rby)
				rb[10:11] <= rb[10:11] | hole[8:7];
			rcl <= 0;
		end
		if(shift_rb) begin
			rb <= { rb[6:17], 6'b0 };
			rcl <= 1;
		end
	end


	/* Punch */
	wire ppa;
	wire ppb;
	wire clr_pb_on_ppa;
	wire clr_pb_ppb;
	wire punch_done = punchphase[5];
	assign punch = punchphase[4];
	reg [0:5] punchphase = 0;
	wire punon = punchphase != 0;
	always @(posedge clk) begin
		punchphase <= { 1'b0, punchphase[0:4] };
		if(clr_pb_on_ppa | clr_pb_ppb) begin
			pb <= 0;
			punchphase <= 6'b100000;
		end
		if(ppa)
			pb <= pb | io[10:17];
		if(ppb)
			pb <= pb | { 2'b10, io[0:5] };
	end

endmodule
