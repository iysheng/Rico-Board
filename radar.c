/**
 * @file   radar.c
 * @author yang yongsheng
 * @date   2016.11.2
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>                 // Required for the GPIO functions
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/sched.h>

#define NAME_LEN 16

#define RADAR_TIME_SCALE 1000

#define RADAR_NUM 1
#define RADAR_NAME "rico_lan radar"

#define RADAR_MAJOR 0
static int radar_major = RADAR_MAJOR;
static dev_t radar_dev_num;
int RADAR_gpio[RADAR_NUM] = {113};
int trig_gpio[RADAR_NUM] = {110};
int echo_gpio[RADAR_NUM] = {111};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yang yongsheng<iysheng@163.com>");
MODULE_VERSION("0.1");

struct radar_dev{
	struct cdev cdev;
	int trig_num;//雷达触发引脚
	int echo_num;//雷达接受引脚
	unsigned int radar_time;//雷达时间
	unsigned int echo_irq_num;
	struct hrtimer radar_hrt;
	wait_queue_head_t wait_queue;
	wait_queue_t wait_radar;
	char name[NAME_LEN];
	bool radar_hrt_state;
	bool radar_echo_state;
} *radar_devp;

struct class *radar_class;

static enum hrtimer_restart radar_hrtimer_callback(struct hrtimer *hrt)
{
	struct radar_dev *devp_tmp;
	devp_tmp = container_of(hrt, struct radar_dev, radar_hrt);
	(devp_tmp->radar_time)++;
	hrtimer_forward_now(hrt, ns_to_ktime(RADAR_TIME_SCALE));
	return HRTIMER_RESTART;
}

irqreturn_t radar_echo_interrupt(int irq, void *dev_id)
{
	struct radar_dev *devp_tmp = (struct radar_dev *)dev_id;
	if (gpio_get_value(devp_tmp->echo_num))//上升沿触发		
	{	
		devp_tmp->radar_time = 0;
		hrtimer_start(&devp_tmp->radar_hrt, ns_to_ktime(RADAR_TIME_SCALE), HRTIMER_MODE_REL);
		devp_tmp->radar_hrt_state = false;
		devp_tmp->radar_echo_state = true;
	}
	else  if ((!gpio_get_value(devp_tmp->echo_num))&&(devp_tmp->radar_echo_state == true))//下降沿触发
	{	
		hrtimer_cancel(&devp_tmp->radar_hrt);
		devp_tmp->radar_hrt_state = true;
		devp_tmp->radar_echo_state = false;
		wake_up_interruptible(&devp_tmp->wait_queue);
	}
	return IRQ_HANDLED;
}


	
int radar_open (struct inode *inode, struct file *filp)
{
	struct radar_dev *devp_tmp;
	devp_tmp = container_of(inode->i_cdev, struct radar_dev, cdev);
	filp->private_data = devp_tmp;
	init_waitqueue_head(&devp_tmp->wait_queue);//初始化等待队列头部 	
	printk(KERN_INFO "radar_open.\n");
	return 0;
}

int radar_release (struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "radar_releas.\n");
	return 0;	
}

ssize_t radar_write (struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	printk(KERN_INFO "radar_write.\n");
	return count;
}

ssize_t radar_read (struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	char length_value[8];
	int length_tmp;
	struct radar_dev *radar_devp_tmp = filp->private_data;
	gpio_set_value(radar_devp_tmp->trig_num, false);
	udelay(10);
	gpio_set_value(radar_devp_tmp->trig_num, true);
	udelay(100);
	gpio_set_value(radar_devp_tmp->trig_num, false);
	
	hrtimer_init(&radar_devp_tmp->radar_hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	radar_devp_tmp->radar_hrt.function = radar_hrtimer_callback;
	mdelay(5);//这个长延时很重要，否则超声波模块不能正常工作，不知道为什么，怀疑是不是高精度时钟初始化的要求。
	
	init_wait(&radar_devp_tmp->wait_radar);	
	add_wait_queue(&radar_devp_tmp->wait_queue, &radar_devp_tmp->wait_radar);
	wait_event_interruptible_timeout(radar_devp_tmp->wait_queue,radar_devp_tmp->radar_hrt_state == true,msecs_to_jiffies(1000));
	if(radar_devp_tmp->radar_hrt_state == true)
	{		
		length_tmp = 34*(radar_devp_tmp->radar_time)/100;
		sprintf(length_value, "%d", length_tmp);
		copy_to_user(buf,length_value,strlen(length_value));
		printk(KERN_INFO "length in kernrl is %s.\n", length_value);
	}
	//printk(KERN_INFO "radar_read radar_timer is %dus and radar_len is %dmm.\n", radar_devp_tmp->radar_time, 34*(radar_devp_tmp->radar_time)/100);
	remove_wait_queue(&radar_devp_tmp->wait_queue,&radar_devp_tmp->wait_radar);
	return 0;
}


static struct file_operations radar_fops = 
{
	.owner = THIS_MODULE,
	.open = radar_open,
	.release = radar_release,
	.write = radar_write,
	.read = radar_read,
};

int radar_setup(struct radar_dev *devp, int min, int trig, int echo)
{
	int ret = 0; 
	sprintf(devp->name, "radar%d", min);	
	if(!gpio_is_valid(trig))
	{
		printk(KERN_INFO "invalid radar%d_gpio_trig:%d.\n",min, trig);
		goto fail_trig;
	}
	else
	{
	gpio_request(trig,"sysfs");
	gpio_direction_output(trig, false);
	gpio_export(trig, false);
	}
	if(!gpio_is_valid(echo))
	{
		printk(KERN_INFO "invalid radar%d_gpio_trig:%d.\n",min, echo);
		goto fail_echo;
	}
	else
	{
	gpio_request(echo,"sysfs");
	gpio_direction_input(echo);
	gpio_export(echo, false);
	}
	devp->trig_num = trig;
	devp->echo_num = echo;
	devp->radar_hrt_state = false;
	devp->radar_echo_state = false;
	devp->echo_irq_num = gpio_to_irq(echo);
	ret = request_irq(devp->echo_irq_num, radar_echo_interrupt, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "radar_echo_handler", devp);
	if(ret<0)
		goto fail;
	devp->cdev.owner = THIS_MODULE;
	cdev_init(&devp->cdev, &radar_fops);
	ret = cdev_add(&devp->cdev, MKDEV(radar_major, min), 1);
	if(ret)
	{
		printk(KERN_INFO "error %d adding radar%d.\n", ret, min);
	}
	else
	{
		device_create(radar_class, NULL, MKDEV(radar_major, min), NULL,devp->name);
	}
	return 0;
fail:
	gpio_unexport(echo);
fail_echo:
	gpio_unexport(trig);
fail_trig:
	return -ENODEV;
}

int __init radar_init(void)
{
	int ret,i;
	printk(KERN_INFO "radar_init begin.\n");

	radar_devp = kzalloc(sizeof(struct radar_dev) * RADAR_NUM, GFP_KERNEL);
	if(IS_ERR(radar_devp))
	{
		printk(KERN_INFO "no space for RADAR_dev.\n");
		return -ENOMEM;
	}

	if(radar_major == 0)
	{
		ret = alloc_chrdev_region(&radar_dev_num, 0, RADAR_NUM, RADAR_NAME);
		radar_major = MAJOR(radar_dev_num);
	}
	else
	{
		radar_dev_num = MKDEV(radar_major, 0);
		ret = register_chrdev_region(radar_dev_num, RADAR_NUM, RADAR_NAME);
	}
	if(ret < 0)
	{
		goto fail_devn;
	}

	radar_class = class_create(THIS_MODULE, RADAR_NAME);
	
	printk(KERN_INFO "radar_major is %d\n", radar_major);
	for(i=0; i<RADAR_NUM; i++)
	{
		radar_setup(radar_devp+i, i, trig_gpio[i], echo_gpio[i]);
	}
		
	printk(KERN_INFO "radar_init finished.\n");
	return 0;
	
fail_devn:
	kfree(radar_devp);
	return ret;
	
}

void __exit radar_exit(void)
{
	int i;
	for(i=0; i<RADAR_NUM; i++)
	{
		device_destroy(radar_class,MKDEV(radar_major,i));
		cdev_del(&(radar_devp+i)->cdev);
		unregister_chrdev_region(MKDEV(radar_major, i), 1);
		free_irq((radar_devp+i)->echo_irq_num, (radar_devp+i));		
		gpio_unexport((radar_devp+i)->echo_num);
		gpio_free((radar_devp+i)->echo_num);
		gpio_unexport((radar_devp+i)->trig_num);
		gpio_free((radar_devp+i)->trig_num);
		kfree(radar_devp+i);
	}
	
	class_destroy(radar_class);	
	
	printk(KERN_INFO "radar_exit finished.\n");
}

module_init(radar_init);
module_exit(radar_exit);
