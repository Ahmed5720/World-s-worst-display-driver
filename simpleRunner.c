
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
int main()
{
	uint8_t* frame = calloc(512, sizeof(uint8_t)); // 128 * 4 * 8 bits + 1 
	for (int i = 0; i < 512; i++)
		frame[i] = 0x01; // should give us first pixel lit up for every 8 pixel column
	int fd = open("/dev/myoled", O_WRONLY);
	write(fd, frame, 512);
	close(fd);
	free(frame);

	return 0;
}
