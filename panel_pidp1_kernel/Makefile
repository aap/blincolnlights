obj-m += pidp1_panel.o

KDIR = /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(shell pwd) modules

clean:
	make -C $(KDIR) M=$(shell pwd) clean

load:
	sudo insmod pidp1_panel.ko
	sudo chmod 666 /dev/pidp1_panel

unload:
	sudo rmmod pidp1_panel.ko
