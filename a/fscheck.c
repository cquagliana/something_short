#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include "fs.h"

// Disk layout is: superblock, inodes, block in-use bitmap, data blocks.
// Bitmap bits per block: BPB = BSIZE * 8 = 512 * 8

// bitmap format - need base and offset
// 0   [ base | 1 | 2 | 3 | 4 .... | 7] 
// 1   [ base | 1 | 2 | 3 | 4 .... | 7] 
// 2   [ base | 1 | 2 | 3 | 4 .... | 7] 
// ... 
// BPB [ base | 1 | 2 | 3 | 4 .... | 7] 

int numInodeBlocks;
int imageSize;
int numBlocks;
int numInodes;
struct superblock* sb;
char dataBitmap[BSIZE / sizeof(char)];

int main(int argc, char *argv[]) {
	int i;

	if(argc != 2) {
		printf("Usage: fscheck <file system image>\n");
	}
	
	int image = open(argv[1], O_RDONLY);
	if(image < 0) {
  		fprintf(stderr, "image not found\n");
	}
	
	// get superblock
	sb = malloc(sizeof(struct superblock));
	lseek(image, BSIZE, SEEK_SET); // superblock locaiton
        read(image, sb, sizeof(struct superblock));
	imageSize = sb->size;
	numBlocks = sb->nblocks;
	numInodes = sb->ninodes;
        
        // get inodes
        struct dinode** inodes = malloc(sizeof(struct dinode*) * numInodes);
        lseek(image, 2 * BSIZE, SEEK_SET); // inodes locaiton
        for(i = 0; i < numInodes; i++) {
        	inodes[i] = malloc(sizeof(struct dinode));
       	        read(image, inodes[i], sizeof(struct dinode));
        }
        
        // get data bitmap
        numInodeBlocks = (int) (numInodes * sizeof(struct dinode)) / BSIZE;
        lseek(image, BSIZE * (3 + numInodeBlocks), SEEK_SET); // data bitmap location
        read(image, dataBitmap, BSIZE);
           
        for(i = 0; i < numInodes; i++) {
        	struct dinode* inode = inodes[i];
       	        printf("inode %d with type of %d\n", i, inode->type);
        }
        
        for(i = 0; i < BSIZE / sizeof(char); i++) {        
        	int base = i % BPB;
        	int bitSelect = 1 << (base % 8);
        	int out = dataBitmap[base / 8] & bitSelect;
        	int free = (out == 0);   
       	        printf("bitmap index %d with entry %d\n", i, free);
        }
        
        printf("num blocks %d\n", numInodeBlocks);

	return 0;
}
