#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define MAX_FILE_SIZE (1<<23)
#define BLOCK_ENTRY_NUM 128
struct BD{
    block_sector_t bde[BLOCK_ENTRY_NUM];
};
struct BT{
    block_sector_t bte[BLOCK_ENTRY_NUM];
};
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. *///now unused
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t parent;
    int isdir;
    block_sector_t bd;  //block directory sector
    int bt_num;
    int alloc_num;
    uint32_t unused[120];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    block_sector_t parent;
    int isdir;
    block_sector_t bd;
    int bt_num;
    int alloc_num;
    struct inode_disk data;             /* Inode content. */
    struct BD block_directory;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
block_sector_t bd_index(block_sector_t s)
{
    if(s==0)
        PANIC("no bdi for sector 0\n");
    else
        return DIV_ROUND_UP(s,BLOCK_ENTRY_NUM) - 1;
}
block_sector_t bt_index(block_sector_t s)
{
    if(s==0)
        PANIC("no index for sector 0\n");
    else
        return (s-1)%BLOCK_ENTRY_NUM;
}
void dump_bt(struct BT* bt)
{
    int i;
    printf("dump bt\n");
    for(i=0;i<BLOCK_ENTRY_NUM;i++)
        printf("%d. %d\n",i,bt->bte[i]);
}
block_sector_t b2s(off_t b)
{
    return (b/BLOCK_SECTOR_SIZE) + 1;
}
/*
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}
*/
static block_sector_t
bd_byte_to_sector (const struct inode *inode, off_t pos)
{
    struct BT bt;
    block_sector_t i;
    int j;
    i = bd_index(b2s(pos));
    ASSERT(inode != NULL);
    if(pos<= inode->data.length)
    {
       // printf("%d\n",i);
        if(inode->block_directory.bde[i] == -1 )
            PANIC("bde is -1\n");
        cache_read(inode->block_directory.bde[i], (uint8_t*)&bt, 0, BLOCK_SECTOR_SIZE);
        if(bt.bte[bt_index(b2s(pos))] == -1)
        {
            printf("dump bt -1 bte is %d\n",bt_index(b2s(pos)));
            for(j=0;j<BLOCK_ENTRY_NUM;j++)
            printf("%d. %d\n",j,bt.bte[j]);
        }
        return bt.bte[bt_index(b2s(pos))];
    }
    else
    {
        PANIC("byond length byte\n");
        return -1;
    }
}
/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  bc_init();
}
void close_bd(struct inode* inode)
{
    struct BT bt;
    int alloc_num = inode->alloc_num;
    block_sector_t bt_num = inode->bt_num;
    block_sector_t sectors;
    int i,j,k;
    sectors = bytes_to_sectors (inode->data.length);
    alloc_num -= 1;
    for(i=0;bt_num > i; i++)
    {
        alloc_num--;
        cache_read(inode->block_directory.bde[i], (uint8_t*)&bt, 0, BLOCK_SECTOR_SIZE);
        for(j=0;j<BLOCK_ENTRY_NUM&&alloc_num > 0;j++)
        {
            free_map_release(bt.bte[j],1);
            alloc_num--;
        }
        free_map_release(inode->block_directory.bde[i],1);
    }
    free_map_release(inode->bd,1);
    free_map_release(inode->sector,1);
    if(alloc_num != 0)
        PANIC("alloc_num !=0 is %d\n",alloc_num);
}
bool install_bd (block_sector_t sector,struct inode_disk* di, block_sector_t s)
{
    struct BD bd;
    struct BT bt;
    block_sector_t bt_num;
    block_sector_t alloc_num;
    int i;
    int j;
    int k;
    block_sector_t *temp;
    static char zeros[BLOCK_SECTOR_SIZE];
    bool success = true;
    bt_num = DIV_ROUND_UP(s,BLOCK_ENTRY_NUM);
    alloc_num = 1 + bt_num + s; // bd + bt + data
    temp = (block_sector_t*)malloc(sizeof(block_sector_t) * alloc_num);
    for(i=0; i<alloc_num ; i++)//allocate frist
    {
        if(!free_map_allocate(1, &temp[i]))
        {
            success = false;
            for(i = i-1;i >= 0;i --)
                free_map_release(temp[i], 1);
            PANIC("install_bd_fail\n");
            goto IEND;
        }
    }
    
    //di->start = temp[--alloc_num];
    di->bd = temp[--alloc_num];
    di->bt_num = bt_num;
    di->alloc_num = alloc_num + 1;
    for(i=0;i<BLOCK_ENTRY_NUM;i++)
        bd.bde[i]= -1;

    for(i = 0; i < bt_num; i++)
    {
        for(j=0;j<BLOCK_ENTRY_NUM;j++)
            bt.bte[j] = -1;

        bd.bde[i] = temp[--alloc_num];
        for(j = 0 ; j< BLOCK_ENTRY_NUM && alloc_num> 0 ;j++)
        {
            bt.bte[j] = temp[--alloc_num];
            //install data
            block_write(fs_device, bt.bte[j], zeros);
        }
        //install bt
        block_write(fs_device, bd.bde[i], &bt);
    }
    //install bd
    block_write(fs_device, di->bd, &bd);
    //install disk_inode
    block_write(fs_device, sector, di);
    if(alloc_num != 0)
        PANIC("alloc num is not 0 is %d\n",alloc_num);

IEND:
    free(temp);
    return success;

}
bool file_growth(struct inode* inode, size_t size)
{
    int old_bt_num = inode->bt_num;
    int new_bt_num;
    int add_bt_num;
    int old_alloc_num = inode->alloc_num;
    int new_alloc_num;
    int add_alloc_num;
    int add_sectors;
    struct BT bt;
    off_t length;
    off_t new_length;
    off_t how_much;
    block_sector_t old_bt_index;
    int i,j,k;
    bool success = true;
    block_sector_t *temp;
    struct BD* bdp;
    static char zeros[BLOCK_SECTOR_SIZE];
    length = inode_length(inode);
    how_much = size - length;
    add_sectors = bytes_to_sectors(size) - bytes_to_sectors(length);
    new_bt_num = DIV_ROUND_UP(bytes_to_sectors(size) , BLOCK_ENTRY_NUM);
    add_bt_num = new_bt_num - old_bt_num;
    add_alloc_num = add_bt_num + add_sectors;
    new_alloc_num = old_alloc_num + add_alloc_num;
    temp = (block_sector_t*)malloc(sizeof(block_sector_t) * add_alloc_num);
    bdp= &inode->block_directory;
   // printf("file_growth called add_alloc_num =%d, add_sectors=%d, add_bt_num=%d\n",add_alloc_num,add_sectors,add_bt_num);
   // printf("\n");
//   if(add_sectors> 1)
//       PANIC("add_sectos = %d\n",add_sectors);
  //  if(add_bt_num > 0)
  //   printf("add_btnum %d, add_sector_num =%d, alloc %d\n",add_bt_num, add_sectors,add_alloc_num);

    if(add_alloc_num == 0)
        goto FGEND2;
    for(i=0;i<add_alloc_num;i++)
    {
        if(!free_map_allocate(1,&temp[i]))
        {
            PANIC("allocate fail\n");
            success = false;
            for(i= i-1;i>=0;i--)
              free_map_release(temp[i], 1);
            goto FGEND;
        }
    }
    if(length == 0)
        goto LENZERO; 


   // printf("inode->data.bt_num = %d length =%d size = %d\n",inode->data.bt_num,inode->data.length, size);
    old_bt_index = bt_index( b2s(length));
   // printf("old_bt_index = %d, length = %d\n",old_bt_index,length);

    if(old_bt_index != BLOCK_ENTRY_NUM -1)
    {
        cache_read(inode->block_directory.bde[bd_index(b2s(length))],
            (uint8_t*)&bt, 0, BLOCK_SECTOR_SIZE);
        i=0;
        while(old_bt_index+i < BLOCK_ENTRY_NUM - 1 && add_alloc_num >0)
        {   
         i++;
       //  printf("i= %d, old_bt_idx= %d, alloc_num=%d, sector = %d\n",i,old_bt_index,add_alloc_num,temp[add_alloc_num -1]);
         bt.bte[old_bt_index+i] = temp[--add_alloc_num];
         block_write(fs_device,bt.bte[old_bt_index+i],zeros);
        }//allocate partial segment in last bt
    /*    printf("check bde[%d]= %d\n",bd_index(length/BLOCK_SECTOR_SIZE)
                ,inode->block_directory.bde[bd_index(length/BLOCK_SECTOR_SIZE)]);

        dump_bt(&bt);
        */
        //update BT to disk
        cache_write(inode->block_directory.bde[bd_index(b2s(length))],
            (uint8_t*)&bt, 0, BLOCK_SECTOR_SIZE);
    /*    cache_read(inode->block_directory.bde[bd_index(length/BLOCK_SECTOR_SIZE)-1],
                (uint8_t*)&bt, 0, BLOCK_SECTOR_SIZE);
        printf("dump -1\n");
        dump_bt(&bt);
    */
    }
LENZERO:

    for(i=0;i<add_bt_num;i++)
    {
        for(j = 0 ;j<BLOCK_ENTRY_NUM;j++)
            bt.bte[j] = -1;
        inode->block_directory.bde[old_bt_num+i] = temp[--add_alloc_num];
        for(j=0;j<BLOCK_ENTRY_NUM && add_alloc_num>0;j++)
        {
            bt.bte[j] = temp[--add_alloc_num];
            block_write(fs_device,bt.bte[j],zeros);
        }
        block_write(fs_device,inode->block_directory.bde[old_bt_num+i],&bt);
    }
    if(add_alloc_num != 0)
        PANIC("add_alloc_num = %d\n",add_alloc_num);
    //update bd
    block_write(fs_device,inode->bd,&inode->block_directory);
    //update inode
    inode->bt_num = new_bt_num;
    inode->alloc_num = new_alloc_num;
    inode->data.bt_num = new_bt_num;
    inode->data.alloc_num = new_alloc_num;
    //for debug

FGEND2:
    inode->data.length = size;
    block_write(fs_device,inode->sector, &inode->data);
FGEND:
    free(temp);
    return success;
}
/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, int isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->isdir = isdir;
      //printf("create length is %d sectors = %d\n",length,sectors);
      //have to change
      /*
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } 
        */
     success = install_bd(sector,disk_inode, sectors);
     //have to change
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  inode->isdir = inode->data.isdir;
  inode->parent = inode->data.parent;
  inode->bd = inode->data.bd;
  inode->bt_num = inode->data.bt_num;
  inode->alloc_num = inode->data.alloc_num;
  block_read(fs_device, inode->bd, &inode->block_directory);
  //cache block_directory in memory for performance
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
    //have to change
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
            close_bd(inode);
          //free_map_release (inode->sector, 1);
          //free_map_release (inode->data.start,
          //                  bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0 && offset<=inode_length(inode)) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = bd_byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
  /*  
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Read full sector directly into caller's buffer. 
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          // Read sector into bounce buffer, then partially copy
           //  into caller's buffer.
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
*/
      cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
        if(size - chunk_size > 0)
        {
            struct ra_elem *r;
            r = (struct ra_elem*)malloc(sizeof(struct ra_elem));
            r->s = bd_byte_to_sector (inode, offset + chunk_size);
            lock_acquire(&ra_lock);
            list_push_back(&ra_list,&r->elem);
            lock_release(&ra_lock);
            sema_up(&ra_sema);
            // index file system
        }
      // Advance. 
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
//  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if(offset+size > inode_length(inode))
  {
      if(offset+size > MAX_FILE_SIZE)
          PANIC("two large file\n");
  //    printf("size-length in write = %d\n", offset+size - inode_length(inode));
      if(!file_growth(inode, offset + size))
          PANIC("file_growth fail\n");
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = bd_byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
/*
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
         // Write full sector directly to disk. 
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          // We need a bounce buffer. 
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          // If the sector contains data before or after the chunk
          //   we're writing, then we need to read in the sector
          //   first.  Otherwise we start with a sector of all zeros.
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }
*/
    cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  //free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
