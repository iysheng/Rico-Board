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

#define NAME_LEN 16

/*gpio5_8*/
//#define MOTOR_GPIO_NUM 168

/*gpio5_9*/
#define LED_GPIO_NUM 169


#define MOTOR_NUM 1
#define MOTOR_NAME "rico_lan motor"

#define MOTOR_MAJOR 0
static int motor_major = MOTOR_MAJOR;
static dev_t motor_dev_num;
int motor_gpio[MOTOR_NUM] = {113};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yang yongsheng<iysheng@163.com>");
MODULE_VERSION("0.1");

struct motor_dev{
	struct cdev cdev;
	int num;
	unsigned int pwm_value_us;
	struct hrtimer hrt;
	char name[NAME_LEN];
	bool pwm_state;
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
	//printk(KERN_INFO "hrt time is %d",devp_tmp->pwm_value_us);
	return HRTIMER_RESTART;
}

int motor_open (struct inode *inode, struct file *filp)
{
	struct motor_dev *devp_tmp;
	devp_tmp = container_of(inode->i_cdev, struct motor_dev, cdev);
	filp->private_data = devp_tmp;
	hrtimer_init(&devp_tmp->hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	devp_tmp->hrt.function = motor_hrtimer_callback;
	gpio_set_value(devp_tmp->num, devp_tmp->pwm_state);
	hrtimer_start(&devp_tmp->hrt, ns_to_ktime(devp_tmp->pwm_value_us), HRTIMER_MODE_REL);
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

static struct file_operations motor_fops = 
{
	.owner = THIS_MODULE,
	.open = motor_open,
	.release = motor_release,
	.write = motor_write,
};

int motor_setup(struct motor_dev *devp, int min, int num)
{
	int ret = 0; 
	sprintf(devp->name, "motor%d", min);	
	if(!gpio_is_valid(num))
	{
		printk(KERN_INFO "invalid motor%d_gpio_num:%d.\n",min, num);
		return -ENODEV;
	}
	else
	{
	gpio_request(num, "sysfs");
	gpio_direction_output(num, false);
	gpio_export(num, false);	/*cause GPIO_NUM to appear in /sys/class/gpio*/
	}
	devp->num = num;
	devp->pwm_value_us = 1500;//500~2500 us
	devp->pwm_state = true;
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
		motor_setup(motor_devp+i, i, motor_gpio[i]);
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
