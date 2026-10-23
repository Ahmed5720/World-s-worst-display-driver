/*
 * 
 *   make
 *   sudo insmod oled.ko
 *   ls /sys/bus/i2c/drivers/            # should list myoled
 *
 *   echo myoled 0x3c | sudo tee /sys/bus/i2c/devices/i2c-1/new_device
 *   dmesg | tail                        # probe message
 *   ls -l /sys/bus/i2c/devices/1-003c/  # "driver" link -> myoled
 *
 *   echo 0x3c | sudo tee /sys/bus/i2c/devices/i2c-1/delete_device
 *   dmesg | tail                        # remove message
 *   sudo rmmod oled



to create a character device manually we need to:
1.  reserve our own major number and a range of minors this can be done with alloc_chrdev_region

2. keep track of which minors are in use, using an ID allocator (IDa)

3. connect a device number to its file operations by creating and registering a cdev cdev init, cdev_add, cdev_del

4. create a class and a device type so the system can make the dev/myoled file using class_create, and device_create

5. finally create an open method to set the file's private data 


 */

/*

now we would like to actually implement a real device kernel module
to do so we must have a way for the kernel to insert module, remove module
and a set of basic operations that user space programs need to call from our module:
: open(), close(), write(), ..
we also need a probe() callback the kernel will call whenever it detects a device has been inserted that needs to be handled by our driver, and performs initialization in it
the driver also needs to be able to handle multiple devices correctly
so an array of devices needs to exist, and we must take into consideration the possibilities of concurrent operations
lastly we must remember that we are in the kernel space and so the c library doesnt exist, we must therefore use kernel ops for printing, memory allocations and so on


finally, writing will now require the provided i2c_master_send(i2c_client* client, char* buf, int count) method provided from #Include /linux/i2c.h
struct i2c_client 



to build a kernel module we dont use gcc, we use the kernel's own build system, we need a makefile for that

one thing to note is that the kernel already provides us with interfaces for different device drivers based on the comm interface they use
in this case we have an i2c interface provided to us in the shape of struct i2c_driver, and we just have to fill its function pointers with our own functions
these interfaces all wrap around the more general driver interface

*our driver operates on one or more clients, or devices and for each of these clients we need some space to store its data

*since the kernel has its own memory allocator and therefore space seperate from user allocated memory, copying data from user to driver's data or vice versa requires special kernel provided methods.


*/

#include <linux/cdev.h>
#include <linux/idr.h>

#include <linux/module.h>
#include <linux/i2c.h>

#include <linux/fs.h> // file, file operations
#include <linux/uaccess.h> // copy_from_user
#include <linux/mutex.h>



/*
 i2c_client struct def from i2c.h
 struct i2c_client {
	unsigned short flags;		
	unsigned short addr;		
	char name[I2C_NAME_SIZE];
	struct i2c_adapter *adapter;	
	struct device dev;		
	int irq;			
	struct list_head detected;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	i2c_slave_cb_t slave_cb;
#endif
};

*/

static dev_t oled_devt; // dev_t packs the major and minor number together, we save that of our first char device, and so we can get the minor number of each consequetive char device in our driver by incrementing this. 
static struct class* oled_class;
static DEFINE_IDA(oled_ida);

#define MAX_DEVICES 2


// this is our per device struct that stores each client's info
typedef struct oled_dev
{
	struct i2c_client* client;
	struct cdev cdev; // needed so we can get our device from file in write() has to be part of dev not a pointer in it
	dev_t devt; // carries major + minor for this physical device
	struct mutex lock; // lock on write, to guarantee one writer at a time
	u8 buf[1 + 512];
} oled_dev;


#define MAX_CMD_LENGTH 6
typedef enum cmd_t
{
	SSD1306_I2C_CMD_NOP, // no operation
    // power
    SSD1306_I2C_CMD_POWER_OFF,
    SSD1306_I2C_CMD_POWER_ON,
    // addressing
    SSD1306_I2C_CMD_MEM_ADDR_HORIZ, // set horizontal addressing mode
    SSD1306_I2C_CMD_MEM_ADDR_VERT, // set vertical addressing mode
    SSD1306_I2C_CMD_MEM_ADDR_PAGE, // set page addressing mode (RESET)
    SSD1306_I2C_CMD_COLUMN_ADDR, // set column address. requires 2 data bytes
    SSD1306_I2C_CMD_PAGE_ADDR, // set page address. requires 2 data bytes

    // and a tad more..
    SSD1306_I2C_CMD_MUX_RATIO,
    SSD1306_I2C_CMD_ENABLE_CHARGE_PUMP,
    SSD1306_I2C_CMD_COM_PIN_CFG,
    SSD1306_I2C_CMD_LET_THERE_BE_LIGHT,
    SSD1306_I2C_CMD_MEM,

} cmd_t;
const u8 cmds[][MAX_CMD_LENGTH] = {
    [SSD1306_I2C_CMD_NOP] = {0x80, 0xE3, 0x80, 0xE3, 0x80, 0xE3},
    [SSD1306_I2C_CMD_POWER_OFF] = {0x80, 0xAE, 0x80, 0xE3, 0x80, 0xE3},
    [SSD1306_I2C_CMD_POWER_ON] =  {0x80, 0xAF, 0x80, 0xE3, 0x80, 0xE3},
    [SSD1306_I2C_CMD_MEM_ADDR_HORIZ] =  {0x80, 0x20, 0x80, 0x00, 0x80, 0xE3},
    [SSD1306_I2C_CMD_MEM_ADDR_VERT] = {0x80, 0x20, 0x80, 0x01, 0x80, 0xE3},
    [SSD1306_I2C_CMD_MEM_ADDR_PAGE] =    {0x80, 0x20, 0x80, 0x02, 0x80, 0xE3},
    [SSD1306_I2C_CMD_COLUMN_ADDR] = {0x80, 0x21, 0x80, 0x00, 0x80, 0x7F},
    [SSD1306_I2C_CMD_PAGE_ADDR] =  {0x80, 0x22, 0x80, 0x00, 0x80, 0x03},
	[SSD1306_I2C_CMD_MUX_RATIO] =  {0x80, 0xA8, 0x80, 0x1F, 0x80, 0xE3},
    [SSD1306_I2C_CMD_ENABLE_CHARGE_PUMP] =   {0x80, 0x8D, 0x80, 0x14, 0x80, 0xE3},
    [SSD1306_I2C_CMD_COM_PIN_CFG] =  {0x80, 0xDA, 0x80, 0x02, 0x80, 0xE3},
    [SSD1306_I2C_CMD_LET_THERE_BE_LIGHT] = {0x80, 0xA5, 0x80, 0xE3, 0x80, 0xE3},
    [SSD1306_I2C_CMD_MEM] = {0x80, 0xA4, 0x80, 0xE3, 0x80, 0xE3},

};


// this fancy ass driver should be called only on devices whos type is oled1
static const struct i2c_device_id oled_i2c_id[] = 
{
	{.name = "myoled"},
	{ },
};



// probe is called by the driver whenever a device is inserted that uses our driver, it allocates and initializes everything for it.
static int oled_probe(struct i2c_client* client)
{
	dev_info(&client->dev, "probe: '%s' at address 0x%02x on bus %d\n", client->name, client->addr, client->adapter->nr);
	// devm ties memory lifetime to device, when device is removed or probe fails it is freed, sort of like a unique pointer.
	// the GFP_KERNEL flag is not clear to me
	// allocate device memory
	oled_dev* dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	
	int minor = ida_simple_get(&oled_ida, 0, MAX_DEVICES, GFP_KERNEL);
	dev->devt = MKDEV(MAJOR(oled_devt), minor);
	dev->client = client;



	// set its client
	i2c_set_clientdata(client, dev);

	// send the init sequence
	oled_send_cmd(client, cmds[SSD1306_I2C_CMD_POWER_OFF], MAX_CMD_LENGTH);
	oled_send_cmd(client, cmds[SSD1306_I2C_CMD_ENABLE_CHARGE_PUMP], MAX_CMD_LENGTH);
	oled_send_cmd(client, cmds[SSD1306_I2C_CMD_MUX_RATIO], MAX_CMD_LENGTH);
	oled_send_cmd(client, cmds[SSD1306_I2C_CMD_COM_PIN_CFG], MAX_CMD_LENGTH);
	oled_send_cmd(client, cmds[SSD1306_I2C_CMD_MEM_ADDR_HORIZ], MAX_CMD_LENGTH);
	oled_send_cmd(client, cmds[SSD1306_I2C_CMD_COLUMN_ADDR], MAX_CMD_LENGTH);
	oled_send_cmd(client, cmds[SSD1306_I2C_CMD_PAGE_ADDR], MAX_CMD_LENGTH);
	oled_send_cmd(client, cmds[SSD1306_I2C_CMD_POWER_ON], MAX_CMD_LENGTH);

	// let there be light
	oled_send_cmd(client, cmds[SSD1306_I2C_CMD_MEM], MAX_CMD_LENGTH);

	mutex_init(&dev->lock);

	cdev_init(&dev->cdev, &oled_fops); // connects our char device with the file operations we defined.
	dev->cdev.owner = THIS_MODULE; 
	cdev_add(&dev->cdev, dev->devt, 1); // makes the cdev active
	device_create(oled_class, &client->dev, dev->devt, dev, "myoled%d", MINOR(dev->devt)); // this creates a device /dev/myoledN
	return 0;
}

static void oled_remove(struct i2c_client* client)
{
	dev_info(&client->dev, "device at 0x%02x unbound from driver\n");
	oled_dev* dev = i2c_get_clientdata(client);
	device_destroy(oled_class, dev->devt);
	cdev_del(&dev->cdev);
	
	ida_simple_remove(&oled_ida, MINOR(dev->devt));
	// should send display off command here
}



static struct i2c_driver oled_driver = 
{
	.probe = oled_probe,
	.remove = oled_remove,
	.id_table = oled_i2c_id,
	// base driver interface that we wrap with i2c driver's specific methods
	.driver =  
	{
		.name = "myoled",
	}, 
};


static int oled_init(void)
{
	int ret;
	ret = alloc_chrdev_region(&oled_devt, 0, MAX_DEVICES, "myoled"); // here we pick an unused major device number and minor numbers starting at 0 up to max devices

	oled_class = class_create(THIS_MODULE, "myoled");
	// this creates a group of devices of the same kind at /sys/class/myoled

	ret = i2c_add_driver(&oled_driver);
	return 0;
}
module_init(oled_init);




static int oled_send_cmd(struct i2c_client* client, const u8* cmd, size_t ln)
{
	int ret = i2c_master_send(client, cmd, ln);
	dev_info(&client->dev, "sent %zu bytes, i2c_master_send returned %d\n",
		 ln, ret);
	if (ret < 0)
		return ret;
	if (ret != ln)
		return -EIO;
	return 0;
}




// inode gives us the kernel's description of a file, inode->i_cdev points to the char device that we registered it with at the end of probe.
static int oled_open(struct inode* inode, struct file* file)
{
	oled_dev* dev = container_of(inode->i_cdev, oled_dev, cdev);
	file->private_data = dev;
	return 0;
}


// note that it is possible for two programs to call this on the same device at the same time, so a mutex is needed
// the macro __user__ tells us that this is allocated in user space and should not be dereferenced as usual
// since we have already put dev in file's private data area in oled_open, we can just get it from there this time.
static ssize_t oled_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	
	oled_dev* dev = file->private_data;
	mutex_lock(&dev->lock);
	if(copy_from_user(&dev->buf[1], ubuf, 512))
	{
		mutex_unlock(&dev->lock);
		return -1;
	}
	dev->buf[0] = 0x40;
	if(!i2c_master_send(dev->client, dev->buf, sizeof(dev->buf)))
	{
		mutex_unlock(&dev->lock);
		return -1;
	}
	mutex_unlock(&dev->lock);
	return 0;
}




static void oled_exit()
{	
	

	i2c_del_driver(&oled_driver);
	class_destroy(oled_class);
	unregister_chrdev_region(oled_devt, MAX_DEVICES);
}
module_exit(oled_exit);



static const struct file_operations oled_fops = 
{
	// should also have .owner to ensure rmmode cant be done while a device is using the driver module
	.owner = THIS_MODULE,
	.write = oled_write,
	.open = oled_open,
}; 


MODULE_DESCRIPTION("the worst driver in the world");
MODULE_AUTHOR("the greatest author of all time");
MODULE_LICENSE("GPL");

