#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include <string.h>
/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  write_back_all();
  free_map_close ();
}

void last_name(const char* src_, char* dest)
{
    int i;
    char *c; 
    int size;
    char src[strlen(src_)+1];
    memcpy(src,src_,strlen(src_)+1);
//    printf("2\n");
    size = strlen(src);
    if(size>1)
    {
    for(i = size-1; i>=0; i--)
    {
        if(src[i] =='/')
        { 
//            printf("4 '%s', size %d, i %d\n",src,size,i);
            memcpy(dest,&src[i+1],size-i);
            return;
        }
    }
    }
//    printf("3\n");
    memcpy(dest,src,size+1);
}

struct dir* find_dir(const char* name)
{
//    PANIC("2\n");
    struct dir* cwd;// = thread_current()->wd;
    char *token, *save_ptr, *nt;
    struct inode* inode;
    int len = strlen(name);
    char str[len+1];
//    printf("in find_dir name = '%s'\n",name);
    if(!thread_current()->wd)
        thread_current()->wd = dir_open_root();
    cwd = thread_current()->wd;
    memcpy(str,name, len+1);
    if(!strlen(name))
        return NULL;
    if(!strcmp(name,"/"))
        return dir_open_root();
    if(str[0] == '/' || cwd == NULL)
        cwd = dir_open_root();
    else
        cwd = dir_reopen(cwd);
    nt = NULL;
    token = strtok_r(str, "/",&save_ptr);
    nt = strtok_r(NULL, "/",&save_ptr);
    if(nt == NULL)
    {
        inode = get_parent_inode(get_dinode(cwd));
            if(!inode)
                return NULL;
        return cwd;
    }
    do
    {
//        printf("token = '%s'\n",token);
        if(!strcmp(token, ".."))
        {
            inode = get_parent_inode(get_dinode(cwd));
            if(!inode)
                PANIC("inode is NULL\n");
        }
        else if(!strcmp(token, "."))
        {
            goto NEXT_TOK;
        }
        else
        {
            if(!dir_lookup(cwd,token,&inode))
            {
     //          printf("here\n");
                return NULL;
                //PANIC("dir lookup fail\n");
            }
        }
        if(get_isdir(dir_get_inode(cwd)))
        {
            dir_close(cwd);
            cwd = dir_open(inode);
        }
        else
        {
            //PANIC("is not dir\n");
            inode_close(inode);
            return NULL;
        }

NEXT_TOK:
        if(nt == NULL)
            break;
        token = nt;
        nt = strtok_r(NULL, "/",&save_ptr);
    }while(nt != NULL);
    

    return cwd;
}


/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
    //    PANIC("1\n");
    block_sector_t inode_sector = 0;
    if(!thread_current()->wd)
        thread_current()->wd = dir_open_root();
    struct dir *dir = find_dir(name);
    char lname[NAME_MAX+1];
    struct inode* inode;
    if(strlen(name) >NAME_MAX)
    {
        dir_close(dir);
        return false;
    }
//    printf("len =%d\n",strlen(name));
    last_name(name, lname);
//    printf("lname = '%s'\n",lname);
    if(thread_current()->wd == NULL)
        PANIC("thread->wd is NULL\n");
    if(dir == NULL)
        return false;
    if(!strcmp(lname,"/")||!strcmp(lname,".")
            ||!strcmp(lname,".."))
    {
        dir_close(dir);
        return false;
    }
    if(dir_lookup(dir,lname,&inode))//exisit
    {
//        printf("5\n");
        dir_close(dir);
        return false;
    }  
    bool success = (dir != NULL
            && free_map_allocate (1, &inode_sector)
            && inode_create (inode_sector, initial_size, 0,
                get_sector(get_dinode(dir)))
            && dir_add (dir, lname, inode_sector));
//    printf("6 success =%d\n",success);
    if (!success && inode_sector != 0) 
        free_map_release (inode_sector, 1);
    dir_close (dir);
//   printf("7\n");
    return success;
}
bool my_chdir(const char* dir_)
{
    struct thread *t;
    struct dir* dir = find_dir(dir_);
    char lname[NAME_MAX+1];
    struct inode* inode;
    last_name(dir_, lname);
    t=thread_current();
    if(dir== NULL|| !strcmp(dir_, ""))
        return false;
    if(!strcmp(dir_,"/"))
    {
        dir_close(t->wd);
        t->wd =dir_open_root();
        dir_close(dir);
        return true;
    }
    if(!strcmp(lname,""))
    {
        dir_close(t->wd);
        t->wd = dir;
        return true;
    }
    if(!strcmp(lname,"."))
    {
        dir_close(t->wd);
        t->wd = dir;
        return true;
    }
    if(!strcmp(lname,".."))
    {
        dir_close(t->wd);
        t->wd =dir_open(get_parent_inode(get_dinode(dir)));
        if(!t->wd)
            PANIC("fail on cd ..\n");
        dir_close(dir);
        return true;
    }
    else
    {
        if(dir_lookup(dir,lname,&inode))
        {
            dir_close(t->wd);
            t->wd=dir_open(inode);
            dir_close(dir);
            return true;
        }
        else
        {
            dir_close(dir);
            return false;
        }
    }
}
bool
my_mkdir (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  //struct dir *dir = dir_open_root ();
  struct dir *dir = find_dir(name);
  char lname[NAME_MAX+1];
  struct inode* inode;
  last_name(name, lname);
  if(thread_current()->wd == NULL)
      PANIC("thread->wd is NULL\n");
  if(dir == NULL)
    return false;
  if(!strcmp(lname,"/")||!strcmp(lname,".")
          ||!strcmp(lname,".."))
  {
      dir_close(dir);
      return false;
  }
  if(dir_lookup(dir,lname,&inode))//exisit
  {
      dir_close(dir);
      return false;
  }  
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 1,
                      get_sector(get_dinode(dir)))
                  && dir_add (dir, lname, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  return success;
}
/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir;// = dir_open_root ();
  struct thread* t;
  char lname[NAME_MAX+1];
  struct inode *inode = NULL;
  t= thread_current();
//  printf("8 name = '%s'\n",name);
  if(!t->wd)
      t->wd = dir_open_root();
  if(!strcmp(name,""))
      return NULL;
  dir= find_dir(name);
  last_name(name,lname);
  if (dir != NULL)
  {
      if(!strcmp(lname,".."))
      {
        inode = get_parent_inode(get_dinode(dir));
      }
      else if(!strcmp(lname,"."))
      {
          return (struct file*)dir;
      }
      else if(!strcmp(lname,"/"))
          return (struct file*)dir;
      else
      {
//          printf("here2 dir sector= %d, lname = '%s'\n",
 //                 get_sector(get_dinode(dir)),lname);
          dir_lookup (dir, lname, &inode);
      }
  }
  dir_close (dir);
//  if(!inode)
//      printf("9\n");
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir;// = dir_open_root ();
  char lname[NAME_MAX+1];
  bool success;
  struct inode* inode;
  dir=find_dir(name);
  last_name(name,lname);
  dir_lookup(dir,lname,&inode);
 
  success = dir != NULL && dir_remove (dir, lname);

  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
