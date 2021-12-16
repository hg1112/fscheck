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


char bitarr[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
#define BITSET(bitmapblocks, blockaddr) ((*(bitmapblocks + blockaddr / 8)) & (bitarr[blockaddr % 8]))

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

//fill the used direct address
void store_dir_indir_address(struct dinode *inode,uint *diraddrscount,uint *indaddrscount){
  uint blockaddress;
  uint *indirect;
  for(i=0;i<NDIRECT;i++){
    blockaddress = inode->addrs[i];
    if(blockaddress == 0){
      continue;
    }
    diraddrscount[blockaddress - freeblock]++;
  }

  blockaddress = inode->addrs[NDIRECT];
  indirect = (uint*)(addr + blockaddress*BLOCK_SIZE);
  blockaddress = inode->addrs[NDIRECT];
  for(i=0; i < NINDIRECT;i++,indirect++){
    blockaddress = *(indirect);
    if(blockaddress==0){
      continue;
    }
    indaddrscount[blockaddress - freeblock]++;
  }
}


void valid_addres_use(){
  int i;

  //storing used address count
  uint diraddrscount[sb->nblocks];
  memset(diraddrscount,0,sizeof(uint)* sb->nblocks);

  //storing used indirect count
  uint indaddrscount[sb->nblocks];
  memset(indaddrscount,0,sizeof(uint)* sb->nblocks);

  dip = (struct dinode*)(char *)(addr + IBLOCK(ROOTINO)*BLOCK_SIZE);
  //inode = (struct dinode*)(image->inodeblocks);
  //(char *)(mmapimage+2*BLOCK_SIZE);

  for(i=0;i<sb->ninodes;i++,dip++){
    if(dip->type == 0)
      continue;

    store_dir_indir_address(dip,diraddrscount,indaddrscount);

  }
  for(i=0;i<sb->nblocks;i++){
    if(diraddrscount[i]>1){
      fprintf(stderr, "ERROR: direct address used more than once.\n");
      exit(1);
    }
    if(indaddrscount[i]>1){
      fprintf(stderr, "ERROR: indirect address used more than once.\n" );
      exit(1);
    }
  }
}

void get_used_dbs(struct dinode *inode, int *used_dbs){
  int j;
  uint blockaddress;
  uint *indirect;
  uint numinodeblocks=(sb->ninodes/(IPB))+1;
  uint ninodeblocks = (sb->ninodes/(IPB)) + 1;
  uint nbitmapblocks = (sb->size/(BPB)) + 1;
  uint countInodeBlocks = ((sb->ninodes/IPB) + 1);
  uint countBitmapBlocks = ((sb->size/BPB) + 1);
  uint dBlockStart = countBitmapBlocks + countInodeBlocks + 2;
  uint firstdatablockValue = ninodeblocks + nbitmapblocks;


  for(i=0; i<(NDIRECT+1);i++){
    blockaddress = inode->addrs[i];
    if(blockaddress == 0){
      continue;
    }

     // printf("%s %d \n","test=",i );
    used_dbs[blockaddress - dBlockStart]=1;

  //   blockaddress = inode->addrs[NDIRECT];
  // indirect = (uint*)(addr + blockaddress*BLOCK_SIZE);
  // blockaddress = inode->addrs[NDIRECT];
  // for(i=0; i < NINDIRECT;i++,indirect++){
  //   blockaddress = *(indirect);
  //   if(blockaddress==0){
  //     continue;
  //   }
  //   indaddrscount[blockaddress - freeblock]++;
  // }

    if(i==NDIRECT){
      printf("%s %d \n","test=",i );
      indirect = (uint *)(addr + blockaddress*BLOCK_SIZE);
      blockaddress = inode->addrs[NDIRECT];
      for(j=0;j<NINDIRECT;j++,indirect++){
        blockaddress = *(indirect);
        if(blockaddress == 0){
          continue;
        }
        used_dbs[blockaddress - dBlockStart]=1;
      }
    }
  }
}



void valid_bitmap(){
  int used_dbs[sb->nblocks];
  uint blockaddress;
  memset(used_dbs,0,sb->nblocks*sizeof(int));
  printf("%s \n","test2" );
  dip = (struct dinode*)(char *)(addr + IBLOCK(ROOTINO)*BLOCK_SIZE);
  char *inodeblocks = (char *)(addr + 2*BLOCK_SIZE);
  char *bitmapblocks = (char *)(inodeblocks + bitblocks*BLOCK_SIZE);

  printf("%s \n","test3" );

  for(i=0;i<sb->ninodes;i++,dip++){
    if(dip->type == 0){
      continue;
    }
    // printf("%s %d \n","test=",i );

    get_used_dbs(dip,used_dbs);
  }
  printf("%s \n","test" );
  for(i=0;i<sb->nblocks;i++){
    blockaddress = (uint)(i+freeblock);
    if(used_dbs[i]==0 && BITSET(bitmapblocks,blockaddress)){
      fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
      exit(1);
    }
  }


}

void search_directory(struct dinode *rootinode, int *inodemap){
    int i, j;
    uint blockaddress;
    uint *indirect;
    struct dinode *inode;
    struct dirent *dir;
    char *inodeblocks = (char *)(addr + IBLOCK(ROOTINO)*BLOCK_SIZE);
    //(struct dinode*)(char *)(addr + IBLOCK(ROOTINO)*BLOCK_SIZE);
    //(char *)(mmapimage+2*BLOCK_SIZE);
    
    if(rootinode->type==T_DIR){
        for(i=0; i<NDIRECT; i++) {
            blockaddress=rootinode->addrs[i];
            if(blockaddress==0){
                continue;
            }

            dir=(struct dirent *)(addr+blockaddress*BLOCK_SIZE);
            for(j=0; j<DPB; j++, dir++) {
                if(dir->inum!=0 && strcmp(dir->name, ".")!=0 && strcmp(dir->name, "..")!=0){
                    inode=((struct dinode *)(inodeblocks))+dir->inum;
                    inodemap[dir->inum]++;
                    search_directory(inode, inodemap);
                }
            }
        }

        //traverse indirect address
        blockaddress=rootinode->addrs[NDIRECT];
        if(blockaddress!=0){
            indirect=(uint *)(addr+blockaddress*BLOCK_SIZE);
            for(i=0; i<NINDIRECT; i++, indirect++){
                blockaddress=*(indirect);
                if(blockaddress==0){
                    continue;
                }

                dir=(struct dirent *)(addr+blockaddress*BLOCK_SIZE);

                for(j=0; j<DPB; j++, dir++){
                    if(dir->inum!=0 && strcmp(dir->name, ".")!=0 && strcmp(dir->name, "..")!=0){
                        inode=((struct dinode *)(inodeblocks))+dir->inum;
                        inodemap[dir->inum]++;
                        search_directory(inode, inodemap);
                    }
                }
            }
        }
    }
}

void valid_directory_2()
{
  int i;
  int inodearr[sb->ninodes];
  memset(inodearr,0,sizeof(int)* sb->ninodes);
  struct dinode  *rootinode;

  
  dip = (struct dinode*)(char *)(addr + IBLOCK(ROOTINO)*BLOCK_SIZE);
  
  rootinode = ++dip;
  inodearr[0]++;
  inodearr[1]++;

  search_directory(rootinode,inodearr);

  dip++;

  for(i=2;i<sb->ninodes;i++,dip++){
    //rule 9
        if(dip->type!=0 && inodearr[i]==0){
            fprintf(stderr,"ERROR: inode marked use but not found in a directory.\n");
            exit(1);
        }

        //rule 10
        if(inodearr[i]>0 && dip->type==0){
            fprintf(stderr,"ERROR: inode referred to in directory but marked free.\n");
            exit(1);
        }

        //rule 11
        //reference count check for all files.
        if(dip->type==T_FILE && dip->nlink!=inodearr[i]){
            fprintf(stderr,"ERROR: bad reference count for file.\n");
            exit(1);
        }
    
        //rule 12
        if(dip->type==T_DIR && inodearr[i]>1){
            fprintf(stderr,"ERROR: directory appears more than once in file system.\n");
            exit(1);
          }
  }

}

void check_bitmap_addr(){

dip = (struct dinode*)(char *)(addr + IBLOCK(ROOTINO)*BLOCK_SIZE);
int j,k;
char *inodeblocks=(char *)(addr+2*BLOCK_SIZE);
uint numinodeblocks=(sb->ninodes/(IPB))+1;
char *bitmapblocks=(char *)(inodeblocks +numinodeblocks*BLOCK_SIZE);
//char *bitmapblocks = (char *)(inodeblocks + bitblocks*BLOCK_SIZE);

  for (i = 0; i < sb->ninodes; i++,dip++) 
  {
    if(dip->type==0){
      continue;
    }
    
    

    
    uint blockaddr;
    uint *indirect;
    
    for(j=0; j<(NDIRECT+1); j++){
        blockaddr=dip->addrs[j];
        if(blockaddr==0){
            continue;
        }
        
        if(!BITSET(bitmapblocks, blockaddr)){
            fprintf(stderr,"ERROR: address used by inode but marked free in bitmap.\n");
            exit(1);
        }
        
        //for indirect address.
        if(j==NDIRECT){
            indirect=(uint *)(addr+blockaddr*BLOCK_SIZE);
            for(k=0; k<NINDIRECT; k++, indirect++){
                blockaddr=*(indirect);
                if(blockaddr==0){
                    continue;
                }
                
                if(!BITSET(bitmapblocks, blockaddr)){
                    fprintf(stderr,"ERROR: address used by inode but marked free in bitmap.\n");
                    exit(1);
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
  check_bitmap_addr();
  valid_addres_use();
  valid_directory_2();
  exit(0);

}

