#include <syscallargs.h>
#include <sys/vm.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/filedesc.h>

/* probably move it to the param.h file for proper architecture */
#define PAGE_SHIFT      12      /* LOG2(PAGE_SIZE) */
#define PAGE_SIZE       (1<<PAGE_SHIFT) /* bytes/page */
#define PAGE_MASK       (PAGE_SIZE-1)
#define round_page(x)       (((x) + PAGE_MASK) & ~PAGE_MASK)

static inline bool vm_map_range_valid(vm_map_t *map, vm_offset_t start, vm_offset_t end) {
  if (end < start)
    return false;

  if (start < vm_map_min(map) || end > vm_map_max(map))
    return false;

  return true;
}

int sys_mmap(thread_t *td, const mmap_args_t *mmap_args) {
  /* extract syscall args from mmap_args */
  vaddr_t addr;
  off_t pos;
  proc_t *p;
  size_t len, pageoff, newlen;
  int flags;
  int fd;
  vm_prot_t prot;
  file_t *fp;

  addr = (vaddr_t)mmap_args->addr;
  len = mmap_args->len;
  pos = mmap_args->pos;
  prot = mmap_args->prot;
  flags = mmap_args->flags;
  fd = mmap_args->fd;

  p = td->td_proc;

  /* mapping cannot be shared and private at the same time */
  if ((flags & (MAP_SHARED|MAP_PRIVATE)) == (MAP_SHARED|MAP_PRIVATE))
    return EINVAL;

  /* align page position and update offset */
  pageoff = (pos & PAGE_MASK);
  pos -= pageoff;

  /* calculate size */
  newlen = len + pageoff;
  newlen = round_page(newlen);

  if (newlen < len)
    return ENOMEM;

  len = newlen;

  if (flags & MAP_FIXED) {
    addr -= pageoff;

    if (addr & PAGE_MASK)
      EINVAL;

    /* if new memory cannot fit into the virtual space of the process, then return EINVAL */
    if (!vm_map_address_p(p->p_uspace, addr) || !vm_map_address_p(p->p_uspace, addr + len)) {
      return EINVAL;
    }
  } else {
    /* non-fixed mapping: try to find reasonable position */
    /* TODO(bkjg) */
  }

  int error;
  if ((flags & MAP_ANON) == 0) {
    if ((error = fdtab_get_file(p->p_fdtable, fd, 0, &fp)) != 0)
      return error;

    if (fp->f_ops->fo_mmap == NULL) {
      error = ENODEV;
      /* TODO(bkjg): http://bxr.su/NetBSD/sys/uvm/uvm_mmap.c#361 */
    }

    error = (*fp->f_ops->fo_mmap)(fp, td, pos, len, prot, flags);
  }




}