obj_dir/pdp1sim: obj_dir/Vpdp1panel.mk main.cpp
	make -C obj_dir -f Vpdp1panel.mk pdp1sim

obj_dir/Vpdp1panel.mk: pdp1panel.v pdp1.v
	verilator -Wall -O3 -o pdp1sim -cc pdp1panel.v dly_50.v -CFLAGS "-O3 -I../.." -LDFLAGS "-lpthread" --exe --top-module pdp1panel main.cpp ../common.c ../gpio.c ../panel_b18.c
