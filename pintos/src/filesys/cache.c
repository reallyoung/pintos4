#include "filesys/cache.h"
#include "filesys/filesys.h"
#include <string.h>
#define WB_TIME 1000
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
    sema_init(&ra_sema,0);
    list_init(&ra_list);
    lock_init(&ra_lock);
    thread_create("bc_write_behind", PRI_DEFAULT, thread_func_write_behind, NULL);
    thread_create("read_ahead", PRI_DEFAULT, thread_func_read_ahead, NULL);
}
int get_bce_idx(block_sector_t s, bool pin)
{
    int i;
    if(s == -1)
        PANIC("want sector -1\n");
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
   // buffer_cache[idx].pinned = true;
    if(s == -1)
        PANIC("sector == -1 get fr block \n");
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
            sema_down(&buffer_cache[i].rws);
            //printf("evict sector %d\n",buffer_cache[i].sector);
            block_write(fs_device, buffer_cache[i].sector,
                    buffer_cache[i].data);
            sema_up(&buffer_cache[i].rws);
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
            if(buffer_cache[idx].sector == -1)
                PANIC("sector == -1\n");
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
void thread_func_write_behind(void* aux UNUSED)
{
    int i;
    while(1)
    {
        timer_sleep(WB_TIME);
        for(i=0;i<64;i++)
        {
            if(buffer_cache[i].dirty)
            {
                sema_down(&buffer_cache[i].rws);
                block_write(fs_device, buffer_cache[i].sector, buffer_cache[i].data);
                buffer_cache[i].dirty = false;
                sema_up(&buffer_cache[i].rws);
            }
        }
    }
}
void make_read_ahead(block_sector_t s)
{

}
void thread_func_read_ahead (void *aux UNUSED)
{
    int idx;
    struct list_elem *e;
    struct ra_elem *r;
    block_sector_t sector_idx;
    
while(1)
{
    sema_down(&ra_sema);
    lock_acquire(&ra_lock);
    while(!list_empty(&ra_list))
    {
        e = list_pop_front(&ra_list);
        r = (struct ra_elem*)list_entry(e, struct ra_elem, elem);
        sector_idx = r->s;
        free(r);
        lock_acquire(&bc_lock);
        idx = get_bce_idx(sector_idx,false);
        if(idx == -1)
        {   
            idx = cache_evict();
            get_from_block(sector_idx, idx);
        }
        lock_release(&bc_lock);
    }
    lock_release(&ra_lock);
}   
}
void write_back_all()
{
    int i;
    lock_acquire(&bc_lock);
    for(i=0;i<64;i++)
    {
        if(buffer_cache[i].dirty)
        {
            block_write(fs_device, buffer_cache[i].sector, buffer_cache[i].data);
            //flush_bce(&buffer_cache[i]);
        }
    }
    lock_release(&bc_lock);

}
