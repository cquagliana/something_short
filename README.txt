// TODO BY MONDAY, MAY 2

-J: Each inode is either unallocated or one of the valid types (T_FILE, T_DIR, T_DEV). ERROR: bad inode.
-C: For in-use inodes, each address that is used by inode is valid (points to a valid datablock address within the image). Check indirect blocks too, when they are in use. ERROR: bad address in inode.
-J: Root directory exists, and it is inode number 1. ERROR MESSAGE: root directory does not exist.
-C: Each directory contains . and .. entries. ERROR: directory not properly formatted.
-J: Each .. entry in directory refers to the proper parent inode, and parent inode points back to it. ERROR: parent directory mismatch.
-C: For in-use inodes, each address in use is also marked in use in the bitmap. ERROR: address used by inode but marked free in bitmap.
-J: For blocks marked in-use in bitmap, actually is in-use in an inode or indirect block somewhere. ERROR: bitmap marks block in use but it is not in use.
-C: For in-use inodes, any address in use is only used once. ERROR: address used more than once.
-J: For inodes marked used in inode table, must be referred to in at least one directory. ERROR: inode marked use but not found in a directory.
-C: For inode numbers referred to in a valid directory, actually marked in use in inode table. ERROR: inode referred to in directory but marked free.
-J: Reference counts for regular files match the number of times file is referred to in directories (i.e., hard links work correctly). ERROR: bad reference count for file.
-C: No extra links allowed for directories (each directory only appears in one other directory). ERROR: directory appears more than once in file system.

-C: make function for getting inode
-C: make function for checking bitmap

// END TODO

 x | superblock | inodes | inodes | inodes | ... | inodes | data bitmap | data | data | data | ... | 
 
 // On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

inode -> data (directory or a file)
inode -> inode (indirect) 

struct dinode {
  short type = dir;
  uint addrs = {0x10, 0, 0, 0 ... } 
};


mem
_____
...
0x10 : struct dirent {   ushort inum; char name[DIRSIZ]; } 
...
________________________________________________________
array on inode helper data things:
-int countFileReferences - increment whenever you find this file
-int isReferenced

________________________________________________________
For in-use inodes, any address in use is only used once. ERROR: address used more than once.
----> need a hash table on addresses 0x323123

________________________________________________________
No extra links allowed for directories (each directory only appears in one other directory). ERROR: directory appears more than once in file system.
----> need hash table on directories

