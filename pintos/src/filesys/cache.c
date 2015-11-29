#include "filesys/cache.h"
#include "filesys/filesys.h"
#include <string.h>
void init_bce(struct bce* b)
{
    b->accessed = false;
    b->dirty = false;
    b->valid = false;
    b->pinned = false;
    b->sector = 0;
    memset(b->data, 0, 512);
    sema_init(&b->rs,1);
    sema_init(&b->rws,1);
    b->read_count = 0;
}
void flush_bce(struct bce* b)
{
    sema_down(&b->rs);
    sema_down(&b->rws);
    b->accessed = false;
    b->dirty = false;
    b->valid = false;
    b->pinned = false;
    b->read_count = 0;
    memset(b->data, 0, 512);
    sema_up(&b->rs);
    sema_up(&b->rws);
}
void bc_init()
{
    int i;
    lock_init(&bc_lock);
    for(i=0;i<64;i++)
    {
        init_bce(&buffer_cache[i]);
    }
}
int get_bce_idx(block_sector_t s, bool pin)
{
    int i;
    for(i=0; i<64;i++)
    {
      if(buffer_cache[i].valid && buffer_cache[i].sector == s)
      {
        if(pin)
            buffer_cache[i].pinned = true;
        return i;
      }
    }
    return -1;
}
void get_from_block(block_sector_t s,int idx)
{
    if(buffer_cache[idx].valid)
        PANIC("valid collision\n");
    block_read(fs_device, s, buffer_cache[idx].data);
    buffer_cache[idx].valid = true;
    buffer_cache[idx].pinned = true;
    buffer_cache[idx].sector = s;
}
void bce_read(struct bce* b,const uint8_t* buffer_, off_t size, off_t ofs)
{
    uint8_t *buffer = buffer_;
    //printf("bce read called\n");
    if(512 - ofs < size)
        PANIC("read beyond cache block\n");
    sema_down(&b->rs);
    b->read_count++;
    if(b->read_count == 1)// first reader
        sema_down(&b->rws);
    sema_up(&b->rs);
    memcpy(buffer, &b->data[ofs] , size);//read performed
    b->accessed = true;
    sema_down(&b->rs);
    b->read_count--;
    if(b->read_count == 0)//last reader
    {
        sema_up(&b->rws);
        b->pinned = false;
    }
    sema_up(&b->rs);
}
void bce_write(struct bce* b,const uint8_t* buffer_, off_t size, off_t ofs)
{
    uint8_t *buffer = buffer_;
     if(512 - ofs < size)
        PANIC("write beyond cache block\n");
     sema_down(&b->rws);
     memcpy(&b->data[ofs], buffer, size);
     b->dirty = true;
     b->accessed = true;
     b->pinned = false;
     sema_up(&b->rws);
}
int cache_evict()
{
    int i=0;
    while(1)
    {
        if(!buffer_cache[i].valid)
            return i;
        if(buffer_cache[i].pinned)
            goto NEXT;
        if(buffer_cache[i].accessed)
        {
            buffer_cache[i].accessed = false;
            goto NEXT;
        }
        if(buffer_cache[i].dirty)
        {
            block_write(fs_device, buffer_cache[i].sector,
                    buffer_cache[i].data);
        }
        flush_bce(&buffer_cache[i]);
        return i;
NEXT:
        i++;
        if(i == 64)
            i=0;
    }
}
void cache_write(block_sector_t sector_idx, const uint8_t *buffer_, off_t ofs, off_t size)
{
    int idx;
    const uint8_t *buffer = buffer_;
    lock_acquire(&bc_lock);
    idx = get_bce_idx(sector_idx,true);
    if(idx == -1)
    {   
        idx = cache_evict();
//        printf("write get %d\n",idx);
        if(ofs != 0)
            get_from_block(sector_idx, idx);
        else
        {
            buffer_cache[idx].valid = true;
            buffer_cache[idx].sector = sector_idx;
            buffer_cache[idx].pinned = true;
        }
    }
    lock_release(&bc_lock);
//    printf("write call %d\n",idx);
    bce_write(&buffer_cache[idx], buffer, size, ofs);
}
void cache_read(block_sector_t sector_idx,const uint8_t *buffer_, off_t ofs, off_t size)
{
    int idx;
    const uint8_t *buffer = buffer_;
    lock_acquire(&bc_lock);
    idx = get_bce_idx(sector_idx,true);
    if(idx == -1)
    {   
        idx = cache_evict();
//        printf("read get %d\n",idx);
        get_from_block(sector_idx, idx);
    }
    lock_release(&bc_lock);
//    printf("bce read %d\n", idx);
    bce_read(&buffer_cache[idx], buffer, size, ofs);
}
