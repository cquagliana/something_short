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
int *inodeHashMap;
int *directoryHashMap;

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

void inodeValid() {
    int i;
    struct dinode* inode;
    for(i = 0; i < numInodes; i++) {	
        inode = getInode(i);
       
        if (!(inode->type == 0)) {
            if (!(inode->type == T_FILE || 
                  inode->type == T_DIR ||
                  inode->type == T_DEV)) {
                printf("bad inode\n");
                exit(1);                                
            }
        }
    }
}

void dotdotCheckParent() {

    int i;
    struct dinode* inode;
    for(i = 0; i < numInodes; i++) {	

        inode = getInode(i);

        if (inode->type == 1) {

            uint j;
            struct dirent* currDirent;
            
            for(j = 0; j < NDIRECT; j++) {
                if (inode->addrs[j] != 0) {
                    //aquireBlock(inode->addrs[j]);
                    uint k = inode->addrs[j];
                    k *= BSIZE;
                    
                    for (; k < (k + BSIZE); k += sizeof(struct dirent)) {
                        currDirent = (struct dirent*) &k;                
                        if (currDirent->inum != 0 && strcmp(currDirent->name, "..") == 0) {
                            struct dinode* dotdotInode = getInode(currDirent->inum);
                            if (dotdotInode == NULL || dotdotInode != inode) {
                                printf("parent directory mismatch.\n");
                                exit(1);
                            }
                        }                        
                    }
                }
            }                                
        }
    }    
}

void inUseBlockCheck() {

    int y;
    for(y = beginDataBlocksAddr; y < numDataBlocks + beginDataBlocksAddr; y++) {	
    //for(i = beginDataBlocksAddr; i < BSIZE * numDataBlocks + 2 * BSIZE; i += BSIZE) {	
    
        int found = 0;        
        if (isAllocated(y)) {
            
            int j;           
            for(j = 0; j < numInodes; j++) {
                struct dinode* inode = getInode(j);
                if (inode->type != 0) {
                    int k;
                    for (k = 0; k < NDIRECT + 1; k++) {
                        if (inode->addrs[k] == (uint) y) {
                            found = 1;
                            break;
                        }
                    }
                    if (found == 1)
                        break;
                }
            }
            if (found == 0) {
                printf("bitmap marks block in use but it is not in use.\n");
                exit(1);
            }
        }
    }

}

void inodesMarkedUsed() {

    int j;           
    for(j = 0; j < numInodes; j++) {

        struct dinode* inode = getInode(j);
        if (inode->type != 0) {
            int found = 0;
            int i;
            for(i = 0; i < numInodes; i++) {

                struct dinode* inodeDir = getInode(i);
                struct dirent* currDirent;
                if (inodeDir->type == 1) {

                    int y;
                    for (y = 0; y < NDIRECT + 1; y++)  {

                        uint k = inode->addrs[y];
                        k *= BSIZE;
                        for (; k < (k + BSIZE); k += sizeof(struct dirent)) {
                            currDirent = (struct dirent*) &k;                
                            if (currDirent->inum != 0 && getInode(currDirent->inum) == inode) {
                                found = 1;
                                break;
                            }                        
                        }
                        if (found == 1)
                            break;
                    }
                }
                if (found == 1) 
                    break;
            }
            
            if ( found == 0) {
                printf("inode marked use but not found in a directory.\n");
                exit(1);
            }
        }
    }     
}

void rootDirectoryExists() {
    struct dinode* inode = getInode(1);
    if (inode == NULL || inode->type != T_DIR) {
        printf("root directory does not exist.\n");
        exit(1);
    }
}

void correctNumRefCounts() {
    int numRefs[numInodes];
    int k = 0;
    for (; k < numInodes; k++) {
        numRefs[k] = 0;
    }

    int i;
    for(i = 0; i < numInodes; i++) {

        struct dinode* inode = getInode(i);
        struct dirent* currDirent;
        if (inode->type == 1) {
            int y;
            for (y = 0; y < NDIRECT; y++)  {

                uint k = inode->addrs[y];
                k*=BSIZE;
                for (; k < (k + BSIZE); k += sizeof(struct dirent)) {
                    currDirent = (struct dirent*) &k;                
                    numRefs[currDirent->inum]++;
                }

            }
        }

    }    

    for(i = 0; i < numInodes; i++) {

        struct dinode* inode = getInode(i);

        if (inode->type == 2) {
            if (numRefs[i] != inode->nlink) {
                printf("bad reference count for file.\n");
                exit(1);
            }
        }
    }    


}

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

			if(inodeHashMap[addr] != 0) {
				printf("address used more than once\n");
				exit(1);
			}
			inodeHashMap[addr] = 1;
        }  
        	
        // check indirect blocks
		addr = inode->addrs[NDIRECT];
		if(addr == 0)
			continue;
		
		int numBlocksForFile = (inode->size) / BSIZE;
		int indirectBase = addr;
		int indirectEnd = indirectBase + numBlocksForFile - NDIRECT;
		
		for(k = indirectBase; k <= indirectEnd; k++) {
			if(inodeHashMap[k] != 0) {
				printf("address used more than once\n");
				exit(1);
			}
			inodeHashMap[k] = 1;
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

void inodeReferredToInDirectoryButMarkedFree(int inodeNumber, int level) {
	int blockIndex, i;
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
			
			if( !(subinode->type == T_DIR || subinode->type == T_FILE || subinode->type == T_DEV) ) {
				printf("inode referred to in directory but marked free\n");	
				exit(1);
			}
			
			if(dir->name[0] == '.' && dir->name[1] == '\0') {
				continue;
			}
			if(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') {
				continue;
			}			
			
			if(subinode->type == T_DIR) {
				inodeReferredToInDirectoryButMarkedFree(dirInode, level + 1);
			}
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
				
				if( !(subinode->type == T_DIR || subinode->type == T_FILE || subinode->type == T_DEV) ) {
					printf("inode referred to in directory but marked free\n");	
					exit(1);					
				}	
				
				if(dir->name[0] == '.' && dir->name[1] == '\0') {
					continue;
				}
				if(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') {
					continue;
				}			
				
				if(subinode->type == T_DIR) {
					inodeReferredToInDirectoryButMarkedFree(dirInode, level + 1);
				}
			}
		}
	}
}

void directoryAppearsMoreThanOnceInFileSystem(int inodeNumber, int level) {
	int blockIndex, i;
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
			
			if(dir->name[0] == '.' && dir->name[1] == '\0') {
				continue;
			}
			if(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') {
				continue;
			}
			
			int dirInode = dir->inum;
			struct dinode* subinode = getInode(dirInode);
			if(subinode->type != T_DIR) {
				continue;
			}
			
			if(directoryHashMap[dirInode] == 1) {
				printf("directory appears more than once in file system\n");
				exit(1);
			}	
			directoryHashMap[dirInode] = 1;
			
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
				
				if(dir->name[0] == '.' && dir->name[1] == '\0') {
					continue;
				}
				if(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') {
					continue;
				}
				
				int dirInode = dir->inum;
				struct dinode* subinode = getInode(dirInode);
				if(subinode->type != T_DIR) {
					continue;
				}
				
				if(directoryHashMap[dirInode] == 1) {
					printf("directory appears more than once in file system\n");
					exit(1);
				}	
				directoryHashMap[dirInode] = 1;
				
				directoryNotProperlyFormatted(dirInode, level + 1);
			}
		}
	}
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
  		exit(1);
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
	inodeHashMap = malloc(sizeof(short) * numInodes);
	for(i = 0; i < imageSize; i++)
		inodeHashMap[i] = 0;
		
	directoryHashMap = malloc(sizeof(short) * numInodes);
	for(i = 0; i < imageSize; i++)
		directoryHashMap[i] = 0;				
					
	// debug
	//printBitMap();             
	//printInodes(); 
	
	// run the file system check functions
	// TODO put them in the/a correct order
	badAddressInInode();
	addressUsedByInodeButMarkedFreeInBitmap();
	addressUsedMoreThanOnce();
	rootDirectoryDoesNotExist();
	directoryNotProperlyFormatted(1, 0);
	inodeReferredToInDirectoryButMarkedFree(1, 0);
	directoryAppearsMoreThanOnceInFileSystem(1, 0);
	dotdotCheckParent();

	return 0;
}

