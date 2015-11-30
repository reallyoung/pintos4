#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H
#include <stdio.h>
#include <stdint.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "devices/block.h"
#include "devices/timer.h"

struct bce{
    uint8_t data[512];
    bool accessed;
    bool dirty;
    bool valid;
    bool pinned;
    block_sector_t sector;
    struct semaphore rs;
    struct semaphore rws;
    int read_count;
};
struct lock bc_lock;
struct bce buffer_cache[64];

void init_bce(struct bce* b);
void bc_init(void);
void thread_func_write_behind (void *aux);
void thread_func_read_ahead (void *aux);
void cache_write(block_sector_t sector_idx,const uint8_t *buffer_, off_t ofs, off_t size);
void cache_read(block_sector_t sector_idx,const uint8_t *buffer_, off_t ofs, off_t size);
void write_back_all();
#endif
