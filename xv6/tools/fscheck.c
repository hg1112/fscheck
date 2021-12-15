#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "types.h"
#include "fs.h"

#define BLOCK_SIZE (BSIZE)
#define BYTEBITS   8
#define T_DIR      1   // Directory
#define T_FILE     2   // File
#define T_DEV      3   // Special device
#define DIRENTPB   (BSIZE / sizeof(struct dirent))

int i, n;
char *addr;
uint bitblocks ;
uint usedblocks;
uint totalblocks;
uint freeblock ;
struct superblock *sb;
struct dinode *dip;
struct dirent *de;

void 
init(char* filename) 
{
  int fsfd = open(filename, O_RDONLY);
  if(fsfd < 0){
    perror(filename);
    exit(1);
  }

  struct stat buf;
  if (fstat(fsfd, &buf) != 0) {
    perror("fstat failed");
    exit(1);
  }

  addr = mmap(NULL, buf.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
  if (addr == MAP_FAILED){
    perror("mmap failed");
    exit(1);
  }
  /* read the super block */
  sb = (struct superblock *) (addr + 1 * BLOCK_SIZE);
  bitblocks = sb->size/BPB + 1;
  usedblocks = sb->ninodes / IPB + 3 + bitblocks;
  totalblocks = sb->nblocks + usedblocks;
  freeblock = usedblocks;
}


/**
 * @brief: [1] Each inode is either unallocated or one of the valid types
 */
void 
valid_inode()
{
  for (i = 0; i < sb->ninodes; i++) 
  {
    dip = (struct dinode *) (addr + IBLOCK((uint) (i + ROOTINO))*BLOCK_SIZE);
    dip += (i + ROOTINO)%IPB;
    if (dip && dip->type && !(dip->type == T_FILE || dip->type == T_DIR || dip->type == T_DEV)) 
    {
      fprintf(stderr, "ERROR: bad inode.\n");
      exit(1);
    }
  }
}

bool 
valid_data_block(uint blocknum)
{
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
  for (i = 0; i < sb->ninodes; i++) 
  {
    dip = (struct dinode *) (addr + IBLOCK((uint) (i + ROOTINO))*BLOCK_SIZE);
    dip += (i + ROOTINO)%IPB;
    if (dip && dip->type) 
    {
      int n = 0;
      for (n = 0; n < NDIRECT; n++) 
      {
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
  dip = (struct dinode *) (addr + IBLOCK((uint) ROOTINO)*BLOCK_SIZE);
  dip += ROOTINO%IPB;
  if (dip) {
    if (dip->type == 0 || dip->type != T_DIR)
    {
      fprintf(stderr, "ERROR: root directory does not exist.\n");
      exit(1);
    }

    int n = dip->size/sizeof(struct dirent);
    de = (struct dirent *) (addr + (dip->addrs[0])*BLOCK_SIZE);
    for (i = 0; i < n; i++,de++){
      if (strcmp(de->name, "..") == 0 && de->inum != ROOTINO)
      {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
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
  for (i = 1; i < sb->ninodes; i++) 
  {
    dip = (struct dinode *) (addr + IBLOCK((uint) (i + ROOTINO))*BLOCK_SIZE);
    dip += (i + ROOTINO)%IPB;
    if (dip && dip->type != 0) 
    {
      if (dip->type == T_DIR)
      {
        int n = dip->size/sizeof(struct dirent);
        de = (struct dirent *) (addr + (dip->addrs[0])*BLOCK_SIZE);
        int j, flag = 0;
        for (j = 0; j < n; j++,de++){
          if (strcmp(de->name, ".") == 0 && de->inum != i + ROOTINO)
          {
            fprintf(stderr, "ERROR: directory not properly formatted.\n");
            exit(1);
          }
          if (strcmp(de->name, "..") == 0) 
            flag++;
        }
        if (flag != 1) {
          fprintf(stderr, "ERROR: directory not properly formatted.\n");
          exit(1);
        }
      }
    }
  }
}

void 
valid_bit(uint blocknum) 
{
  uchar* buf = (uchar*) (addr + BBLOCK(blocknum, sb->ninodes));
  uint offset = blocknum % BPB;
  if (!(buf[offset/8] & (0x1 << (offset % 8))))
  {
    fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
    exit(1);
  }
}

void 
valid_inode_bitblocks() 
{
  uint blocknum;
  for (i = 0; i < sb->ninodes; i++) 
  {
    dip = (struct dinode *) (addr + IBLOCK((uint) (i + ROOTINO))*BLOCK_SIZE);
    dip += (i + ROOTINO)%IPB;
    if (dip && dip->type) 
    {
      int n;
      for (n = 0; n < NDIRECT + 1; n++) 
      {
        if ((blocknum = dip->addrs[n]) != 0)
          valid_bit(blocknum);
      }
    }
  }
}

  int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(stderr, "Usage: fscheck <path to .img> ...\n");
    exit(1);
  }

  init(argv[1]);
  valid_inode();
  valid_inode_blocks();
  valid_root();
  valid_directory();
  exit(0);

}

