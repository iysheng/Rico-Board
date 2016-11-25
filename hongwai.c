#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("iysheng<iysheng@163.com>");


#define HW_GPIO 161

struct input_dev *hw_input_dev;

int hw_irq;
unsigned char hw_value;
unsigned char hw_code[4];
void hw_delay(unsigned char del)
{
	unsigned long del_tmp = del * 140;
	udelay(del_tmp);
}


irqreturn_t hw_isr(int irq, void * p)
{
	unsigned char i,j,time = 0,test[4];
	hw_delay(15);	
	if(true == gpio_get_value(HW_GPIO))
		goto over;
	while(false == gpio_get_value(HW_GPIO))
	{
		hw_delay(1);
	}
	for(i=0; i<4; i++)
	{		
		for(j=0; j<8; j++)
		{
			while(true == gpio_get_value(HW_GPIO))
			{
				hw_delay(1);
			}
			while(false == gpio_get_value(HW_GPIO))
			{
				hw_delay(1);
			}
			time = 0;
			while(true == gpio_get_value(HW_GPIO))
			{
				hw_delay(1);
				time++;
				if(time > 30)
					goto over;
			}
			hw_code[i] >>= 1;
			if(time > 7)
				hw_code[i] |= 0x80;				
		}
		test[i] = hw_code[i];
	}
	if(hw_code[2]|hw_code[3] == 0xff)
	{	
		hw_value = hw_code[2];
		input_report_key(hw_input_dev, hw_value, 1);
		input_report_key(hw_input_dev, hw_value, 0);
		input_sync(hw_input_dev);
	}
over:
	return IRQ_HANDLED;
}


int __init hw_init(void)
{
	int ret = 0;
	int i;
	char label[16];
	
	sprintf(label, "hongwai");
	ret = gpio_request(HW_GPIO, label);
	if(ret < 0)
	{
		printk(KERN_INFO "failed to gpio.\n");
		goto fail1;
	}
	gpio_direction_input(HW_GPIO);
	gpio_export(HW_GPIO,false);
	hw_irq = gpio_to_irq(HW_GPIO);	
	ret = request_irq(hw_irq,hw_isr,IRQF_TRIGGER_FALLING,"hongwai",NULL);

	
	if(ret < 0)
		goto fail2;
	
	hw_input_dev = input_allocate_device();
	if(IS_ERR(hw_input_dev))
	{
		printk(KERN_INFO "failed to alloce input_device");
		ret = PTR_ERR(hw_input_dev);
		goto fail3;
	}
	for(i=0; i<250; i++)
	{
		set_bit(i,hw_input_dev->keybit);
	}
	set_bit(EV_KEY, hw_input_dev->evbit);
	//sprintf(hw_input_dev->name, "hongwai");//切记不能这么写
	hw_input_dev->name = "HongWai";
	input_register_device(hw_input_dev);	
	return ret;
fail3:
	free_irq(hw_irq, NULL);	
fail2:
	gpio_unexport(HW_GPIO);
	gpio_free(HW_GPIO);
fail1:
	return ret;
}
module_init(hw_init);

void __exit hw_exit(void)
{
	input_unregister_device(hw_input_dev);
	input_free_device(hw_input_dev);
	
	free_irq(hw_irq, NULL);	
	gpio_unexport(HW_GPIO);
	gpio_free(HW_GPIO);
}

module_exit(hw_exit);
