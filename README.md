# World-s-worst-display-driver
an inquiry into writing a linux display driver module for the Jetson Nano


In this project we are interested in gaining control over a small display connected to our jetson nano by writing a linux driver for it, the display has the SSD 1306 controller chip, and communicates over I2C. In general, its the operating system’s job to communicate to hardware and provide the end user with a high level api to it, this is provided through the concept of driver modules. Such modules can be added or removed from the kernel during runtime without having to restart or rebuild the kernel. They are “plug and play” in a sense.  We first start by creating a standard program to do this job for us using the provided I2C device file and then we work out the changes needed to turn it into a device driver. Our reference here is the actual driver linux uses at drivers/video/fbdev/ssd1307fb.c


For an in depth (kinda) tutorial:
[Clickable Text](https://ahmed5720.github.io/World-s-worst-display-driver/)



![pong](/media/pong.jpeg)
