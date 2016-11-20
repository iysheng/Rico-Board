#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/input.h>

#include "pt2262.h"

const int gpio_num[4]={49,60,51,50};

MODULE_LICENSE("GPL");

struct resource pt2262_resource[]=
{
	{
		.flags = IORESOURCE_IRQ,
	},
	{
		.flags = IORESOURCE_IRQ,
	},
	{
		.flags = IORESOURCE_IRQ,
	},
	{
//		.start = gpio_to_irq(gpio_num[3]),
//		.end = gpio_to_irq(gpio_num[3]),
		.flags = IORESOURCE_IRQ,
	}
};

struct pt2262_dev pt2262_devs[] =
{
	{
		.desc = "A",
		.gpio = 49,
		.code = KEY_A,
	},
	{
		.desc = "B",
		.gpio = 60,
		.code = KEY_B,
	},
	{
		.desc = "C",
		.gpio = 51,
		.code = KEY_C,
	},
	{
		.desc = "D",
		.gpio = 50,
		.code = KEY_D,
	}

};

void	pt2262_release(struct device *dev)
{
	printk(KERN_INFO "pt2262 release func.\n");	
}

struct  platform_device pt_platform_dev =
{
	.name = "pt2262",
	.resource = pt2262_resource,
	.num_resources = ARRAY_SIZE(pt2262_resource),
	.dev = 
	{
		.platform_data = pt2262_devs,	
		.release = pt2262_release,
	}
};

irqreturn_t pt_irq_handler(int irq, void *pdev)
{	
	struct pt2262_dev * pt2262_devp = (struct pt2262_dev*)pdev;
	int i;
	for(i=0; i<pt_platform_dev.num_resources; i++)
	{
		if(0 == strcmp(pt2262_devp->desc ,pt2262_devs[i].desc))
		{
			printk(KERN_INFO "pt2262's code is %d\tdesc is %s\ti is %d.\n", pt2262_devp->code, pt2262_devp->desc, i);	
			break;
		}
		continue;		
	}
	return IRQ_HANDLED;
}

int pt_probe(struct platform_device *plat_pdev)
{
	int i,j,ret;
	char buf[16];
	struct pt2262_dev* pt2262_pdev_tmp = plat_pdev->dev.platform_data;
	for(i=0; i<plat_pdev->num_resources; i++)
	{
		//j = plat_pdev->dev.platform_data
		j = pt2262_pdev_tmp[i].gpio;
		sprintf(buf, "pt_gpio_%d", j);
		ret = gpio_request(j, buf);
		if(ret < 0)
			goto fail;
		gpio_direction_input(j);
		((plat_pdev->resource)+i)->start = gpio_to_irq(j);
		((plat_pdev->resource)+i)->end = gpio_to_irq(j);
		ret = request_irq(((plat_pdev->resource)+i)->start,pt_irq_handler,IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING,"pt_irq_handler", pt2262_pdev_tmp+i);
		if(ret < 0)
			goto fail2;
	}
	printk(KERN_INFO "num_resources is %d.\n", plat_pdev->num_resources);
	return ret;
fail:
	printk(KERN_ERR "failed to request %s\n",buf);
	return ret;
fail2:
	printk(KERN_ERR "failed to request irq %s\n",buf);
	return ret;
}

int pt_remove(struct platform_device *plat_pdev)
{
	int i;
	struct pt2262_dev* pt2262_pdev_tmp = plat_pdev->dev.platform_data;
	printk("pt_remove goodbye.\n");
	for(i=0; i<plat_pdev->num_resources; i++)
	{
		free_irq(plat_pdev->resource[i].start, pt2262_pdev_tmp+i);
		gpio_free((pt2262_pdev_tmp+i)->gpio);
	}
	return 0;
}


struct platform_driver pt_platform_drv =
{
	.driver = 
	{
		.name = "pt2262",
		.owner = THIS_MODULE,
	},
	.probe = pt_probe,
	.remove = pt_remove,
};

int __init pt_init(void)
{
	int ret;
	ret = platform_device_register(&pt_platform_dev);	
	ret = platform_driver_register(&pt_platform_drv);	
	return ret;
}
module_init(pt_init);

void __exit pt_exit(void)
{
	platform_driver_unregister(&pt_platform_drv);
	platform_device_unregister(&pt_platform_dev);
}
module_exit(pt_exit);
