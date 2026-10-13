/*

this is not an actual driver yet, it uses system calls from stidlib.h
and stdio.h

this is for display microcontroller ssd1306

*/


#include <stdio.h> // fopen/ fclose / printf, actually useless for now
#include <stdlib.h> // malloc/free
#include <string.h> // for debugging
#include <unistd.h> // write() 
#include <sys/ioctl.h> // to specify the address of the device within the i2c bus
//#include <linux/i2c-dev.h> // not sure if needed here, it has definitions but do we need anything other than the i2c_slave macro?
#include <fcntl.h> // open(), write()
#include <stdlib.h> // uint8_t definition
#define I2C_SLAVE 0x0703 // do we need any other definition so far?
// #define I2C_FUNCS 0x0705


/*
structs used
*/



#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 32
#define SSD1306_PAGES (SSD1306_HEIGHT / 8)

typedef struct ssd1306_i2c_t
{
	int fd; // file descriptor
	char* dev; // device name
	uint8_t addr; // i2c address
	uint8_t* displayRam; // [SSD1306_PAGES][SSD1306_WIDTH] + 1 // carries 1 byte for every 8 vertical pixels in a page
	// + a start byte for the ctrl byte that says the following is a bunch of databytes = 0x40
} ssd1306_i2c_t;

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

} cmd_t;

typedef struct frameBuffer
{
	uint8_t* frame;
	int sz;
} frameBuffer;

uint8_t* cmdbuf;
#define MAX_CMD_LENGTH 16


/*
	the commandBuffer can be used to send multiple commands after one another
	we always send a control byte followed by a data byte?
	the control byte just tells us two things only, whether we would continue to receive control bytes and 
	whether the next byte is data or control

	therefore when sending commands we are always sending pairs
	(ctrl byte + cmd byte)
	however if we set continue = 0 that means the next byte will not be control 
	so we can send multiple data bytes in a row
	using 0x40 + dataByte1, databyte2.. 

	ctrl = 0000 0000 =  0x00 = next is command 
	ctrl = 0100 0000 =  0x40 = next is data
	ctrl = 1000 0000 =  0x80 = next is command, expect control after
	ctrl = 1100 0000 =  0xC0 = next is data, expect control after


*/
int fill_cmd_buffer(cmd_t cmd, uint8_t* cmdbuf, uint8_t* data)
{

	// for now cmd buffer has fixed size = 6 as thats the max len so far among all our commands
	// we init cmd buffer with ctrl bytes (co = 1, d/c = 0) at odd positions and nops at even positons, in the swicth statements these no ops are actually set 
	int sz = 6;
	for (size_t i = 0; i < sz; i++)
		cmdbuf[i] = i % 2 == 0 ? 0x80 : 0xE3;
	
	switch(cmd)
	{
		case SSD1306_I2C_CMD_POWER_OFF:
			cmdbuf[1] = 0xAE; break;
		case SSD1306_I2C_CMD_POWER_ON:
			cmdbuf[1] = 0xAF; break;

		// sets the addressing mode to Horizontal
		case SSD1306_I2C_CMD_MEM_ADDR_HORIZ:
			cmdbuf[1] = 0x20; // Set memory address
        	cmdbuf[3] = 0x00; // horizontal
        	sz = 4;
        	break;
        case SSD1306_I2C_CMD_MEM_ADDR_PAGE:
        	cmdbuf[1] = 0x20;
        	cmdbuf[3] = 0x02;
        	sz = 4;
        	break;
		// sets the column address range to be from c0 to cn
		// 3 bytes + 3 ctrls between them
		//  (1st for column address), 2nd begin, 3rd end
		case SSD1306_I2C_CMD_COLUMN_ADDR:
			cmdbuf[1] = 0x21;
			cmdbuf[3] = data[0];// needs to be in rng [0 - 127]
			cmdbuf[5] = data[1];// needs to be in rng [0 - 127]
			sz = 6;
			break;
		// sets the page address range to be from p0 to pn
		case SSD1306_I2C_CMD_PAGE_ADDR:
			cmdbuf[1] = 0x22;
			cmdbuf[3] = data[0]; // in rng [0-3]
			cmdbuf[5] = data[1]; // in rng [0-3]
			sz = 6;
			break;
		// determines number of rows panel has
		case SSD1306_I2C_CMD_MUX_RATIO:
	        cmdbuf[1] = 0xA8;
	        cmdbuf[3] = 0x1F; // 32 rows - 1
	        sz = 4;
	        break;
	    // physical wire confiration varies across diff panel sizes
	    case SSD1306_I2C_CMD_COM_PIN_CFG:
	        cmdbuf[1] = 0xDA;
	        cmdbuf[3] = 0x02; // valid values: 0x02, 0x12, 0x22, 0x32
	        sz = 4;
	        break;
	    // needed to supply enough power for the oled
	    case SSD1306_I2C_CMD_ENABLE_CHARGE_PUMP:
	        cmdbuf[1] = 0x8D; 
	        cmdbuf[3] = 0x14; // enable 
	        sz = 4;
	        break;
	    default:
	    	cmdbuf[1] = 0xE3; // nop;
	    	break;

	}

	return sz;

}

int send_cmd(ssd1306_i2c_t* display, cmd_t cmd, uint8_t* data)
{	
	int sz = fill_cmd_buffer(cmd, cmdbuf, data);
	write(display->fd, cmdbuf, sz); 
	return 0;
}

// init
int display_init(ssd1306_i2c_t* display)
{	

	uint8_t data[2] = {0, 127};
	cmdbuf = malloc(sizeof(uint8_t) * MAX_CMD_LENGTH);

	send_cmd(display, SSD1306_I2C_CMD_POWER_OFF, data);

	send_cmd(display, SSD1306_I2C_CMD_ENABLE_CHARGE_PUMP, data);
	send_cmd(display, SSD1306_I2C_CMD_MUX_RATIO, data);
	send_cmd(display, SSD1306_I2C_CMD_COM_PIN_CFG, data);


	data[1] = 127;
	send_cmd(display, SSD1306_I2C_CMD_MEM_ADDR_HORIZ, data);
	send_cmd(display, SSD1306_I2C_CMD_COLUMN_ADDR, data);
	data[1] = 3;
	//send_cmd(display, SSD1306_I2C_CMD_MEM_ADDR_PAGE, data);
	send_cmd(display, SSD1306_I2C_CMD_PAGE_ADDR, data);


	send_cmd(display, SSD1306_I2C_CMD_POWER_ON, data);
	return 0;

}


// creates a display struct and allocates memory to it and sets its data
// then opens its file descriptor for reading/writing
ssd1306_i2c_t* open_display(const char* dev, uint8_t addr)
{
	// no error checking, cause its for the weak.
	ssd1306_i2c_t* display = calloc(1, sizeof(*display));
	display->dev = strdup(dev);
	display->addr = addr;
	display->displayRam = calloc(1, 1 + SSD1306_PAGES * SSD1306_WIDTH); 
	display->fd = open(dev, O_RDWR);
	if(display->fd == -1)
		printf("could not open bus\n");
	if(ioctl(display->fd, I2C_SLAVE, addr) == -1)
		printf("could not device on bus\n");
	return display;

}

// since c passes by value, setting display = null just nulls the copy, we would need to pass a pointer to the pointer
int close_display(ssd1306_i2c_t* display)
{
	if(display)
	{
		close(display->fd);
		free(display->displayRam);
		display->displayRam = NULL;
		free(display->dev);
		display->dev = NULL;
		free(display);
		display = NULL;
	}
	if(cmdbuf)
	{
		free(cmdbuf);
		cmdbuf = NULL;
	}

	return 0;
	
}



/*

	writes whats in framebuffer to display
*/
int update_display(ssd1306_i2c_t* display, frameBuffer* fb)
{
	write(display->fd, fb->frame, fb->sz);
	return 0;
}



// example use
int main()
{	
	ssd1306_i2c_t* display = open_display("/dev/i2c-1", 0x3c);
	display_init(display);
	// creating, allocating, and freeing the framebuffer object should be a responsibility of the user application i believe
	frameBuffer* fb = calloc(1, sizeof(*fb));
	fb->sz = 1 + SSD1306_WIDTH * SSD1306_PAGES;
	fb->frame = calloc(1, fb->sz);
	for (int i = 0; i < fb->sz; i++)
		fb->frame[i] = 0x01; // should give us first pixel lit up for every 8 pixel column
	fb->frame[0] = 0x40;
	update_display(display, fb);
	free(fb->frame);
	fb->frame = NULL;
	free(fb);
	fb = NULL;
	return 0;
}



