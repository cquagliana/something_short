#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include "fs.h"

#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Special device

int image;
int numInodeBlocks;
int imageSize;
int numDataBlocks;
int numInodes;
int dataBitmapAddr;
int beginDataBlocksAddr;
struct superblock* sb;
struct dinode** inodes;
char dataBitmap[BSIZE];
char currBlock[BSIZE];
int *addressHashSet;

///////////////////////
//// misc. helpers //// 
///////////////////////

// this accesses the bitmap entry for block "blockAddr" and returns the value,
// which will be 0 or 1 for unallocated and deallocated respectively. 
int isAllocated(int blockAddr) {
        int base = blockAddr % BPB;
        int bitSelect = 1 << (base % 8);
        int out = dataBitmap[base / 8] & bitSelect;
        return !(out == 0);   
}

// this gets the inode in slot i
struct dinode* getInode(int i) {
	if(i >= 0 && i < numInodes) {
		return inodes[i];
	}
	return NULL;
}

// gets the addr-th block and puts it into currBlock 
void aquireBlock(int addr) {
    lseek(image, addr * BSIZE, SEEK_SET);
    read(image, currBlock, BSIZE);
}

////////////////////////////
//// file system checks //// 
////////////////////////////

void badAddressInInode() {
	int i, j, addr;
	struct dinode* inode;
	
	for(i = 0; i < numInodes; i++) {	
	       	inode = getInode(i);
	
		// check direct blocks
		for(j = 0; i < NDIRECT; i++) {	
			addr = inode->addrs[j];
			if(addr == 0)
				continue;

			if(addr < beginDataBlocksAddr || addr >= imageSize) {
				printf("bad address in inode\n");
				exit(1);
			}
        }  
        	
        // check indirect blocks
		addr = inode->addrs[NDIRECT];
		if(addr == 0)
			continue;
		
		int numBlocksForFile = (inode->size) / BSIZE;
		int indirectBase = addr;
		int indirectEnd = indirectBase + numBlocksForFile - NDIRECT;
		
		if(indirectBase < beginDataBlocksAddr || indirectBase >= imageSize) {
			printf("bad address in inode\n");
			exit(1);
		}	
		if(indirectEnd < beginDataBlocksAddr || indirectEnd >= imageSize) {
			printf("bad address in inode\n");
			exit(1);
		}
	}
}

void addressUsedByInodeButMarkedFreeInBitmap() {
	int i, j, k, addr;
	struct dinode* inode;
	
	for(i = 0; i < numInodes; i++) {	
	       	inode = getInode(i);
	
		// check direct blocks
		for(j = 0; i < NDIRECT; i++) {	
			addr = inode->addrs[j];
			if(addr == 0)
				continue;

			if(!isAllocated(addr)) {
				printf("address used by inode but marked free in bitmap\n");
				exit(1);
			}
        }  
        	
        // check indirect blocks
		addr = inode->addrs[NDIRECT];
		if(addr == 0)
			continue;
		
		int numBlocksForFile = (inode->size) / BSIZE;
		int indirectBase = addr;
		int indirectEnd = indirectBase + numBlocksForFile - NDIRECT;
		
		for(k = indirectBase; k <= indirectEnd; k++) {
			if(!isAllocated(k)) {
				printf("address used by inode but marked free in bitmap\n");
				exit(1);
			}
		}
	}
}

void addressUsedMoreThanOnce() {
	int i, j, k, addr;
	struct dinode* inode;
	
	for(i = 0; i < numInodes; i++) {	
	       	inode = getInode(i);
	
		// check direct blocks
		for(j = 0; i < NDIRECT; i++) {	
			addr = inode->addrs[j];
			if(addr == 0)
				continue;

			if(addressHashSet[addr] != 0) {
				printf("address used more than once\n");
				exit(1);
			}
			addressHashSet[addr] = 1;
        }  
        	
        // check indirect blocks
		addr = inode->addrs[NDIRECT];
		if(addr == 0)
			continue;
		
		int numBlocksForFile = (inode->size) / BSIZE;
		int indirectBase = addr;
		int indirectEnd = indirectBase + numBlocksForFile - NDIRECT;
		
		for(k = indirectBase; k <= indirectEnd; k++) {
			if(addressHashSet[k] != 0) {
				printf("address used more than once\n");
				exit(1);
			}
			addressHashSet[k] = 1;
		}
	}
}

void rootDirectoryDoesNotExist() {
	struct dinode* root = getInode(1);
	if(root == NULL) {
		printf("root directory does not exist\n");
		exit(1);
	}
	
	if(root->type != T_DIR) {
		printf("root directory does not exist\n");
		exit(1);
	}
	
	int rootBlock = root->addrs[0];
    struct dirent* dir = malloc(sizeof(struct dirent));

    lseek(image, BSIZE * rootBlock, SEEK_SET);
	
	// check that . points to correct location
	read(image, dir, sizeof(struct dirent));	
	if( !(dir->name[0] == '.' && dir->name[1] == '\0') ) {
		//printf("root directory does not exist\n");
		//exit(1);
		// maybe let the other test handle this?
		return;
	}
	if(dir->inum != 1) {
		printf("root directory does not exist\n");
		exit(1);
	}
	
	// check that .. points to correct location
	read(image, dir, sizeof(struct dirent));	
	if( !(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') ) {
		//printf("root directory does not exist\n");
		//exit(1);
		// maybe let the other test handle this?
		return;
	}
	if(dir->inum != 1) {
		printf("root directory does not exist\n");
		exit(1);
	}
		
	free(dir);
}

void directoryNotProperlyFormatted(int inodeNumber, int level) {
	int blockIndex, i;
	int foundDot = 0;
	int foundDotDot = 0;
	struct dinode* inode = getInode(inodeNumber);
	struct dirent* dir = malloc(sizeof(struct dirent));		
		
	// direct blocks
	for(i = 0; i < NDIRECT; i++) {
		if(inode->addrs[i] == 0) {
			continue;
		}
				
		// get the block containing the data at index i  
		blockIndex = inode->addrs[i];
		int totalRead = 0;
	
		while(1) {
			if(totalRead + sizeof(struct dirent) > BSIZE) {	
				break;
			}	
		
			lseek(image, BSIZE * blockIndex + totalRead, SEEK_SET);
			read(image, dir, sizeof(struct dirent));	
			totalRead += sizeof(struct dirent); 
			
			if(dir->name[0] == 0) {
				break;
			}
			
			int dirInode = dir->inum;
			struct dinode* subinode = getInode(dirInode);
			if(subinode->type != T_DIR) {
				continue;
			}
			
			/*
			int j;
			for(j = 0; j < level; j++) 
				printf("\t");		
			printf("item %d name: ", dirInode);
			
			for(j = 0; j < DIRSIZ; j++) {
				printf("%c",dir->name[j]);
			}	
			printf("\n");
			*/
				
			if(dir->name[0] == '.' && dir->name[1] == '\0') {
				foundDot = 1;
				continue;
			}
			if(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') {
				foundDotDot = 1;
				continue;
			}			
			
			directoryNotProperlyFormatted(dirInode, level + 1);
		}
	}
	
	// indirect blocks
	blockIndex = inode->addrs[NDIRECT];
	if(blockIndex != 0) {
		int numBlocksForFile = (inode->size) / BSIZE;
		int indirectBase = blockIndex;
		int indirectEnd = indirectBase + numBlocksForFile - NDIRECT;
		
		for(blockIndex = indirectBase; blockIndex <= indirectEnd; blockIndex++) {
			int totalRead = 0;
	
			while(1) {
				if(totalRead + sizeof(struct dirent) > BSIZE) {	
					break;
				}	
			
				lseek(image, BSIZE * blockIndex + totalRead, SEEK_SET);
				read(image, dir, sizeof(struct dirent));	
				totalRead += sizeof(struct dirent); 
				
				if(dir->name[0] == 0) {
					break;
				}
				
				int dirInode = dir->inum;
				struct dinode* subinode = getInode(dirInode);
				if(subinode->type != T_DIR) {
					continue;
				}
				
				/*
				int j;
				for(j = 0; j < level; j++) 
					printf("\t");		
				printf("item %d name: ", dirInode);
				
				for(j = 0; j < DIRSIZ; j++) {
					printf("%c",dir->name[j]);
				}	
				printf("\n");
				*/
					
				if(dir->name[0] == '.' && dir->name[1] == '\0') {
					foundDot = 1;
					continue;
				}
				if(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') {
					foundDotDot = 1;
					continue;
				}			
				
				directoryNotProperlyFormatted(dirInode, level + 1);
			}
		}
	}
	
	if(!foundDot || !foundDotDot) {
		printf("directory not properly formatted\n");
		exit(1);
	}
		
	free(dir);
}

///////////////////
//// debugging //// 
///////////////////

void printBitMap() {
	int i;
	for(i = 0; i < numDataBlocks; i++) {	
       	        printf("bitmap index %d with entry %d\n", i, isAllocated(i));
        }   
}

void printInodes() {
	int i;
        for(i = 0; i < numInodes; i++) {
        	struct dinode* inode = getInode(i);
        	
        	int sz = (inode->size) / BSIZE;
        	if( ((inode->size) % BSIZE) != 0)
        		sz++;
        	
       	        printf("inode=%d size=%d blocks=%d | ", i, inode->size, sz);
       	        
       	        int j;
       	        for(j = 0; j < NDIRECT; j++) {
       	       		printf("%d ", inode->addrs[j]);
       	        }
       	        
       	        if(inode->addrs[NDIRECT] != 0) {
       	        	int base = inode->addrs[NDIRECT];
       	        	base += sz;
       	        	base -= NDIRECT;
       	        	printf(" | ind_start=%d ind_end=%d", inode->addrs[NDIRECT], base);
       	        }
       	        
       	        printf("\n\n");
        }   
}

/////////////////////
//// end helpers ////
/////////////////////

int main(int argc, char *argv[]) {
	int i;

	if(argc != 2) {
		printf("Usage: fscheck <file system image>\n");
	}
	
	image = open(argv[1], O_RDONLY);
	if(image < 0) {
  		fprintf(stderr, "image not found\n");
	}
	
	// get superblock
	sb = malloc(sizeof(struct superblock));
	lseek(image, BSIZE, SEEK_SET); // superblock locaiton
        read(image, sb, sizeof(struct superblock));
	imageSize = sb->size;
	numDataBlocks = sb->nblocks;
	numInodes = sb->ninodes;
        
	// get inodes
	inodes = malloc(sizeof(struct dinode*) * numInodes);
	lseek(image, 2 * BSIZE, SEEK_SET); // inodes locaiton
	for(i = 0; i < numInodes; i++) {
		inodes[i] = malloc(sizeof(struct dinode));
			read(image, inodes[i], sizeof(struct dinode));
	}
	
	// get data bitmap
	numInodeBlocks = 1 + (int) (numInodes * sizeof(struct dinode)) / BSIZE;
	dataBitmapAddr = 2 + numInodeBlocks;
	beginDataBlocksAddr = 3 + numInodeBlocks;     
	lseek(image, BSIZE * (dataBitmapAddr), SEEK_SET); // data bitmap location
	read(image, dataBitmap, BSIZE);
	
	// setup helpers
	addressHashSet = malloc(sizeof(short) * imageSize);
	for(i = 0; i < imageSize; i++)
		addressHashSet[i] = 0;
					
	// debug
	//printBitMap();             
	//printInodes(); 
	
	// run the file system check functions
	badAddressInInode();
	addressUsedByInodeButMarkedFreeInBitmap();
	addressUsedMoreThanOnce();
	rootDirectoryDoesNotExist();
	directoryNotProperlyFormatted(1, 0);

	return 0;
}

