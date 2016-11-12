/*************************
*
*@file mada.c
*@date 2016.11.12
*author iysheng<iysheng@163.com>
*
*************************/


#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/hrtimer.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/uaccess.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("iysheng<iysheng@163.com>");
MODULE_VERSION("0.1");

#define MADA_NUM 4
#define MADA_NAME "MADA"
#define NAME_SIZE 10
#define MADA_TIME_SCALE 1000

#define VALUE_SIZE 8

#define MADA_MAJOR 0

static dev_t mada_devnum;
static int mada_major = MADA_MAJOR;

#define  P_VOLTAGE  33
#define MADA_VOLTAGE(devp) (P_VOLTAGE*(devp->mada_high)/10)

int mada_gpio[MADA_NUM] = {164, 165, 166, 167};

struct mada_dev{
	struct cdev cdev;
	struct device *device;
	int mada_gpio;
	unsigned int mada_high;
	unsigned int mada_voltage;
	dev_t devnum;
	char mada_name[NAME_SIZE];
	struct hrtimer mada_hrt;
	bool mada_gpio_value;
	bool mada_hrt_state;
} *mada_devp;

struct class *mada_class;

enum hrtimer_restart mada_hrt_callback(struct hrtimer *hrt)
{
	struct mada_dev *devp;
	devp = container_of(hrt, struct mada_dev, mada_hrt);
	devp->mada_gpio_value = !devp->mada_gpio_value;
	gpio_set_value(devp->mada_gpio, devp->mada_gpio_value);
	if(devp->mada_gpio_value == true)
		hrtimer_forward_now(hrt, ns_to_ktime(devp->mada_high * MADA_TIME_SCALE));
	else
		hrtimer_forward_now(hrt, ns_to_ktime((1000 - devp->mada_high) * MADA_TIME_SCALE));
	return HRTIMER_RESTART;
}

int mada_open (struct inode *inode, struct file *filp)
{
	struct mada_dev *devp = container_of(inode->i_cdev, struct mada_dev, cdev);
	filp->private_data = devp;
	if(devp->mada_hrt_state == false)
	{
		hrtimer_init(&devp->mada_hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		devp->mada_hrt.function = mada_hrt_callback;
		hrtimer_start(&devp->mada_hrt,ns_to_ktime(devp->mada_high),HRTIMER_MODE_REL);
		devp->mada_hrt_state = true;
	}
	printk(KERN_INFO "mada_open func.\n");
	return 0;
}

int mada_release (struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "mada_release func.\n");
	return 0;
}

ssize_t mada_write (struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	int ret;
	char value[VALUE_SIZE];
	struct mada_dev *devp = filp->private_data;
	ret = copy_from_user(value, buf, sizeof(buf));
	if(0 != ret)
	{
		printk(KERN_INFO "error in writing and errnum is %d.\n", ret);
		return 0;
	}	
	else
		devp->mada_high = (unsigned int)simple_strtoul(value, NULL, 10);	
	return sizeof(buf);
}

ssize_t mada_read (struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	int ret;
	struct mada_dev *devp = filp->private_data;
	char value[VALUE_SIZE];
	devp->mada_voltage = MADA_VOLTAGE(devp);
	sprintf(value, "%d", devp->mada_voltage);
	ret = copy_to_user(buf,value,sizeof(value));
	if(0 != ret)
	{
		printk(KERN_INFO "error in reading and errnum is %d.\n", ret);
	}
	else		
		printk(KERN_INFO "%s mada_voltage is %s.\n",devp->mada_name,value);
	return ret;
}


struct file_operations mada_fops = {
	.open = mada_open,
	.release = mada_release,
	.read = mada_read,
	.write = mada_write,
};

int mada_setup(struct mada_dev *devp, int min, int gpio_num)
{
	int ret = 0;
	printk(KERN_INFO "mada_setup function.\n");
	if(!gpio_is_valid(gpio_num))
	{
		printk(KERN_INFO "invalid gpio_num:%d.\n", gpio_num);
		return -1;
	}
	else
	{
		gpio_request(gpio_num,"sysfs");
		gpio_direction_output(gpio_num,false);
		gpio_export(gpio_num,false);
	}

	devp->mada_gpio = gpio_num;
	devp->mada_high = 500;
	//devp->mada_voltage = MADA_VOLTAGE(devp);
	devp->mada_hrt_state = false;
	devp->mada_gpio_value = false;
	devp->devnum = MKDEV(mada_major, min);
	sprintf(devp->mada_name, "mada%d", min);
	cdev_init(&devp->cdev,&mada_fops);
	ret = cdev_add(&devp->cdev,MKDEV(mada_major, min),1);
	if(ret)
	{
		printk(KERN_INFO "add mada%d fail. error is %d", min, ret);
		memset(devp, 0, sizeof(struct mada_dev));
		gpio_free(gpio_num);
		gpio_unexport(gpio_num);
		return ret;
	}
	else
		devp->device = device_create(mada_class,NULL,MKDEV(mada_major, min),NULL,devp->mada_name);
	
	return 0;
}

void mada_del(struct mada_dev *devp)
{
	hrtimer_cancel(&devp->mada_hrt);
	gpio_unexport(devp->mada_gpio);
	gpio_free(devp->mada_gpio);
	device_destroy(mada_class,devp->devnum);
	unregister_chrdev_region(devp->devnum, 1);
	memset(devp, 0, sizeof(struct mada_dev));
	kfree(devp);
}

int __init mada_init(void)
{
	int ret = 0, i;
	printk(KERN_INFO "mada_init begin.\n");
	mada_devp = kzalloc(MADA_NUM * sizeof(struct mada_dev), GFP_KERNEL);
	if(IS_ERR(mada_devp))
	{
		ret = PTR_ERR(mada_devp);
		printk(KERN_INFO "vmalloc_fail and err is %d.\n", ret);
		goto fail;
	}

	if(mada_major == 0)
	{
		ret = alloc_chrdev_region(&mada_devnum,0,MADA_NUM,MADA_NAME);
		mada_major = MAJOR(mada_devnum);
	}
	else
	{
		mada_devnum = MKDEV(mada_major, 0);
		ret = register_chrdev_region(mada_devnum,MADA_NUM,MADA_NAME);
	}
	if(ret < 0)
	{
		printk(KERN_INFO "register_fail and err is %d.\n", ret);
		goto fail;
	}
	
	mada_class = class_create(THIS_MODULE, MADA_NAME);
	for(i=0; i<MADA_NUM; i++)
	{
		mada_setup(mada_devp+i, i, mada_gpio[i]);
	}	
	
	printk(KERN_INFO "mada_init!\n");
	return 0;

fail:
	return ret;
}

void __exit mada_exit(void)
{
	int i;
	for(i=0; i<MADA_NUM; i++)
		mada_del(mada_devp+i);
	class_destroy(mada_class);
	printk("mada_exit!\n");
}
module_init(mada_init);
module_exit(mada_exit);

