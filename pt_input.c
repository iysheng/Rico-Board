#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/input.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("iysheng<iysheng@163.com>");
#define PT_MAJOR 0
#define PT_COUNT 1
#define GPIO_PT2262 4
#define BUF_LEN 8
#define PT_NAME "pt2262"
int pt_major = PT_MAJOR;
dev_t pt_dev_num = MKDEV(PT_MAJOR,0);

struct input_dev *pt_input_dev;
int gpio_pt2262[GPIO_PT2262]={49,60,51,50};

int irq_gpio[GPIO_PT2262];


irqreturn_t pt2262_isr(int irq, void *gpio_num)
{
	int i;
	int *gpio_num_tmp = (int *)gpio_num;
	for(i=0; i<GPIO_PT2262; i++)
	{
		if(gpio_pt2262[i] != *gpio_num_tmp)
			continue;
		switch(i)
		{
			case 0:
					input_event(pt_input_dev, EV_KEY, KEY_A, 0);input_event(pt_input_dev, EV_SYN, 0, 0);printk(KERN_INFO "success A.\n");break;
			case 1:
					input_event(pt_input_dev, EV_KEY, KEY_B, 0);input_event(pt_input_dev, EV_SYN, 0, 0);printk(KERN_INFO "success B.\n");break;
			case 2:
					input_event(pt_input_dev, EV_KEY, KEY_C, 0);input_event(pt_input_dev, EV_SYN, 0, 0);printk(KERN_INFO "success C.\n");break;
			case 3:
					input_event(pt_input_dev, EV_KEY, KEY_D, 0);input_event(pt_input_dev, EV_SYN, 0, 0);printk(KERN_INFO "success D.\n");break;
			default:
				break;
		}
	}
	return IRQ_HANDLED;
}

int __init pt_init(void)
{
	int ret = 0;
	int i = 0;
	char gpio_name_buf[BUF_LEN];
	/*
	if(0 == pt_major)
		ret = alloc_chrdev_region(&pt_dev_num, 0, PT_COUNT, PT_NAME);
	else
		register_chrdev_region(pt_dev_num, PT_COUNT, PT_NAME);
	if(ret < 0)
		goto fail;
	pt_major = MAJOR(pt_dev_num);
	*/

	/*分配一个输出设备*/
	pt_input_dev = input_allocate_device();
	/*设置改输入设备的一些成员变量*/
	set_bit(EV_KEY, pt_input_dev->evbit);
	set_bit(EV_SYN, pt_input_dev->evbit);

	set_bit(KEY_A, pt_input_dev->keybit);
	set_bit(KEY_B, pt_input_dev->keybit);
	set_bit(KEY_C, pt_input_dev->keybit);
	set_bit(KEY_D, pt_input_dev->keybit);

	pt_input_dev->name = PT_NAME;
	/*注册输入设备*/
	ret = input_register_device(pt_input_dev);
	
	for(i=0; i<GPIO_PT2262; i++)
	{
		sprintf(gpio_name_buf, "pt_gpio%d", i);
		ret = gpio_request(gpio_pt2262[i], gpio_name_buf);
		if(ret < 0)
			goto fail;
		gpio_direction_input(gpio_pt2262[i]);
		gpio_export(gpio_pt2262[i], false);
		irq_gpio[i] = gpio_to_irq(gpio_pt2262[i]);
		ret = request_irq(irq_gpio[i], pt2262_isr, IRQF_TRIGGER_RISING, "pt_isr", (void*)(gpio_pt2262+i));
		if(ret < 0)
			goto fail;
	}


fail:
	return ret;
	
}
module_init(pt_init);

void __exit pt_exit(void)
{
	int i;
	for(i=0; i<GPIO_PT2262; i++)
	{
		free_irq(irq_gpio[i], gpio_pt2262+i);
		gpio_unexport(gpio_pt2262[i]);
		gpio_free(gpio_pt2262[i]);		
	}
	/*取消注册输入设备*/
	input_unregister_device(pt_input_dev);
	/*释放输入设备*/
	input_free_device(pt_input_dev);
	
}
module_exit(pt_exit);
