#define KL_LOG KL_VM
#include <sys/klog.h>
#include <sys/mimiker.h>
#include <sys/pool.h>
#include <sys/pmap.h>
#include <sys/vm_object.h>
#include <sys/vm_physmem.h>

static POOL_DEFINE(P_VMOBJ, "vm_object", sizeof(vm_object_t));

static inline int vm_page_cmp(vm_page_t *a, vm_page_t *b) {
  if (a->offset < b->offset)
    return -1;
  return a->offset - b->offset;
}

RB_PROTOTYPE_STATIC(vm_pagetree, vm_page, obj.tree, vm_page_cmp);
RB_GENERATE(vm_pagetree, vm_page, obj.tree, vm_page_cmp);

vm_object_t *vm_object_alloc(vm_pgr_type_t type) {
  vm_object_t *obj = pool_alloc(P_VMOBJ, M_ZERO);
  TAILQ_INIT(&obj->list);
  TAILQ_INIT(&obj->shadows_list);
  RB_INIT(&obj->tree);
  rw_init(&obj->mtx, NULL, 0);
  obj->pager = &pagers[type];
  obj->ref_counter = 1;
  return obj;
}

static void vm_object_remove_page_nolock(vm_object_t *obj, vm_page_t *page) {
  page->offset = 0;
  page->object = NULL;

  TAILQ_REMOVE(&obj->list, page, obj.list);
  RB_REMOVE(vm_pagetree, &obj->tree, page);

  if (refcnt_release(&page->ref_counter)) {
    vm_page_free(page);
  }

  obj->npages--;
}

vm_page_t *vm_object_find_page_no_lock(vm_object_t *obj, off_t offset) {
  vm_page_t find = {.offset = offset};
  return RB_FIND(vm_pagetree, &obj->tree, &find);
}

bool vm_object_add_page_no_lock(vm_object_t *obj, off_t offset,
                                vm_page_t *page) {
  assert(page_aligned_p(page->offset));
  /* For simplicity of implementation let's insert pages of size 1 only */
  assert(page->size == 1);

  page->object = obj;
  page->offset = offset;

  refcnt_acquire(&page->ref_counter);

  if (!RB_INSERT(vm_pagetree, &obj->tree, page)) {
    obj->npages++;
    vm_page_t *next = RB_NEXT(vm_pagetree, &obj->tree, page);
    if (next)
      TAILQ_INSERT_BEFORE(next, page, obj.list);
    else
      TAILQ_INSERT_TAIL(&obj->list, page, obj.list);
    return true;
  }

  return false;
}

static void merge_shadow(vm_object_t *shadow) {
  vm_object_t *elem;

  TAILQ_FOREACH (elem, &shadow->shadows_list, link) {
    assert(elem != NULL);

    SCOPED_RW_ENTER(&elem->mtx, RW_WRITER);

    vm_page_t *pg;
    TAILQ_FOREACH (pg, &shadow->list, obj.list) {
      if (vm_object_find_page_no_lock(elem, pg->offset) == NULL) {
        TAILQ_REMOVE(&shadow->list, pg, obj.list);
        pg->object = elem;
        vm_object_add_page_no_lock(elem, pg->offset, pg);
      }
    }
  }
}

void vm_object_free(vm_object_t *obj) {
  WITH_RW_LOCK (&obj->mtx, RW_WRITER) {
    if (!refcnt_release(&obj->ref_counter)) {
      return;
    }

    while (!TAILQ_EMPTY(&obj->list)) {
      vm_page_t *pg = TAILQ_FIRST(&obj->list);
      TAILQ_REMOVE(&obj->list, pg, obj.list);

      if (refcnt_release(&pg->ref_counter)) {
        vm_page_free(pg);
      }
    }

    if (obj->shadow_object) {
      vm_object_t *shadow = obj->shadow_object;
      TAILQ_REMOVE(&shadow->shadows_list, obj, link);

      if (shadow->ref_counter == 2) {
        merge_shadow(shadow);
      }

      vm_object_free(obj->shadow_object);
    }
  }
  pool_free(P_VMOBJ, obj);
}

vm_page_t *vm_object_find_page(vm_object_t *obj, off_t offset) {
  SCOPED_RW_ENTER(&obj->mtx, RW_READER);
  return vm_object_find_page_no_lock(obj, offset);
}

bool vm_object_add_page(vm_object_t *obj, off_t offset, vm_page_t *page) {
  SCOPED_RW_ENTER(&obj->mtx, RW_WRITER);
  return vm_object_add_page_no_lock(obj, offset, page);
}

void vm_object_remove_page(vm_object_t *obj, vm_page_t *page) {
  SCOPED_RW_ENTER(&obj->mtx, RW_WRITER);
  vm_object_remove_page_nolock(obj, page);
}

void vm_object_remove_range(vm_object_t *object, off_t offset, size_t length) {
  vm_page_t *pg, *next;

  SCOPED_RW_ENTER(&object->mtx, RW_WRITER);

  TAILQ_FOREACH_SAFE (pg, &object->list, obj.list, next) {
    if (pg->offset >= (off_t)(offset + length))
      break;
    if (pg->offset >= offset)
      vm_object_remove_page_nolock(object, pg);
  }
}

vm_object_t *vm_object_clone(vm_object_t *obj) {
  vm_object_t *new_obj = vm_object_alloc(VM_DUMMY);
  new_obj->pager = obj->pager;

  SCOPED_RW_ENTER(&obj->mtx, RW_READER);

  vm_page_t *pg;
  TAILQ_FOREACH (pg, &obj->list, obj.list) {
    vm_page_t *new_pg = vm_page_alloc(1);
    pmap_copy_page(pg, new_pg);
    vm_object_add_page(new_obj, pg->offset, new_pg);
  }

  return new_obj;
}

void vm_map_object_dump(vm_object_t *obj) {
  vm_page_t *it;

  SCOPED_RW_ENTER(&obj->mtx, RW_READER);
  RB_FOREACH (it, vm_pagetree, &obj->tree)
    klog("(vm-obj) offset: 0x%08lx, size: %ld", it->offset, it->size);
}

void vm_object_set_readonly(vm_object_t *obj) {
  SCOPED_RW_ENTER(&obj->mtx, RW_WRITER);

  vm_page_t *pg;
  TAILQ_FOREACH (pg, &obj->list, obj.list) { pmap_set_page_readonly(pg); }
}

void vm_object_increase_pages_references(vm_object_t *obj) {
  SCOPED_RW_ENTER(&obj->mtx, RW_WRITER);

  vm_page_t *pg;
  TAILQ_FOREACH (pg, &obj->list, obj.list) { refcnt_acquire(&pg->ref_counter); }

  if (obj->shadow_object) {
    vm_object_increase_pages_references(obj->shadow_object);
  }
}