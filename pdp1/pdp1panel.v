/* verilator lint_off LITENDIAN */

/* verilator lint_off UNUSED */

module pdp1panel(
	input wire clk,
	input wire reset,

	input wire [0:17] sw0,
	input wire [0:17] sw1,
	output reg [0:17] light0,
	output reg [0:17] light1,
	output wire [0:17] light2,
	output wire sw_power
);
	wire start_sw = sw1[0];
	wire sbm_start_sw = 0;
	wire stop_sw = sw1[1];
	wire continue_sw = sw1[2];
	wire examine_sw = sw1[3];
	wire deposit_sw = sw1[4];
	wire read_in_sw = sw1[5];

	wire power_sw = sw1[12];
	assign sw_power = power_sw;
	wire single_cyc_sw = sw1[13];
	wire single_inst_sw = sw1[14];

	reg [6:17] ta;
	reg [0:17] tw;
	reg [6:1] ss = 0;

	wire [0:4] ir;
	wire [6:17] pc;
	wire [6:17] ma;
	wire [0:17] mb;
	wire [0:17] ac;
	wire [0:17] io;
	wire [6:1] pf;

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
		.i(i)
	);
	wire [0:17] flags = 0;

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
		if(load1) ta <= sw0[6:17];
		if(load2) tw <= sw0;
	end
endmodule
