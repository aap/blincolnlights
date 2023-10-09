
module edgedet(clk, reset, in, p);
	input wire clk;
	input wire reset;
	input wire in;
	output wire p;

	reg [1:0] x;
	reg [1:0] init = 0;
	always @(posedge clk or negedge reset) begin
		if(reset)
			init <= 0;
		else begin
			x <= { x[0], in };
			init <= { init[0], 1'b1 };
		end
	end

	assign p = (&init) & x[0] & !x[1];
endmodule

