#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char buff[52];

int main(int argc, char *argv[]) {
	int value;
	int i;
	
	int fd = open("foo", O_CREATE | O_SMALLFILE | O_RDWR);
	value = write(fd, "this is a message", 17);
	printf(1, "%d bytes were written\n", value);
	close(fd);

	int fd2 = open("foo", O_SMALLFILE | O_RDWR);
    value = read(fd2, buff, 17);
	printf(1, "%d bytes were read with message: ", value);
	
	for(i = 0; i < 52; i++) {
		if(buff[i] == 0) break;
		printf(1, "%d ", buff[i]);
	}
	printf(1, "\n");
	close(fd2);
	
	//int fd3 = open("foo", O_SMALLFILE | O_RDWR);
	//value = write(fd3, "this is a message that is probably longer than is allowed", 57);
	//printf(1, "%d bytes were written\n", value);

	exit();
}