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
int *inodeHashMap2;
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

void badInode() {
    int i;
    struct dinode* inode;
    for(i = 0; i < numInodes; i++) {	
        inode = getInode(i);
       
        if (!(inode->type == 0)) {
            if (!(inode->type == T_FILE || 
                  inode->type == T_DIR ||
                  inode->type == T_DEV)) {
                fprintf(stderr,"ERROR: bad inode.\n");
                exit(1);                                
            }
        }
    }
}

void parentDirectoryMismatch() {
    int i, m;
    struct dinode* inode;
    struct dirent* dir = malloc(sizeof(struct dirent));		
    
    for(m = 0; m < numInodes; m++) {	

        inode = getInode(m);

        if (inode->type == 1) {  
            int parent = -1;
            
            for(i = 0; i < NDIRECT; i++) {
		if(inode->addrs[i] == 0) {
			continue;
		}
				
		// get the block containing the data at index i  
		int blockIndex = inode->addrs[i];
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
				
			if(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') {
				parent = dir->inum;
				
				if(parent == m && m != 1) {
					fprintf(stderr,"ERROR: parent directory mismatch.\n");
					exit(1);
				}
				

				goto found;
			}			
		}
	   }
	   
	   found:
	   if(parent == -1) {
	   	fprintf(stderr,"ERROR: parent directory mismatch.\n");
           	exit(1);
	   }
	   
	   inode = getInode(parent);
	   if(inode->type != T_DIR) {
	   	fprintf(stderr,"ERROR: parent directory mismatch.\n");
           	exit(1);
	   }   
	   
	   for(i = 0; i < NDIRECT; i++) {
		if(inode->addrs[i] == 0) {
			continue;
		}
				
		// get the block containing the data at index i  
		int blockIndex = inode->addrs[i];
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
			
			if(dir->inum == m) {
				goto found2;
			}			
		}
	   }
	   
	   fprintf(stderr,"ERROR: parent directory mismatch.\n");
           exit(1);
	   
	   found2:
           parent++; // meaningless and does nothing                 
        }
    }    
}

void bitmapMarksBlockInUseButItIsNotInUse() {

    int i, j, y, addr;
    for(y = beginDataBlocksAddr; y < numDataBlocks + beginDataBlocksAddr; y++) {	
    //for(i = beginDataBlocksAddr; i < BSIZE * numDataBlocks + 2 * BSIZE; i += BSIZE) {	
    
        int found = 0;        
        if (isAllocated(y)) {
		struct dinode* inode;

		for(i = 0; i < numInodes; i++) {	
			inode = getInode(i);

			// check direct blocks
			for(j = 0; j < NDIRECT; j++) {	
				addr = inode->addrs[j];
				if(addr == 0)
					continue;

				if(addr == y) {
					found = 1;
					goto wasfound;
				}
			}  

			// check indirect blocks
			addr = inode->addrs[NDIRECT];
			if(addr == 0)
				continue;

			int numBlocksForFile = (inode->size) / BSIZE;
			int indirectBase = addr;
			int indirectEnd = indirectBase + numBlocksForFile - NDIRECT;	
			if( ((inode->size) % BSIZE) != 0)
        			indirectEnd++;
					
			if(y >= indirectBase && y <= indirectEnd) {
				found = 1;
			}
		}
            
            wasfound:
            if (found == 0) {
                fprintf(stderr,"ERROR: bitmap marks block in use but it is not in use.\n");
                exit(1);
            }
        }
    }

}

void inodeMarkedUseButNotFoundInADirectory(int inodeNumber, int level) {
	int blockIndex, i, m;
	struct dinode* inode = getInode(inodeNumber);
	struct dirent* dir = malloc(sizeof(struct dirent));		
				
	for(m = 0; m < numInodes; m++) {
		inode = getInode(m);
		if(inode->type != T_DIR) continue;
		
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
				inodeHashMap2[dirInode] = 1;
			}
		}
	
		// indirect blocks
		blockIndex = inode->addrs[NDIRECT];
		if(blockIndex != 0) {
			int numBlocksForFile = (inode->size) / BSIZE;
			int indirectBase = blockIndex;
			int indirectEnd = indirectBase + numBlocksForFile - NDIRECT;
			if( ((inode->size) % BSIZE) != 0)
				indirectEnd++;
		
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
					inodeHashMap2[dirInode] = 1;
				}
			}
		}
	}

	for(i = 0; i < numInodes; i++) {
	        struct dinode* inode = getInode(i);
		if( !(inode->type == T_DIR || inode->type == T_FILE || inode->type == T_DEV) ) {
			continue;
		}
		if(inodeHashMap2[i] == 1) {
			continue;
		}
	        fprintf(stderr,"ERROR: inode marked use but not found in a directory.\n");
                exit(1);
	}
}

void rootDirectoryExists() {
    struct dinode* inode = getInode(1);
    if (inode == NULL || inode->type != T_DIR) {
        fprintf(stderr,"ERROR: root directory does not exist.\n");
        exit(1);
    }
}

void badReferenceCountForFile() {
	int numRefs[numInodes];
	int k, m, i, blockIndex;
	struct dinode* inode;
	struct dirent* dir = malloc(sizeof(struct dirent));			
	
	for (k = 0; k < numInodes; k++) {
		numRefs[k] = 0;
	}	

	for(m = 0; m < numInodes; m++) {
		inode = getInode(m);
		if(inode->type != T_DIR) continue;
		
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
				if(subinode->type == T_FILE) {
					numRefs[dirInode]++;
				}
			}
		}
	
		// indirect blocks
		blockIndex = inode->addrs[NDIRECT];
		if(blockIndex != 0) {
			int numBlocksForFile = (inode->size) / BSIZE;
			int indirectBase = blockIndex;
			int indirectEnd = indirectBase + numBlocksForFile - NDIRECT;
			if( ((inode->size) % BSIZE) != 0)
				indirectEnd++;
		
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
					if(subinode->type == T_FILE) {
						numRefs[dirInode]++;
					}
				}
			}
		}
	}
    
	for(i = 0; i < numInodes; i++) {

		struct dinode* inode = getInode(i);

		if (inode->type == T_FILE) {
		    if (numRefs[i] != inode->nlink) {
			fprintf(stderr,"ERROR: bad reference count for file.\n");
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
		for(j = 0; j < NDIRECT; j++) {	
			addr = inode->addrs[j];
		
			if(addr == 0)
				continue;

			if(addr < beginDataBlocksAddr || addr >= imageSize) {
				fprintf(stderr,"ERROR: bad address in inode.\n");
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
		
		if(((inode->size) % BSIZE) != 0)
			indirectEnd++;
		
		if(indirectBase < beginDataBlocksAddr || indirectBase >= imageSize) {
			fprintf(stderr,"ERROR: bad address in inode.\n");
			exit(1);
		}	
		if(indirectEnd < beginDataBlocksAddr || indirectEnd >= imageSize) {
			fprintf(stderr,"ERROR: bad address in inode.\n");
			exit(1);
		}
		
		for(j = indirectBase; j <= indirectEnd; j++) {
			if(!isAllocated(j)) {
				//printf("derp %d", j);
				//fprintf(stderr,"ERROR: bad address in inode.\n");
				//exit(1);			
			}
			
		}
		
		//printf("ADDR %d %d %d %d\n", indirectBase, indirectEnd, isAllocated(indirectBase), isAllocated(indirectEnd));
	}
}

void addressUsedByInodeButMarkedFreeInBitmap() {
	int i, j, k, addr;
	struct dinode* inode;
	
	for(i = 0; i < numInodes; i++) {	
	       	inode = getInode(i);
	
		// check direct blocks
		for(j = 0; j < NDIRECT; j++) {	
			addr = inode->addrs[j];
			if(addr == 0)
				continue;

			if(!isAllocated(addr)) {
				fprintf(stderr,"ERROR: address used by inode but marked free in bitmap.\n");
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
				fprintf(stderr,"ERROR: address used by inode but marked free in bitmap.\n");
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
		for(j = 0; j < NDIRECT; j++) {	
			addr = inode->addrs[j];
			if(addr == 0)
				continue;

			if(inodeHashMap[addr] != 0) {
				fprintf(stderr,"ERROR: address used more than once.\n");
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
				fprintf(stderr,"ERROR: address used more than once.\n");
				exit(1);
			}
			inodeHashMap[k] = 1;
		}
	}
}

void rootDirectoryDoesNotExist() {
	struct dinode* root = getInode(1);
	if(root == NULL) {
		fprintf(stderr,"ERROR: root directory does not exist.\n");
		exit(1);
	}
	
	if(root->type != T_DIR) {
		fprintf(stderr,"ERROR: root directory does not exist.\n");
		exit(1);
	}
	
	int rootBlock = root->addrs[0];
    struct dirent* dir = malloc(sizeof(struct dirent));

    lseek(image, BSIZE * rootBlock, SEEK_SET);
	
	// check that . points to correct location
	read(image, dir, sizeof(struct dirent));	
	if( !(dir->name[0] == '.' && dir->name[1] == '\0') ) {
		//fprintf(stderr,"ERROR: root directory does not exist\n");
		//exit(1);
		// maybe let the other test handle this?
		return;
	}
	if(dir->inum != 1) {
		fprintf(stderr,"ERROR: root directory does not exist.\n");
		exit(1);
	}
	
	// check that .. points to correct location
	read(image, dir, sizeof(struct dirent));	
	if( !(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') ) {
		//fprintf(stderr,"ERROR: root directory does not exist\n");
		//exit(1);
		// maybe let the other test handle this?
		return;
	}
	if(dir->inum != 1) {
		fprintf(stderr,"ERROR: root directory does not exist.\n");
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
		fprintf(stderr,"ERROR: directory not properly formatted.\n");
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
				fprintf(stderr,"ERROR: inode referred to in directory but marked free.\n");	
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
		if( ((inode->size) % BSIZE) != 0)
        		indirectEnd++;
		
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
					fprintf(stderr,"ERROR: inode referred to in directory but marked free.\n");	
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
	/*
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
				fprintf(stderr,"ERROR: directory appears more than once in file system.\n");
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
					fprintf(stderr,"ERROR: directory appears more than once in file system.\n");
					exit(1);
				}	
				directoryHashMap[dirInode] = 1;
				
				directoryNotProperlyFormatted(dirInode, level + 1);
			}
		}
	}
	*/
	
	int dirMap[numInodes];
	int k, m, i, blockIndex;
	struct dinode* inode;
	struct dirent* dir = malloc(sizeof(struct dirent));			
	
	for (k = 0; k < numInodes; k++) {
		dirMap[k] = 0;
	}	

	for(m = 0; m < numInodes; m++) {
		inode = getInode(m);
		if(inode->type != T_DIR) continue;
		
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
					//break;
				}
			
				int dirInode = dir->inum;
				struct dinode* subinode = getInode(dirInode);
				if(subinode->type == T_DIR) {
					
					if(dir->name[0] == '.' && dir->name[1] == '\0') {
						if(dirInode == m) {	
							continue;
						}				
					}
					if(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') {
						continue;
					}			
					
					//printf("dir: %d\n", dirInode);
					
					if(dirMap[dirInode] == 1) {
						fprintf(stderr,"ERROR: directory appears more than once in file system.\n");
						exit(1);
					}	
					dirMap[dirInode] = 1;
				}
			}
		}
	
		// indirect blocks
		blockIndex = inode->addrs[NDIRECT];
		if(blockIndex != 0) {
			int numBlocksForFile = (inode->size) / BSIZE;
			int indirectBase = blockIndex;
			int indirectEnd = indirectBase + numBlocksForFile - NDIRECT;
			if( ((inode->size) % BSIZE) != 0)
				indirectEnd++;
		
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
						//break;
					}
								
					int dirInode = dir->inum;
					struct dinode* subinode = getInode(dirInode);
					if(subinode->type == T_DIR) {
						
						
						if(dir->name[0] == '.' && dir->name[1] == '\0') {
							if(dirInode == m) {	
								continue;
							}
						}
						if(dir->name[0] == '.' && dir->name[1] == '.' && dir->name[2] == '\0') {
							continue;
						}
						
						printf("dir: %d\n", dirInode);

						
						if(dirMap[dirInode] == 1) {
							fprintf(stderr,"ERROR: directory appears more than once in file system.\n");
							exit(1);
						}	
						dirMap[dirInode] = 1;
					}
				}
			}
		}
	}
	
}

void printBitMap() {
	int i;
	for(i = 0; i < numDataBlocks; i++) {	
       	    fprintf(stderr,"ERROR: bitmap index %d with entry %d\n", i, isAllocated(i));
        }   
}

void printInodes() {
	int i;
        for(i = 0; i < numInodes; i++) {
        	struct dinode* inode = getInode(i);
        	
        	int sz = (inode->size) / BSIZE;
        	if( ((inode->size) % BSIZE) != 0)
        		sz++;
        	
       	        printf("inode=%d size=%d blocks=%d type=%d ref=%d| ", i, inode->size, sz, inode->type, inode->nlink);
       	        
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

int main(int argc, char *argv[]) {
	int i;

	if(argc != 2) {
		printf("Usage: fscheck <file system image>\n");
	}
	
	image = open(argv[1], O_RDONLY);
	if(image < 0) {
  		fprintf(stderr, "image not found.\n");
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
	inodeHashMap = malloc(sizeof(int) * numInodes);
	for(i = 0; i < imageSize; i++)
		inodeHashMap[i] = 0;
		
	inodeHashMap2 = malloc(sizeof(int) * numInodes);
	for(i = 0; i < imageSize; i++)
		inodeHashMap2[i] = 0;
		
	directoryHashMap = malloc(sizeof(int) * numInodes);
	for(i = 0; i < imageSize; i++)
		directoryHashMap[i] = 0;			
		
	// debug
	//printBitMap();             
	//printInodes(); 

	badInode(); // Jon - checked
	badAddressInInode(); // Connor - checked
	rootDirectoryDoesNotExist(); // Connor - checked
	directoryNotProperlyFormatted(1, 0); // Connor - checked
	parentDirectoryMismatch(); // Jon - checked
	addressUsedByInodeButMarkedFreeInBitmap(); // Connor - checked
	bitmapMarksBlockInUseButItIsNotInUse(); // Jon - checked
	addressUsedMoreThanOnce(); // Connor - checked
	inodeMarkedUseButNotFoundInADirectory(1, 0); // Jon - checked
	inodeReferredToInDirectoryButMarkedFree(1, 0); // Connor - checked
	badReferenceCountForFile(); // Jon - checked
	directoryAppearsMoreThanOnceInFileSystem(1, 0); // Connor 

	return 0;
}

