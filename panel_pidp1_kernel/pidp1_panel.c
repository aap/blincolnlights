#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/err.h>
#include <linux/device.h>

#include <linux/mm.h>

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>

#define nelem(a) (sizeof(a)/sizeof(*a))

#define DEVICE_NAME "pidp1_panel"
#define CLASS_NAME "pidp1"

static dev_t dev = 0;
static struct class *class;
static struct device *device;
static struct cdev cdev;

static struct task_struct *thread;

static int ADDR[] = {4, 17, 27, 22};
static int COLUMNS[] = {26, 19, 13, 6, 5, 11, 9, 10,
        18, 23, 24, 25, 8, 7, 12, 16, 20, 21};
static struct gpio_desc *addr[nelem(ADDR)];
static struct gpio_desc *columns[nelem(COLUMNS)];

struct PDP1_Panel {
	uint8_t lights[10][18];
	int sw[4];
};

static struct PDP1_Panel *panel_mapped;

#include "../pwmtab.inc"

static int
panel_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	if(size > PAGE_SIZE) {
		printk(KERN_ERR "pidp1: mmap size too large\n");
		return -EINVAL;
	}

	if(remap_pfn_range(vma, vma->vm_start,
		virt_to_phys(panel_mapped) >> PAGE_SHIFT, size,
		vma->vm_page_prot)) {
		printk(KERN_ERR "pidp1: remap_pfn_range failed\n");
		return -EAGAIN;
	}
	return 0;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.mmap = panel_mmap,
};

static void
outRow(void)
{
	for(int i = 0; i < nelem(columns); i++)
		gpiod_direction_output(columns[i], 0);
}

static void
inRow(void)
{
	for(int i = 0; i < nelem(columns); i++)
		gpiod_direction_input(columns[i]);
}

/*
static void
setRow(int l)
{
	for(int i = 0; i < nelem(columns); i++)
		gpiod_set_value(columns[i], (l>>i)&1);
}
*/

static void
setRowPWM(uint8_t *l, int phase)
{
	for(int i = 0; i < nelem(columns); i++)
		gpiod_set_value(columns[i], !pwmtable[l[i]][phase]);
}

static void
setAddr(int a)
{
        for(int i = 0; i < nelem(addr); i++)
                gpiod_set_value(addr[i], (a>>i)&1);
}

static int
readRow(void)
{
	int sw = 0777777;
	for(int i = 0; i < nelem(columns); i++)
		if(gpiod_get_value(columns[i]))
			sw &= ~(1<<i);
	return sw;
}

static int
panel_thread(void *data)
{
	static int ledaddr[] = { 0, 1, 2, 3, 4, 5, 6, 12, 13, 14 };
	static int swaddr[] = { 8, 9, 10, 11 };

	sched_set_fifo(current);

	struct PDP1_Panel *p = panel_mapped;

	setAddr(15);
	int swa = 0;
	while(!kthread_should_stop()) {
		outRow();
		for(int phase = 0; phase < 31; phase++)
		for(int a = 0; a < nelem(ledaddr); a++) {
			setRowPWM(p->lights[a], phase);

			setAddr(ledaddr[a]);
			fsleep(20);
			setAddr(15);
			fsleep(5);
		}

		inRow();
		setAddr(swaddr[swa]);
		fsleep(100);
		p->sw[swa] = readRow();
		swa = (swa+1) % nelem(swaddr);

		setAddr(15);
		fsleep(100);
	}
	return 0;
}

static int __init pidp1_panel_init(void)
{
	void *mem = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if(mem == NULL) {
		printk(KERN_ERR "pidp1: failed to allocate page\n");
		goto err0;
	}
	panel_mapped = mem;
	memset(panel_mapped, 0, PAGE_SIZE);

	if(alloc_chrdev_region(&dev, 0, 1, "pidp1_dev") < 0) {
		printk(KERN_ERR "pidp1: failed to create major number\n");
		goto err1;
	}

	cdev_init(&cdev, &fops);
	if(cdev_add(&cdev, dev, 1) < 0) {
		printk(KERN_ERR "pidp1: failed to add cdev\n");
		goto err2;
	}

	class = class_create(THIS_MODULE, CLASS_NAME);
	if(IS_ERR(class)) {
		printk(KERN_ERR "pidp1: failed to create class\n");
		goto err2;
	}

	device = device_create(class, NULL, dev, NULL, DEVICE_NAME);
	if(IS_ERR(device)) {
		printk(KERN_ERR "pidp1: faild to create device\n");
		goto err3;
	}

	// not sure gpio_to_desc is the right thing
	// what about gpiod_get/put?
	for(int i = 0; i < nelem(ADDR); i++) {
		addr[i] = gpio_to_desc(ADDR[i]);
		if(IS_ERR(addr[i])) {
			printk(KERN_ERR "pidp1: can't get pin %d\n", ADDR[i]);
			goto err4;
		}
	}
	for(int i = 0; i < nelem(COLUMNS); i++) {
		columns[i] = gpio_to_desc(COLUMNS[i]);
		if(IS_ERR(columns[i])) {
			printk(KERN_ERR "pidp1: can't get pin %d\n", COLUMNS[i]);
			goto err4;
		}
	}

	for(int i = 0; i < nelem(addr); i++)
		gpiod_direction_output(addr[i], 0);
	for(int i = 0; i < nelem(columns); i++)
		gpiod_set_config(columns[i], PIN_CONFIG_BIAS_PULL_UP);


	thread = kthread_run(panel_thread, NULL, "pidp1_kthread");
	if(IS_ERR(thread)) {
		printk(KERN_ERR "pidp1: faild to create thread\n");
		goto err4;
	}

	return 0;

err4:	for(int i = 0; i < nelem(addr); i++)
		addr[i] = NULL;
	for(int i = 0; i < nelem(columns); i++)
		columns[i] = NULL;
	device_destroy(class, dev);
err3:	class_destroy(class);
err2:	unregister_chrdev_region(dev, 1);
err1:	kfree(mem);
err0:	return -1;
}

static void __exit pidp1_panel_exit(void)
{
	if(thread) {
		kthread_stop(thread);
		thread = NULL;
	}

	device_destroy(class, dev);
	class_destroy(class);
	cdev_del(&cdev);
	unregister_chrdev_region(dev, 1);

	for(int i = 0; i < nelem(addr); i++)
		addr[i] = NULL;
	for(int i = 0; i < nelem(columns); i++)
		columns[i] = NULL;

	kfree(panel_mapped);
}

module_init(pidp1_panel_init);
module_exit(pidp1_panel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("aap");
MODULE_DESCRIPTION("pidp1 panel");
MODULE_VERSION("1.0");
