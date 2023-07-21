/* verilator lint_off DECLFILENAME */

module dly200ns(input clk, input reset, input in, output p);
	reg [4-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 4'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 10;
endmodule


module dly250ns(input clk, input reset, input in, output p);
	reg [4-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 4'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 12;
endmodule


module dly300ns(input clk, input reset, input in, output p);
	reg [4-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 4'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 15;
endmodule


module dly400ns(input clk, input reset, input in, output p);
	reg [5-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 5'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 20;
endmodule


module dly550ns(input clk, input reset, input in, output p);
	reg [5-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 5'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 27;
endmodule


module dly750ns(input clk, input reset, input in, output p);
	reg [6-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 6'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 37;
endmodule


module dly1us(input clk, input reset, input in, output p);
	reg [6-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 6'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 50;
endmodule


module dly1_2us(input clk, input reset, input in, output p);
	reg [6-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 6'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 60;
endmodule


module dly2us(input clk, input reset, input in, output p);
	reg [7-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 7'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 100;
endmodule


module dly100us(input clk, input reset, input in, output p);
	reg [13-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 13'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 5000;
endmodule


module dly500us(input clk, input reset, input in, output p);
	reg [15-1:0] r;
	always @(posedge clk or posedge reset) begin
		if(reset)
			r <= 0;
		else begin
			if(r != 0)
				r <= r + 15'b1;
			if(in)
				r <= 1;
		end
	end
	assign p = r == 25000;
endmodule

