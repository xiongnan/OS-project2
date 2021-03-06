#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

struct lock filesys_lock;

struct process_file {
  struct file *file;
  int fd;
  struct list_elem elem;
};

int process_add_file (struct file *f);
struct file* process_get_file (int fd);

static void syscall_handler (struct intr_frame *);

void check_ptr(const void * ptr);
void check_buffer (void* buffer, unsigned size);


void syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  check_ptr((const void*) f->esp);
  uint32_t * esp = f->esp;
  int syscall_number = *esp;
  switch (syscall_number)
  {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      check_ptr(esp + 1);
	    exit(*(esp + 1));
	    break;
    case SYS_EXEC:
	     //arg[0] = user_to_kernel_ptr((const void *) arg[0]);
      check_ptr(esp + 1);
	    f->eax = exec((char *) *(esp + 1)); 
	    break;
    case SYS_WAIT:
      check_ptr(esp + 1);
	    f->eax = wait(*(esp + 1));
	    break;
    case SYS_CREATE:
	     //arg[0] = user_to_kernel_ptr((const void *) arg[0]);
      check_ptr(esp + 1);
      check_ptr(esp + 2);
	    f->eax = create((char *) *(esp + 1), *(esp + 2));
	    break;
    case SYS_REMOVE:
      check_ptr(esp + 1);
	    f->eax = remove((char *) *(esp + 1));
	    break;
    case SYS_OPEN:
      check_ptr(esp + 1);
	    f->eax = open((char *) *(esp + 1));
	    break; 		
    case SYS_FILESIZE:
       check_ptr(esp + 1);
	     f->eax = filesize(*(esp + 1));
	     break;
    case SYS_READ:
       check_ptr(esp + 1);
       check_ptr(esp + 2);
       check_ptr(esp + 3);
	     f->eax = read(*(esp + 1), (void *) *(esp + 2), *(esp + 3));
	     break;
    case SYS_WRITE:
       check_ptr(esp + 1);
       check_ptr(esp + 2);
       check_ptr(esp + 3);
	     f->eax = write(*(esp + 1), (void *) *(esp + 2), *(esp + 3));
	     break;
    case SYS_SEEK:
       check_ptr(esp + 1);
       check_ptr(esp + 2);
	     seek(*(esp + 1), *(esp + 2));
	     break;
    case SYS_TELL:
       check_ptr(esp + 1);
	     f->eax = tell(*(esp + 1));
	     break;
    case SYS_CLOSE:
       check_ptr(esp + 1);
	     close(*(esp + 1));
	     break;
    default:
       exit(ERROR);
       break;
    }
}

void halt (void)
{
  shutdown_power_off();
}

void exit (int status)
{
  struct thread *cur = thread_current();
  if (thread_alive(cur->parent))
    {
      cur->cp->status = status;
    }
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit();
}

pid_t exec (const char *cmd_line)
{
  check_ptr(cmd_line);
  cmd_line = pagedir_get_page(thread_current()->pagedir, cmd_line);
  if (!cmd_line)
  {
      exit(ERROR);
  }
  //cmd_line=user_to_kernel_ptr(cmd_line);
  pid_t pid = process_execute(cmd_line);
  struct child_process* cp = get_child_process(pid);
  ASSERT(cp);
  while (cp->load == NOT_LOADED)
    {
      barrier();
    }
  if (cp->load == LOAD_FAIL)
    {
      return ERROR;
    }
  return pid;
}

int wait (pid_t pid)
{
  return process_wait(pid);
}

bool create (const char *file, unsigned initial_size)
{
  check_ptr(file);
  lock_acquire(&filesys_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return success;
}

bool remove (const char *file)
{
  check_ptr(file);
  lock_acquire(&filesys_lock);
  bool success = filesys_remove(file);
  lock_release(&filesys_lock);
  return success;
}

int open (const char *file)
{
  check_ptr(file);
  lock_acquire(&filesys_lock);
  struct file *f = filesys_open(file);
  if (!f)
    {
      lock_release(&filesys_lock);
      return ERROR;
    }
  int fd = process_add_file(f);
  lock_release(&filesys_lock);
  return fd;
}

int filesize (int fd)
{
  lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&filesys_lock);
      return ERROR;
    }
  int size = file_length(f);
  lock_release(&filesys_lock);
  return size;
}

int read (int fd, void *buffer, unsigned size)
{
  check_buffer(buffer, size);
  if (fd == STDIN_FILENO)
    {
      unsigned i;
      uint8_t* local_buffer = (uint8_t *) buffer;
      for (i = 0; i < size; i++)
	{
	  local_buffer[i] = input_getc();
	}
      return size;
    }
  lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&filesys_lock);
      return ERROR;
    }
  int bytes = file_read(f, buffer, size);
  lock_release(&filesys_lock);
  return bytes;
}

int write (int fd, const void *buffer, unsigned size)
{
  check_buffer(buffer, size);
  if (fd == STDOUT_FILENO)
    {
      putbuf(buffer, size);
      return size;
    }
  lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&filesys_lock);
      return ERROR;
    }
  int bytes = file_write(f, buffer, size);
  lock_release(&filesys_lock);
  return bytes;
}

void seek (int fd, unsigned position)
{
  lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&filesys_lock);
      return;
    }
  file_seek(f, position);
  lock_release(&filesys_lock);
}

unsigned tell (int fd)
{
  lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&filesys_lock);
      return ERROR;
    }
  off_t offset = file_tell(f);
  lock_release(&filesys_lock);
  return offset;
}

void close (int fd)
{
  lock_acquire(&filesys_lock);
  process_close_file(fd);
  lock_release(&filesys_lock);
}




/* helper */

void check_ptr(const void * ptr)
{
  if (ptr == NULL || !is_user_vaddr (ptr)) {
    return exit(ERROR);
  }
  struct thread * cur = thread_current ();
  void * res = pagedir_get_page (cur->pagedir, ptr);
  if (res == NULL) {
    exit(ERROR);
  }
}

void check_buffer (void* buffer, unsigned size)
{
  unsigned i;
  char* local_buffer = (char *) buffer;
  for (i = 0; i < size; i++)
    {
      check_ptr((const void*) local_buffer);
      local_buffer++;
    }
}


int process_add_file (struct file *f)
{
  struct process_file *pf = malloc(sizeof(struct process_file));
  pf->file = f;
  pf->fd = thread_current()->fd;
  thread_current()->fd++;
  list_push_back(&thread_current()->file_list, &pf->elem);
  return pf->fd;
}

struct file* process_get_file (int fd)
{
  struct thread *t = thread_current();
  struct list_elem *e;

  for (e = list_begin (&t->file_list); e != list_end (&t->file_list);
       e = list_next (e))
        {
          struct process_file *pf = list_entry (e, struct process_file, elem);
          if (fd == pf->fd)
	    {
	      return pf->file;
	    }
        }
  return NULL;
}

void process_close_file (int fd)
{
  struct thread *t = thread_current();
  struct list_elem *next, *e = list_begin(&t->file_list);

  while (e != list_end (&t->file_list))
    {
      next = list_next(e);
      struct process_file *pf = list_entry (e, struct process_file, elem);
      if (fd == pf->fd || fd == CLOSE_ALL)
	{
	  file_close(pf->file);
	  list_remove(&pf->elem);
	  free(pf);
	  if (fd != CLOSE_ALL)
	    {
	      return;
	    }
	}
      e = next;
    }
}

struct child_process* add_child_process (int pid)
{
  struct child_process* cp = malloc(sizeof(struct child_process));
  cp->pid = pid;
  cp->load = NOT_LOADED;
  cp->wait = false;
  cp->exit = false;
  lock_init(&cp->wait_lock);
  list_push_back(&thread_current()->child_list,
		 &cp->elem);
  return cp;
}

struct child_process* get_child_process (int pid)
{
  struct thread *t = thread_current();
  struct list_elem *e;

  for (e = list_begin (&t->child_list); e != list_end (&t->child_list);
       e = list_next (e))
        {
          struct child_process *cp = list_entry (e, struct child_process, elem);
          if (pid == cp->pid)
	    {
	      return cp;
	    }
        }
  return NULL;
}

void remove_child_process (struct child_process *cp)
{
  list_remove(&cp->elem);
  free(cp);
}

void remove_child_processes (void)
{
  struct thread *t = thread_current();
  struct list_elem *next, *e = list_begin(&t->child_list);

  while (e != list_end (&t->child_list))
    {
      next = list_next(e);
      struct child_process *cp = list_entry (e, struct child_process,
					     elem);
      list_remove(&cp->elem);
      free(cp);
      e = next;
    }
}



