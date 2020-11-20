#include <sys/mimiker.h>
#include <sys/pmap.h>
#include <sys/vm_object.h>
#include <sys/vm_pager.h>
#include <sys/vm_physmem.h>
#include <sys/klog.h>

static vm_page_t *dummy_pager_fault(vm_object_t *obj, off_t offset) {
  return NULL;
}

static vm_page_t *anon_pager_fault(vm_object_t *obj, off_t offset) {
  assert(obj != NULL);

  vm_page_t *new_pg = vm_page_alloc(1);
  pmap_zero_page(new_pg);
  vm_object_add_page(obj, offset, new_pg);
  return new_pg;
}

static vm_page_t *shadow_pager_fault(vm_object_t *obj, off_t offset) {
  assert(obj != NULL);
  assert(obj->backing_object != NULL);

  assert(obj != obj->backing_object);

  vm_page_t *pg = NULL;
  vm_object_t *it = obj;
  while (!pg && it->backing_object) {
    vm_object_find_page(it->backing_object, offset);
    it = it->backing_object;
  }

  klog("shadow_pager_fault");
  if (pg) {
    klog("my backing object has this page!");
    vm_page_t *new_pg = vm_page_alloc(1);
    pmap_copy_page(pg, new_pg);
    vm_object_add_page(obj, offset, new_pg);
    new_pg->flags = pg->flags;
    pmap_remove_readonly(new_pg);
    return new_pg;
  } else {
    klog("I have to create this page by myself unfortunately");
    klog("while loop");
    while(it->backing_object) {
      klog("loop: %p %p", it, it->backing_object);
      it = it->backing_object;
    }

    klog("time to call pager!");
    //klog("backing_object: %p, object: %p, pager: %p", obj->backing_object, obj, obj->backing_object->pager->pgr_fault);
    return it->pager->pgr_fault(obj, offset);
  }
}

vm_pager_t pagers[] = {
  [VM_DUMMY] = {.pgr_fault = dummy_pager_fault},
  [VM_ANONYMOUS] = {.pgr_fault = anon_pager_fault},
  [VM_SHADOW] = {.pgr_fault = shadow_pager_fault},
};
