/**
 * @file   motor.c
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

#define NAME_LEN 16

/*gpio5_8*/
//#define MOTOR_GPIO_NUM 168

#define RADAR_TIME_SCALE 1000

/*gpio5_9*/
#define LED_GPIO_NUM 169


#define MOTOR_NUM 1
#define MOTOR_NAME "rico_lan motor"

#define MOTOR_MAJOR 0
static int motor_major = MOTOR_MAJOR;
static dev_t motor_dev_num;
int motor_gpio[MOTOR_NUM] = {113};
int trig_gpio[MOTOR_NUM] = {110};
int echo_gpio[MOTOR_NUM] = {111};



MODULE_LICENSE("GPL");
MODULE_AUTHOR("yang yongsheng<iysheng@163.com>");
MODULE_VERSION("0.1");

struct motor_dev{
	struct cdev cdev;
	int num;//舵机引脚
	int trig_num;//雷达触发引脚
	int echo_num;//雷达接受引脚
	unsigned int radar_time;//雷达时间
	unsigned int pwm_value_us;//舵机高脉冲时间
	unsigned int echo_irq_num;
	struct hrtimer radar_hrt;
	struct hrtimer hrt;
	struct semaphore radar_sem;
	char name[NAME_LEN];
	bool pwm_state;
	bool hrt_state;
	bool radar_hrt_state;
} *motor_devp;

struct class *motor_class;



static enum hrtimer_restart motor_hrtimer_callback(struct hrtimer *hrt)
{
	struct motor_dev *devp_tmp;
	devp_tmp = container_of(hrt, struct motor_dev, hrt);
	devp_tmp->pwm_state = !devp_tmp->pwm_state;
	gpio_set_value(devp_tmp->num, devp_tmp->pwm_state);
	if(devp_tmp->pwm_state == false)
	hrtimer_forward_now(hrt, ns_to_ktime((20000-devp_tmp->pwm_value_us)*1000));//T=20,000ns
	else if(devp_tmp->pwm_state == true)
	hrtimer_forward_now(hrt, ns_to_ktime((devp_tmp->pwm_value_us) * 1000));//T=20,000ns
	return HRTIMER_RESTART;
}

static enum hrtimer_restart radar_hrtimer_callback(struct hrtimer *hrt)
{
	struct motor_dev *devp_tmp;
	devp_tmp = container_of(hrt, struct motor_dev, radar_hrt);
	(devp_tmp->radar_time)++;
	hrtimer_forward_now(hrt, ns_to_ktime(RADAR_TIME_SCALE));
	return HRTIMER_RESTART;
}

irqreturn_t radar_echo_interrupt(int irq, void *dev_id)
{
	struct motor_dev *devp_tmp = (struct motor_dev *)dev_id;
	if (gpio_get_value(devp_tmp->echo_num))//上升沿触发		
	{
		if(!down_trylock(&devp_tmp->radar_sem))
		{
		hrtimer_init(&devp_tmp->radar_hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		devp_tmp->radar_hrt.function = radar_hrtimer_callback;
		devp_tmp->radar_time = 0;
		hrtimer_start(&devp_tmp->radar_hrt, ns_to_ktime(RADAR_TIME_SCALE), HRTIMER_MODE_REL);
		devp_tmp->radar_hrt_state = false;
		}
	}
	else  if (!gpio_get_value(devp_tmp->echo_num))//下降沿触发
	{	
		devp_tmp->radar_hrt_state = true;
		up(&devp_tmp->radar_sem);
		hrtimer_cancel(&devp_tmp->radar_hrt);
	}
	return IRQ_HANDLED;
}

int motor_open (struct inode *inode, struct file *filp)
{
	struct motor_dev *devp_tmp;
	devp_tmp = container_of(inode->i_cdev, struct motor_dev, cdev);
	filp->private_data = devp_tmp;
	if(devp_tmp->hrt_state == false)
	{
		hrtimer_init(&devp_tmp->hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		devp_tmp->hrt.function = motor_hrtimer_callback;
		gpio_set_value(devp_tmp->num, devp_tmp->pwm_state);
		hrtimer_start(&devp_tmp->hrt, ns_to_ktime(devp_tmp->pwm_value_us), HRTIMER_MODE_REL);	
		devp_tmp->hrt_state = true;
		devp_tmp->radar_hrt_state = false;
	}
	sema_init(&devp_tmp->radar_sem, 1);
	printk(KERN_INFO "motor_open.\n");
	return 0;
}

int motor_release (struct inode *inode, struct file *filp)
{
	struct motor_dev *devp_tmp;
	devp_tmp = filp->private_data;
	printk(KERN_INFO "motor_release and pwm_value_us is %d.\n", devp_tmp->pwm_value_us);
	return 0;	
}

ssize_t motor_write (struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	char pwm_value_tmp[8];
	struct motor_dev *motor_devp_tmp = filp->private_data;
	copy_from_user(pwm_value_tmp, buf, sizeof(buf));	
	gpio_set_value(LED_GPIO_NUM	, motor_devp_tmp->pwm_state);	
	motor_devp_tmp->pwm_value_us = simple_strtoul(pwm_value_tmp, NULL, 10);
	if(motor_devp_tmp->pwm_value_us > 2500)
		motor_devp_tmp->pwm_value_us = 2500;
	else if(motor_devp_tmp->pwm_value_us < 500)
		motor_devp_tmp->pwm_value_us = 500;
	printk(KERN_INFO "pwm_value is %d.\n", motor_devp_tmp->pwm_value_us);
	printk(KERN_INFO "motor_write.\n");
	return count;
}

ssize_t radar_read (struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct motor_dev *motor_devp_tmp = filp->private_data;
	gpio_set_value(motor_devp_tmp->trig_num, false);
	udelay(10);
	gpio_set_value(motor_devp_tmp->trig_num, true);
	udelay(100);
	gpio_set_value(motor_devp_tmp->trig_num, false);
	mdelay(10);
	down(&motor_devp_tmp->radar_sem);
	if(motor_devp_tmp->radar_hrt_state == true)
		printk(KERN_INFO "radar_read ente radar_timer is %dus and radar_len is %dmm.\n", motor_devp_tmp->radar_time, 34*(motor_devp_tmp->radar_time)/100);
	up(&motor_devp_tmp->radar_sem);
	return 0;
}


static struct file_operations motor_fops = 
{
	.owner = THIS_MODULE,
	.open = motor_open,
	.release = motor_release,
	.write = motor_write,
	.read = radar_read,
};

int motor_setup(struct motor_dev *devp, int min, int num, int trig, int echo)
{
	int ret = 0; 
	sprintf(devp->name, "motor%d", min);	
	if(!gpio_is_valid(num))
	{
		printk(KERN_INFO "invalid motor%d_gpio_num:%d.\n",min, num);
		goto fail_num;
	}
	else
	{
	gpio_request(num, "sysfs");
	gpio_direction_output(num, false);
	gpio_export(num, false);	/*cause GPIO_NUM to appear in /sys/class/gpio*/
	}
	if(!gpio_is_valid(trig))
	{
		printk(KERN_INFO "invalid motor%d_gpio_trig:%d.\n",min, trig);
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
		printk(KERN_INFO "invalid motor%d_gpio_trig:%d.\n",min, trig);
		goto fail_echo;
	}
	else
	{
	gpio_request(echo,"sysfs");
	gpio_direction_input(echo);
	gpio_export(echo, false);
	}
	devp->num = num;
	devp->trig_num = trig;
	devp->echo_num = echo;
	devp->pwm_value_us = 1500;//500~2500 us
	devp->pwm_state = true;
	devp->hrt_state = false;
	devp->radar_hrt_state = false;
	devp->echo_irq_num = gpio_to_irq(echo);
	request_irq(devp->echo_irq_num, radar_echo_interrupt, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "radar_echo_handler", devp);
	devp->cdev.owner = THIS_MODULE;
	cdev_init(&devp->cdev, &motor_fops);
	ret = cdev_add(&devp->cdev, MKDEV(motor_major, min), 1);
	if(ret)
	{
		printk(KERN_INFO "error %d adding motor%d.\n", ret, min);
	}
	else
	{
		device_create(motor_class, NULL, MKDEV(motor_major, min), NULL,devp->name);
	}
	return 0; 
fail_echo:
	gpio_unexport(trig);
fail_trig:
	gpio_unexport(num);
fail_num:
	return -ENODEV;
}

int __init motor_init(void)
{
	int ret,i;
	printk(KERN_INFO "motor_init begin.\n");
	if(!gpio_is_valid(LED_GPIO_NUM))
	{
		printk(KERN_INFO "invalid LED_GPIO_NUM:%d.\n",LED_GPIO_NUM);
		return -ENODEV;
	}

	motor_devp = kzalloc(sizeof(struct motor_dev) * MOTOR_NUM, GFP_KERNEL);
	if(IS_ERR(motor_devp))
	{
		printk(KERN_INFO "no space for motor_dev.\n");
		return -ENOMEM;
	}

	motor_class = class_create(THIS_MODULE, MOTOR_NAME);

	if(motor_major == 0)
	{
		ret = alloc_chrdev_region(&motor_dev_num, 0, MOTOR_NUM, MOTOR_NAME);
		motor_major = MAJOR(motor_dev_num);
	}
	else
	{
		motor_dev_num = MKDEV(motor_major, 0);
		ret = register_chrdev_region(motor_dev_num, MOTOR_NUM, MOTOR_NAME);
	}
	if(ret < 0)
	{
		return ret;
	}
	printk(KERN_INFO "motor_major is %d\n", motor_major);
	for(i=0; i<MOTOR_NUM; i++)
	{
		motor_setup(motor_devp+i, i, motor_gpio[i], trig_gpio[i], echo_gpio[i]);
	}
		
	gpio_request(LED_GPIO_NUM, "sysfs");
	gpio_direction_output(LED_GPIO_NUM, true);
	gpio_export(LED_GPIO_NUM, false);/*cause LED_GPIO_NUM to appear in /sys/class/gpio*/
	
	printk(KERN_INFO "motor_init finished.\n");
	return 0;
	
	
}

void __exit motor_exit(void)
{
	int i;
	for(i=0; i<MOTOR_NUM; i++)
	{
		hrtimer_cancel(&(motor_devp+i)->hrt);
		gpio_unexport((motor_devp+i)->num);
		gpio_free((motor_devp+i)->num);
		gpio_unexport((motor_devp+i)->trig_num);
		gpio_free((motor_devp+i)->trig_num);
		gpio_unexport((motor_devp+i)->echo_num);
		gpio_free((motor_devp+i)->echo_num);
		free_irq((motor_devp+i)->echo_irq_num, (motor_devp+i));
		device_destroy(motor_class,MKDEV(motor_major,i));
		cdev_del(&(motor_devp+i)->cdev);
		unregister_chrdev_region(MKDEV(motor_major, i), 1);
		kfree(motor_devp+i);
	}

	
	gpio_unexport(LED_GPIO_NUM);
	gpio_free(LED_GPIO_NUM);
	class_destroy(motor_class);	
	
	printk(KERN_INFO "motor_exit finished.\n");
}

module_init(motor_init);
module_exit(motor_exit);
