#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <stdbool.h>
#include "types.h"
#include "fs.h"

#include <fcntl.h>
#include <sys/stat.h>

/** MACROS */
#define BLOCK_SIZE (BSIZE) // Block size of FS
#define BYTE   8       // Bits per byte
#define T_DIR      1   // Directory
#define T_FILE     2   // File
#define T_DEV      3   // Special device
#define DIRENTPB   (BSIZE / sizeof(struct dirent)) // number of directory entries in a block
#define BIT(addr, blocknum, ninodes) ((*(addr + (BBLOCK(blocknum,ninodes) * BLOCK_SIZE) + blocknum / BYTE)) & (0x1 << (blocknum % BYTE))) // return the value of bitmap for given block num 

/** Global variables */
int i, j, n, flag; // Iteration variables
char *addr;        // memory address of memory mapped fs
uint blocknum;     // block address
uint bitblocks, usedblocks, totalblocks, freeblock; // aggregate values of different types of blocks
struct superblock *sb; // superblock
struct dinode *dip;    // pointer to inode struct
struct dirent *de;     // pointer to directory entry struct

/**
 * @brief: Initialize the file system checker
 */
void 
init(char* image) 
{
  // open fd for given image file
  int fsfd = open(image, O_RDONLY);
  if(fsfd < 0){
    perror(image);
    exit(1);
  }

  // read stats of the image file
  struct stat buf;
  if (fstat(fsfd, &buf) != 0) {
    perror("fstat failed");
    exit(1);
  }

  // memory map the file system
  addr = mmap(NULL, buf.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
  if (addr == MAP_FAILED){
    perror("mmap failed");
    exit(1);
  }

  // read the super block
  sb = (struct superblock *) (addr + 1 * BLOCK_SIZE);
  bitblocks = sb->size/BPB + 1;
  usedblocks = sb->ninodes / IPB + 3 + bitblocks;
  totalblocks = sb->nblocks + usedblocks;
  freeblock = usedblocks;
}

/**
 * @return struct pointer to inode i
 */
struct dinode* 
inode(int i)
{
    struct dinode* ip = (struct dinode *) (addr + IBLOCK((uint) i)*BLOCK_SIZE);
    ip += (i)%IPB;
    return ip;
}

/**
 * @brief: [1] Each inode is either unallocated or one of the valid types
 */
void 
valid_inode()
{
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    // check whether inode is allocated and then if it is valid
    if (dip && dip->type && !(dip->type == T_FILE || dip->type == T_DIR || dip->type == T_DEV)) 
    {
      fprintf(stderr, "ERROR: bad inode.\n");
      exit(1);
    }
  }
}

/**
 * @return true if the blocknum is within bounds of the image file
 */
bool 
valid_data_block(uint blocknum)
{
  // any block should be within proper bounds 
  if (blocknum >= freeblock && blocknum < totalblocks)
    return true;
  return false;
}


/**
 * @brief [2] for each address , all addresses referenced are valid
 */
void 
valid_inode_blocks()
{
  // for all inodes from root inode
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    // if allocated
    if (dip && dip->type) 
    {
      for (n = 0; n < NDIRECT; n++) 
      {
        // check if direct blocks have valid address
        if (dip->addrs[n] && !valid_data_block(dip->addrs[n]))
        {
          fprintf(stderr, "ERROR: bad direct address in inode.\n");
          exit(1);
        }
      }
      if (dip->addrs[NDIRECT]) {
        if (valid_data_block(dip->addrs[NDIRECT])) {
          uint* addrs = (uint*) (addr + dip->addrs[NDIRECT] * BLOCK_SIZE);
          for (n = 0; n < NINDIRECT; n++) 
          {
            // check if indirect blocks have valid address
            if (addrs[n] && !valid_data_block(addrs[n]))
            {
              fprintf(stderr, "ERROR: bad indirect address in inode.\n");
              exit(1);
            }
          }
        } else {
          fprintf(stderr, "ERROR: bad indirect address in inode.\n");
          exit(1);
        }
      }
    }
  }
}

/**
 * @brief [3] Root directory exists with inum 1 and self reference in parent
 */
void 
valid_root()
{
  // check existence of root
  if ((dip = inode(ROOTINO)) != 0)
  {
    if (dip->type == 0 || dip->type != T_DIR)
    {
      fprintf(stderr, "ERROR: root directory does not exist.\n");
      exit(1);
    }

    de = (struct dirent *) (addr + (dip->addrs[0])*BLOCK_SIZE);
    for (i = 0; i < DIRENTPB; i++,de++){
      if (de) {
        // peculiar formatting only for root
        if (strcmp(de->name, "..") == 0 && de->inum != ROOTINO)
        {
          fprintf(stderr, "ERROR: root directory does not exist.\n");
          exit(1);
        }
      }
    }
  } else {
    fprintf(stderr, "ERROR: root directory does not exist.\n");
    exit(1);
  }
}

/**
 * @brief [4] Each directory has '.' with reference to itself and ".." to its parent
 */
void 
valid_directory()
{
  // over all inodes
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type) 
    {
      // ones which are directory
      if (dip->type == T_DIR) {
        // flag to count instances of . and ..
        flag = 0;
        // reading all direct blocks for directory entries
        for (n = 0; n < NDIRECT; n++) 
        {
          if ((blocknum = dip->addrs[n]) != 0)
          {
            de = (struct dirent *) (addr + blocknum*BLOCK_SIZE);
            for (j = 0; j < DIRENTPB; j++,de++){
              // "." should map to current inode
              if (strcmp(de->name, ".") == 0 && ++flag && de->inum != i)
              {
                fprintf(stderr, "ERROR: directory not properly formatted.\n" );
                exit(1);
              }
              if (strcmp(de->name, "..") == 0) flag++;
            }
          }
        }
        if ((blocknum = dip->addrs[NDIRECT]) != 0) 
        {
          uint* addrs = (uint*) (addr + blocknum * BLOCK_SIZE);
          for (n = 0; n < NINDIRECT; n++) 
          {
            if ((blocknum = addrs[n]) != 0)
            {
              de = (struct dirent *) (addr + blocknum*BLOCK_SIZE);
              for (j = 0; j < DIRENTPB; j++,de++){
                // "." should map to current inode
                if (strcmp(de->name, ".") == 0 && ++flag && de->inum != i)
                {
                  fprintf(stderr, "ERROR: directory not properly formatted.\n" );
                  exit(1);
                }
                if (strcmp(de->name, "..") == 0) flag++;
              }
            }
          }
        }
        if (flag != 2) {
          fprintf(stderr, "ERROR: directory not properly formatted.\n");
          exit(1);
        }
      }
    }
  }
}

/**
 * @brief [5] For in-use inodes, each block address in use is also marked in use in the bitmap
 *        [6] For blocks marked in-use  in bitmap, the block should actually be in-use in an inode
 *        or indirect block somewhere
 */
void 
valid_bitmap_mark() 
{
  bool* marked = (bool*) malloc(sizeof(bool) * sb->nblocks);
  bool* inuse = (bool*) malloc(sizeof(bool) * sb->nblocks);

  // Flag all the blocks which have been marked in bitmap
  for (i = freeblock; i < totalblocks; i++) 
  {
    if (BIT(addr, i, sb->ninodes))
      marked[i - freeblock] = true;
  }

  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    // flag all block address used in inodes
    if (dip && dip->type) 
    {
      for (n = 0; n < NDIRECT + 1; n++) 
      {
        if ((blocknum = dip->addrs[n]) != 0)
          inuse[blocknum - freeblock] = true;
      }
      if (blocknum) {
        uint* addrs = (uint*) (addr + blocknum * BLOCK_SIZE);
        for (n = 0; n < NINDIRECT; n++) 
        {
          if ((blocknum = addrs[n]) != 0)
            inuse[blocknum - freeblock] = true;
        }
      }
    }
  }
  for (i = 0; i < totalblocks - freeblock; i++) 
  {
    // not marked in bitmap but used  in inode
    if (inuse[i] && !marked[i])
    {
      fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
      exit(1);
    }
    // marked in bitmap but used nowhere in inodes
    if (marked[i] && !inuse[i])
    {
      fprintf(stderr, "ERROR: bitmap marks block in use but it is not inuse.\n");
      exit(1);
    }
  }
}

/**
 * @brief [7] For in-use inodes, each direct address in use is only used once
 */
void 
valid_direct_address()
{
  // counters for reference to data blocks
  uint* count = (uint*) malloc(sizeof(uint) * sb->nblocks);
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type) 
    {
      for (n = 0; n < NDIRECT; n++) 
      {
        if ((blocknum = dip->addrs[n]) != 0)
        {
          count[blocknum - freeblock]++;
          // any block can have utmost one reference
          if (count[blocknum - freeblock] > 1) {
            fprintf(stderr, "ERROR: direct address used more than once.\n");
            exit(1);
          }
        }
      }
    }
  }
}

/**
 * @brief [8] For in-use inodes, each indirect address in use is only used once
 */
void 
valid_indirect_address()
{
  // counters for data blocks used in indirect references
  uint* count = (uint*) malloc(sizeof(uint) * sb->nblocks);
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type && (blocknum = dip->addrs[NDIRECT]) != 0) 
    {
      uint* addrs = (uint*) (addr + blocknum * BLOCK_SIZE);
      for (n = 0; n < NINDIRECT; n++) 
      {
        if ((blocknum = addrs[n]) != 0)
        {
          count[blocknum - freeblock]++;
          // any block can have utmost one reference
          if (count[blocknum - freeblock] > 1) {
            fprintf(stderr, "ERROR: indirect address used more than once.\n");
            exit(1);
          }
        }
      }
    }
  }
}

/**
 * @brief [9] For all inodes marked in use, each must be referred to in at least one directory
 *        [10] For each inode number that is referred to in a valid directory, it is actually marked
 *        in use
 */
void 
valid_inode_mark()
{
  // flags for marked in inode bitmap
  bool* mark = (bool*) malloc(sizeof(uint) * sb->ninodes);
  // flags for used in inodes
  bool* inuse = (bool*) malloc(sizeof(bool) * sb->ninodes);

  inuse[ROOTINO] = true; // root inode has to be used bruh
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type) 
    {
      mark[i] = true; // mark as allocated in bitmap
      if (dip->type == T_DIR) {
        for (n = 0; n < NDIRECT; n++) 
        {
          if ((blocknum = dip->addrs[n]) != 0)
          {
            de = (struct dirent *) (addr + blocknum*BLOCK_SIZE);
            for (j = 0; j < DIRENTPB; j++,de++){
              if (!de || strcmp(".", de->name) == 0 || strcmp("..", de->name) == 0)
                continue;
              inuse[de->inum] = true; // mark as allocated to inode
            }
          }
        }
        if ((blocknum = dip->addrs[NDIRECT]) != 0) 
        {
          uint* addrs = (uint*) (addr + blocknum * BLOCK_SIZE);
          for (n = 0; n < NINDIRECT; n++) 
          {
            if ((blocknum = addrs[n]) != 0)
            {
              de = (struct dirent *) (addr + blocknum*BLOCK_SIZE);
              for (j = 0; j < DIRENTPB; j++,de++){
                if (!de || strcmp(".", de->name) == 0 || strcmp("..", de->name) == 0)
                  continue;
                inuse[de->inum] = true; // mark as allocated to inode
              }
            }
          }
        }
      }
    }
  }
  for (i = ROOTINO; i < sb->ninodes; i++)
  {
    // used by inode but not marked in bitmap 
    if (inuse[i] && !mark[i]) {
      fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
      exit(1);
    }
    // marked in bitmap but used nowhere in inodes
    if (!inuse[i] && mark[i]) {
      fprintf(stderr, "ERROR: inode marked use but not found in directory.\n");
      exit(1);
    }
  }
}

/**
 * @brief [11] Reference counts(number of links) for regular files match the number of times file is
 *             referred to in directories (i.e., hard links work correctly)
 */
void 
valid_ref_count()
{
  // counter for links to inodes
  uint* link = (uint*) malloc(sizeof(uint) * sb->ninodes);
  // counter for references to inodes in mapping
  uint* ref = (uint*) malloc(sizeof(uint) * sb->ninodes);

  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type) 
    {
      // counts only links to file
      if (dip->type == T_FILE) 
        link[i] += dip->nlink; // update link count
      // count references in directory mappings
      if (dip->type == T_DIR) 
      {
        for (n = 0; n < NDIRECT; n++) 
        {
          if ((blocknum = dip->addrs[n]) != 0)
          {
            de = (struct dirent *) (addr + blocknum*BLOCK_SIZE);
            for (j = 0; j < DIRENTPB; j++,de++){
              if (!de || strcmp(".", de->name) == 0 || strcmp("..", de->name) == 0)
                continue;
              ref[de->inum]++; // update reference count
            }
          }
        }
        if ((blocknum = dip->addrs[NDIRECT]) != 0) 
        {
          uint* addrs = (uint*) (addr + blocknum * BLOCK_SIZE);
          for (n = 0; n < NINDIRECT; n++) 
          {
            if ((blocknum = addrs[n]) != 0)
            {
              de = (struct dirent *) (addr + blocknum*BLOCK_SIZE);
              for (j = 0; j < DIRENTPB; j++,de++){
                if (!de || strcmp(".", de->name) == 0 || strcmp("..", de->name) == 0)
                  continue;
                ref[de->inum]++; // update reference count
              }
            }
          }
        }
      }
    }
  }
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    // check for mismatch in link count and reference count
    if (dip && dip->type && dip->type == T_FILE && ref[i] != link[i]) 
    {
      fprintf(stderr, "ERROR: bad reference count for file.\n");
      exit(1);
    }
  }
}

/**
 * @brief [12] No extra links allowed for directories (each directory only appears in one other
 *        directory)
 */
void 
valid_dir_links()
{
  // count references of directory
  uint* count = (uint*) malloc(sizeof(uint) * sb->ninodes);
  // flag whether an inode is directory or not
  bool* isdir = (bool*) malloc(sizeof(bool) * sb->ninodes);
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    isdir[i] = false;
    if (dip && dip->type && dip->type == T_DIR)  // mark isdir as true
      isdir[i] = true; 
  }
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type && dip->type == T_DIR) 
    {
      for (n = 0; n < NDIRECT; n++) 
      {
        if ((blocknum = dip->addrs[n]) != 0)
        {
          de = (struct dirent *) (addr + blocknum*BLOCK_SIZE);
          for (j = 0; j < DIRENTPB; j++,de++){
            if (!de || strcmp(".", de->name) == 0 || strcmp("..", de->name) == 0)
              continue;
            if (isdir[de->inum]) {
              count[de->inum]++;
              if (count[de->inum] != 1)  // a directory should be mapped only once throughout fs
              {
                fprintf(stderr, "ERROR: directory appears more than once in filesystem.\n");
                exit(1);
              }
            }
          }
        }
      }
      if ((blocknum = dip->addrs[NDIRECT]) != 0) 
      {
        uint* addrs = (uint*) (addr + blocknum * BLOCK_SIZE);
        for (n = 0; n < NINDIRECT; n++) 
        {
          if ((blocknum = addrs[n]) != 0)
          {
            de = (struct dirent *) (addr + blocknum*BLOCK_SIZE);
            for (j = 0; j < DIRENTPB; j++,de++) {
              if (!de || strcmp(".", de->name) == 0 || strcmp("..", de->name) == 0)
                continue;
              if (isdir[de->inum]) {
                count[de->inum]++;
                if (count[de->inum] != 1)  // a directory should be mapped only once throughout fs
                {
                  fprintf(stderr, "ERROR: directory appears more than once in filesystem.\n");
                  exit(1);
                }
              }
            }
          }
        }
      }
    }
  }
}


/** Main */
int
main(int argc, char *argv[])
{
  // check arguments
  if(argc < 2){
    fprintf(stderr, "image not found.\n");
    exit(1);
  }
  
  init(argv[1]);             // initialize the checker
  valid_inode();             // check for valid inodes
  valid_inode_blocks();      // validate each block address referred in inodes
  valid_root();              // validate the contents and existence of root directory
  valid_directory();         // validate the properties of directory i.e ".",".." entry points
  valid_bitmap_mark();       // validate block bitmap with blocks allocated in inode 
  valid_direct_address();    // validate whether block addresses used in direct blocks are not repeated
  valid_indirect_address();  // validate whether block addresses used in indirect blocks are not repeated
  valid_inode_mark();        // validate the refernces provided in inode mapping and actual inode allocation
  valid_ref_count();         // validate references associated with inodes with mapping
  valid_dir_links();         // make sure directories do not have more than link
  return 0;
}

