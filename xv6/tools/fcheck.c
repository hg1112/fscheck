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

#define BLOCK_SIZE (BSIZE)
#define BYTE   8
#define T_DIR      1   // Directory
#define T_FILE     2   // File
#define T_DEV      3   // Special device
#define DIRENTPB   (BSIZE / sizeof(struct dirent))
#define BIT(addr, blocknum, ninodes) ((*(addr + (BBLOCK(blocknum,ninodes) * BLOCK_SIZE) + blocknum / BYTE)) & (0x1 << (blocknum % BYTE)))

int i, j, n, flag;
char *addr;
uint blocknum, bitblocks, usedblocks, totalblocks, freeblock;
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
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type) 
    {
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
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type && dip->type == T_DIR) 
    {
      de = (struct dirent *) (addr + (dip->addrs[0])*BLOCK_SIZE);
      for (j = 0, flag = 0; j < DIRENTPB; j++,de++){
        if (strcmp(de->name, ".") == 0 && ++flag && de->inum != i)
        {
          fprintf(stderr, "ERROR: directory not properly formatted.\n" );
          exit(1);
        }
        if (strcmp(de->name, "..") == 0) 
          flag++;
      }
      if (flag != 2) {
        fprintf(stderr, "ERROR: directory not properly formatted.\n");
        exit(1);
      }
    }
  }
}

void 
valid_bitmap_mark() 
{
  bool* marked = (bool*) malloc(sizeof(bool) * sb->nblocks);
  bool* inuse = (bool*) malloc(sizeof(bool) * sb->nblocks);
  for (i = freeblock; i < totalblocks; i++) 
  {
    if (BIT(addr, i, sb->ninodes))
      marked[i - freeblock] = true;
  }
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
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
    if (inuse[i] && !marked[i])
    {
      fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
      exit(1);
    }
    if (marked[i] && !inuse[i])
    {
      fprintf(stderr, "ERROR: bitmap marks block in use but it is not inuse.\n");
      exit(1);
    }
  }
}

void 
valid_direct_address()
{
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
            if (count[blocknum - freeblock] > 1) {
              fprintf(stderr, "ERROR: direct address used more than once.\n");
              exit(1);
            }
        }
      }
    }
  }
}

void 
valid_indirect_address()
{
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
            if (count[blocknum - freeblock] > 1) {
              fprintf(stderr, "ERROR: indirect address used more than once.\n");
              exit(1);
            }
        }
      }
    }
  }
}

void 
valid_inode_mark()
{
  bool* mark = (bool*) malloc(sizeof(uint) * sb->ninodes);
  bool* inuse = (bool*) malloc(sizeof(bool) * sb->ninodes);
  inuse[ROOTINO] = true;
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type) 
    {
      mark[i] = true;
      if (dip->type == T_DIR) {
        for (n = 0; n < NDIRECT; n++) 
        {
          if ((blocknum = dip->addrs[n]) != 0)
          {
            de = (struct dirent *) (addr + blocknum*BLOCK_SIZE);
            for (j = 0; j < DIRENTPB; j++,de++){
              if (!de || strcmp(".", de->name) == 0 || strcmp("..", de->name) == 0)
                continue;
              inuse[de->inum] = true;
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
                inuse[de->inum] = true;
              }
            }
          }
        }
      }
    }
  }
  for (i = ROOTINO; i < sb->ninodes; i++)
  {
    if (inuse[i] && !mark[i]) {
      fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
      exit(1);
    }
    if (!inuse[i] && mark[i]) {
      fprintf(stderr, "ERROR: inode marked use but not found in directory.\n");
      exit(1);
    }
  }
}

void 
valid_ref_count()
{
  uint* link = (uint*) malloc(sizeof(uint) * sb->ninodes);
  uint* ref = (uint*) malloc(sizeof(uint) * sb->ninodes);
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type) 
    {
      if (dip->type == T_FILE) 
        link[i] += dip->nlink;
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
              ref[de->inum]++;
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
                ref[de->inum]++;
              }
            }
          }
        }
      }
    }
  }
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    if (dip && dip->type && dip->type == T_FILE && ref[i] != link[i]) 
    {
      fprintf(stderr, "ERROR: bad reference count for file.\n");
      exit(1);
    }
  }
}

void 
valid_dir_links()
{
  uint* count = (uint*) malloc(sizeof(uint) * sb->ninodes);
  bool* isdir = (bool*) malloc(sizeof(bool) * sb->ninodes);
  for (i = ROOTINO, dip = inode(i); i < sb->ninodes; i++, dip++) 
  {
    isdir[i] = false;
    if (dip && dip->type && dip->type == T_DIR) 
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
              if (count[de->inum] != 1) 
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
                if (count[de->inum] != 1) 
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
  valid_bitmap_mark();
  valid_direct_address();
  valid_indirect_address();
  valid_inode_mark();
  valid_ref_count();
  valid_dir_links();
  exit(0);

}

