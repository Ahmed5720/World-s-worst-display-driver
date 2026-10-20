/*

now we would like to actually implement a real device kernel module
to do so we must have a way for the kernel to insert module, remove module
and a set of basic operations that user space programs need to call from our module:
: open(), close(), write(), ..
we also need a probe() callback the kernel will call whenever it detects a device has been inserted that needs to be handled by our driver
the driver also needs to be able to handle multiple devices correctly
so an array of devices needs to exist, and we must take into consideration the possibilities of concurrent operations
lastly we must remember that we are in the kernel space and so the c library doesnt exist, we must therefore use kernel ops for printing, memory allocations and so on


finally, writing will now require the provided i2c_master_send(i2c_client* client, char* buf, int count) method provided from #Include /linux/i2c.h
struct i2c_client 



to build a kernel module we dont use gcc, we use the kernel's own build system




*/


static int oled_init(struct oled_par* par)
{

}


MODULE_DEVICE_TABLE(of, oled_of_match); // whats that


// probe is called by the driver whenever a device is inserted that uses our driver, it allocates and initializes everything for it*?
static int oled_probe(struct i2c_client* client);


void oled_remove(struct i2c_client* client);


// this fancy ass driver should be called only on devices whos name is oled1 or oled2
static const struct i2c_device_id oled_i2c_id[] = 
{
	{.name = "oled1"},
	{.name = "oled2"},
};
static struct i2c_driver oled_driver = 
{
	.probe = oled_probe,
	.remove = oled_remove,
	.id_table = oled_i2c_id,
	.driver =  
	{
		.name = "oled",
		.of_match_table = oled_of_match, // whats that?
	}, 
};


module_i2c_driver(oled_driver)

MODULE_DESCRIPTION("the worst driver in the world");
MODULE_AUTHOR("the greatest author of all time");
MODULE_LICENSE("GPL");

