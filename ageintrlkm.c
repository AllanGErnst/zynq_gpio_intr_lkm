#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

#define CLASS_NAME "AGE_CUSTOM"
#define DEV_NAME "GPIO_INTR_STATUS"
#define BUFFER_SIZE 64

static unsigned int gpioLED = 1016;
static unsigned int gpioButton = 1020;
static unsigned int irqNumber;
static unsigned int numberPresses = 0;
static bool ledOn = 0;
static unsigned int intr_cnt = 0;


// Function prototype
static irq_handler_t  btn_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);


dev_t dev_no;
static struct cdev c_dev;
static struct class *driver_class = NULL;
static struct device *this_device;

static char output_buffer[BUFFER_SIZE];

int openCnt = 0;
int closeCnt = 0;
int placeHolder = 0;

static ssize_t dev_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset){
	/* If offset is behind the end of a file we have nothing to read */
	if( *offset >= BUFFER_SIZE )
		return 0;
	/* If a user tries to read more than we have, read only as many bytes as we have */
	if( *offset + length > BUFFER_SIZE )
		length = BUFFER_SIZE - *offset;
	if( copy_to_user(buffer, output_buffer + *offset, length) != 0 )
		return -EFAULT;    
	/* Move reading offset */
	*offset += length;
	return length;
}

ssize_t dev_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset){
	printk(KERN_ALERT "GPIO_INTR is a read only device\n");
	return -EROFS;
}

int dev_open(struct inode *pinode, struct file *pfile){
	openCnt++;
	sprintf(output_buffer, "The GPIO interrupt has been triggered %d times\n", numberPresses); 
	//printk(KERN_ALERT "OPENING Simple Character Driver. It has been opened %d times\n", openCnt);
	return 0;
}

int dev_release(struct inode *pinode, struct file *pfile){
	closeCnt++;
	//printk(KERN_ALERT "CLOSING Simple Character Driver. It has been closed %d times\n", closeCnt);
	return 0;
}

struct file_operations fops = {
	.owner   = THIS_MODULE,
	.read    = dev_read,
	.write   = dev_write,
	.open    = dev_open,
	.release = dev_release
};

static int dev_init(void){

	printk(KERN_ALERT "GPIO_INTR: inside %s  \n",__FUNCTION__);

	// Request major number from kernel
	if(alloc_chrdev_region( &dev_no , 0, 1,"age_led_dev") < 0) {
		printk("GPIO_INTR: No major number assigned\n");
		return -1;
	}

	// Create class and device in /sys/class/<classname>
	driver_class = class_create(THIS_MODULE, CLASS_NAME);
	if(driver_class==NULL){
		printk(KERN_ALERT "GPIO_INTR: Create class failed\n");
		unregister_chrdev_region(dev_no, 1);
		return -1;
	}

	this_device = device_create(driver_class, NULL, dev_no,NULL, DEV_NAME);
	if(this_device == NULL){
		printk(KERN_ALERT "GPIO_INTR: Create device failed\n");
		class_destroy(driver_class);
		unregister_chrdev_region(dev_no,1);
		return -1;
	}

	// Initialize char_dev to /dev/...
	cdev_init(&c_dev, &fops);
	if(cdev_add(&c_dev, dev_no,1) == -1){
		printk(KERN_ALERT "GPIO_INTR: Create char driver failed\n");
		device_destroy(driver_class,dev_no);
		class_destroy(driver_class);
		unregister_chrdev_region(dev_no,1);
		return -1;
	}

	printk(KERN_ALERT "GPIO_INTR: Device successfully created\n");

	int result = 0;

	printk(KERN_INFO "GPIO_INTR: Initializing GPIOs\n");

	if (!gpio_is_valid(gpioLED)){
		printk(KERN_INFO "GPIO_INTR: Invalid LED GPIO\n");
		return -ENODEV;
	}

	// Setup LED and BTN
	ledOn = true;
	gpio_request(gpioLED, "sysfs");
	gpio_direction_output(gpioLED, ledOn);
	gpio_export(gpioLED, false);

	gpio_request(gpioButton, "sysfs");
	gpio_direction_input(gpioButton);
	gpio_set_debounce(gpioButton, 200);
	gpio_export(gpioButton, false);

	// map GPIO number to IRQ number
	irqNumber = gpio_to_irq(gpioButton);
	printk(KERN_INFO "GPIO_INTR: The BTN GPIO is mapped to IRQ: %d\n", irqNumber);

	// Request interrupt
	result = request_irq(irqNumber,(irq_handler_t) btn_irq_handler,IRQF_TRIGGER_RISING,"gpio_intr_handler",NULL);

	return result;
}

static void dev_exit(void){

	printk(KERN_ALERT "GPIO_INTR: inside %s  \n",__FUNCTION__);
	cdev_del(&c_dev);
	device_destroy(driver_class,dev_no);
	class_destroy(driver_class);
	unregister_chrdev_region(dev_no,1);

	printk(KERN_INFO "GPIO_INTR: the button was pressed %d times\n", numberPresses);
	gpio_set_value(gpioLED, 0);
	gpio_unexport(gpioLED);
	free_irq(irqNumber, NULL);
	gpio_unexport(gpioButton);
	gpio_free(gpioLED);
	gpio_free(gpioButton);
	printk(KERN_INFO "GPIO_INTR: exit\n");
}

static irq_handler_t btn_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
	ledOn = !ledOn;
	gpio_set_value(gpioLED, ledOn);
	numberPresses++;
	return (irq_handler_t) IRQ_HANDLED;
}

MODULE_LICENSE("GPL");
module_init(dev_init);
module_exit(dev_exit);