obj_dir/tx0sim: obj_dir/Vtx0panel.mk main.cpp
	make -C obj_dir -f Vtx0panel.mk tx0sim

obj_dir/Vtx0panel.mk: tx0panel.v tx0.v
	verilator -Wall -O3 -o tx0sim -cc tx0panel.v edgedet.v -CFLAGS "-O3 -I../.." -LDFLAGS "-lpigpio -lpthread" --exe main.cpp ../common.c

