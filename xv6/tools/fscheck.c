#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>

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
  uint bitblocks = sb->size/BPB + 1;
  uint usedblocks = sb->ninodes / IPB + 3 + bitblocks;
  uint freeblock = usedblocks;
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
      fprintf(stderr, "ERROR: bad inode\n");
      exit(1);
    }
  }
}

/**
 * @brief [2] for each address , all addresses referenced are valid
 */
void 
valid_inode_blocks()
{
  int n = 0;
  for (i = 0; i < sb->ninodes; i++) 
  {
    dip = (struct dinode *) (addr + IBLOCK((uint) (i + ROOTINO))*BLOCK_SIZE);
    dip += (i + ROOTINO)%IPB;
    if (dip && dip->type) 
    {
      for (n = 0; n < NDIRECT; n++) 
      {
        if (dip->addrs[n] && !(dip->addrs[n] >= addr + freeblock*BLOCK_SIZE && dip->addrs[n] < addr + sb->size))
        {
          fprintf(stderr, "ERROR: bad direct address in inode.\n");
          exit(1);
        }
      }

      if (dip->addrs[n] && !(dip->addrs[n] >= addr + freeblock*BLOCK_SIZE && dip->addrs[n] < addr + sb->size))
      {
        fprintf(stderr, "ERROR: bad indirect address in inode.\n");
        exit(1);
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
          if (strcmp(de->name, ".") == 0 && de->inum != j + ROOTINO)
          {
            fprintf(stderr, "ERROR: directory not properly formatted.\n");
            exit(1);
          }
          if (strcmp(de->name, "..") == 0) 
            flag = 1;
        }
        if (!flag) {
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
  uchar* buf = (uchar*) BBLOCK(blocknum, sb->ninodes);
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
  uint blocknum, blockaddr;
  for (i = 0; i < sb->ninodes; i++) 
  {
    dip = (struct dinode *) (addr + IBLOCK((uint) (i + ROOTINO))*BLOCK_SIZE);
    dip += (i + ROOTINO)%IPB;
    if (dip && dip->type) 
    {
      int n;
      for (n = 0; n < NDIRECT; n++) 
      {
        if (blockaddr = dip->addrs[n])
        {
          uchar* buf = (uchar*) BBLOCK(blockaddr, sb->ninodes);
          if (buf[dip->addrs[n]%BPB] & (0x1 << (dip->addrs[n]%BPB)))
            continue;
          else {
            fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
            exit(1);
          }
        }
      }

      if (dip->addrs[n])
      {
          uchar* buf = (uchar*) BBLOCK(dip->addrs[n], sb->ninodes);
          if (buf[dip->addrs[n]%BPB] & (0x1 << (dip->addrs[n]%BPB)))
            continue;
          else {
            fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
            exit(1);
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

