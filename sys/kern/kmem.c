#define KL_LOG KL_KMEM
#include <sys/klog.h>
#include <sys/mimiker.h>
#include <sys/libkern.h>
#include <sys/param.h>
#include <sys/pmap.h>
#include <sys/vmem.h>
#include <sys/vm.h>
#include <sys/vm_physmem.h>
#include <sys/kasan.h>

static vmem_t *kvspace; /* Kernel virtual address space allocator. */

void init_kmem(void) {
  kvspace = vmem_create("kvspace", PAGESIZE);
  if (KERNEL_SPACE_BEGIN < (vaddr_t)__kernel_start)
    vmem_add(kvspace, KERNEL_SPACE_BEGIN,
             (vaddr_t)__kernel_start - KERNEL_SPACE_BEGIN);
  vmem_add(kvspace, (vaddr_t)vm_kernel_end,
           KERNEL_SPACE_END - (vaddr_t)vm_kernel_end);
}

static void kick_swapper(void) {
  panic("Cannot allocate more kernel memory: swapper not implemented!");
}

vaddr_t kva_alloc(size_t size) {
  assert(page_aligned_p(size));
  vmem_addr_t start;
  if (vmem_alloc(kvspace, size, &start, M_NOGROW))
    return 0;
  return start;
}

void kva_free(vaddr_t ptr, size_t size) {
  assert(page_aligned_p(ptr) && page_aligned_p(size));
  vmem_free(kvspace, ptr, size);
}

void kva_map(vaddr_t ptr, size_t size, kmem_flags_t flags) {
  assert(page_aligned_p(size));

  /* Mark the entire block as valid */
  kasan_mark_valid((void *)ptr, size);

  size_t npages = size / PAGESIZE;
  vaddr_t va = ptr;

  while (npages > 0) {
    size_t pagecnt = 1L << log2(npages);
    vm_page_t *pg = vm_page_alloc(pagecnt);
    if (pg == NULL)
      kick_swapper();
    paddr_t pa = pg->paddr;
    for (size_t i = 0; i < pagecnt; i++)
      pmap_kenter(va + PAGESIZE * i, pa + PAGESIZE * i,
                  VM_PROT_READ | VM_PROT_WRITE, 0);
    npages -= pagecnt;
    va += pagecnt * PAGESIZE;
  }

  if (flags & M_ZERO)
    bzero((void *)ptr, size);
}

vm_page_t *kva_find_page(vaddr_t ptr) {
  paddr_t pa;
  if (pmap_extract(pmap_kernel(), ptr, &pa))
    return vm_page_find(pa);
  return NULL;
}

void kva_unmap(vaddr_t ptr, size_t size) {
  assert(page_aligned_p(ptr) && page_aligned_p(size));

  kasan_mark_invalid((void *)ptr, size, KASAN_CODE_KMEM_FREED);

  vaddr_t va = (vaddr_t)ptr;
  vaddr_t end = va + size;
  while (va < end) {
    vm_page_t *pg = kva_find_page(va);
    assert(pg != NULL);
    va += pg->size * PAGESIZE;
    vm_page_free(pg);
  }

  pmap_kremove((vaddr_t)ptr, end);
}

void *kmem_alloc(size_t size, kmem_flags_t flags) {
  assert(page_aligned_p(size));
  assert(!(flags & M_NOGROW));

  vmem_addr_t start;
  if (vmem_alloc(kvspace, size, &start, M_NOGROW))
    kick_swapper();

  kva_map(start, size, flags);

  return (void *)start;
}

void kmem_free(void *ptr, size_t size) {
  klog("%s: free %p of size %ld", __func__, ptr, size);
  kva_unmap((vaddr_t)ptr, size);
  vmem_free(kvspace, (vmem_addr_t)ptr, size);
}

void *kmem_map(paddr_t pa, size_t size) {
  assert(page_aligned_p(pa) && page_aligned_p(size));

  vmem_addr_t start;
  if (vmem_alloc(kvspace, size, &start, M_NOGROW))
    kick_swapper();

  /* Mark the entire block as valid */
  kasan_mark_valid((void *)start, size);

  klog("%s: map %p of size %ld at %p", __func__, pa, size, start);

  for (size_t offset = 0; offset < size; offset += PAGESIZE)
    pmap_kenter(start + offset, pa + offset, VM_PROT_READ | VM_PROT_WRITE, 0);

  return (void *)start;
}
