#include "utest.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

#define mmap_anon_prw(addr, length)                                            \
  mmap((addr), (length), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)

static void mmap_no_hint(void) {
  void *addr = mmap_anon_prw(NULL, 12345);
  assert(addr != MAP_FAILED);
  printf("mmap returned pointer: %p\n", addr);
  /* Ensure mapped area is cleared. */
  assert(*(char *)(addr + 10) == 0);
  assert(*(char *)(addr + 1000) == 0);
  /* Check whether writing to area works correctly. */
  memset(addr, -1, 12345);
}

#define TESTADDR (void *)0x12345000
static void mmap_with_hint(void) {
  /* Provide a hint address that is page aligned. */
  void *addr = mmap_anon_prw(TESTADDR, 99);
  assert(addr != MAP_FAILED);
  assert(addr >= TESTADDR);
  printf("mmap returned pointer: %p\n", addr);
  /* Ensure mapped area is cleared. */
  assert(*(char *)(addr + 10) == 0);
  assert(*(char *)(addr + 50) == 0);
  /* Check whether writing to area works correctly. */
  memset(addr, -1, 99);
}
#undef TESTADDR

static void mmap_bad(void) {
  void *addr;
  /* Address range spans user and kernel space. */
  addr = mmap_anon_prw((void *)0x7fff0000, 0x20000);
  assert(addr == MAP_FAILED);
  assert(errno == EINVAL);
  /* Address lies in low memory, that cannot be mapped. */
  addr = mmap_anon_prw((void *)0x3ff000, 0x1000);
  assert(addr == MAP_FAILED);
  assert(errno == EINVAL);
  /* Hint address is not page aligned. */
  addr = mmap_anon_prw((void *)0x12345678, 0x1000);
  assert(addr == MAP_FAILED);
  assert(errno == EINVAL);
}

static void munmap_bad(void) {
  void *addr;
  int result;

  /* mmap & munmap one page */
  addr = mmap_anon_prw(NULL, 0x1000);
  result = munmap(addr, 0x1000);
  assert(result == 0);

  /* munmapping again fails */
  munmap(addr, 0x1000);
  assert(errno == EINVAL);

  /* more pages */
  addr = mmap_anon_prw(NULL, 0x5000);

  /* munmap pieces of segments is unsupported */
  munmap(addr, 0x2000);
  assert(errno == ENOTSUP);

  result = munmap(addr, 0x5000);
  assert(result == 0);
}

/* Don't call this function in this module */
int test_munmap_sigsegv(void) {
  void *addr = mmap_anon_prw(NULL, 0x4000);
  munmap(addr, 0x4000);

  /* Try to access freed memory. It should raise SIGSEGV */
  int data = *((volatile int *)(addr + 0x2000));
  (void)data;
  return 1;
}

int test_mmap(void) {
  mmap_no_hint();
  mmap_with_hint();
  mmap_bad();
  munmap_bad();
  return 0;
}

static volatile int sigsegv_handled = 0;
static jmp_buf return_to;
static void sigsegv_handler(int signo) {
  printf("sigsegv handled!\n");
  sigsegv_handled++;
  printf("Incremented!!!\n");
  longjmp(return_to, 5);
}

int test_mprotect(void) {
  size_t pgsz = getpagesize();
  signal(SIGSEGV, sigsegv_handler);
  void *addr = mmap(NULL, pgsz * 8, PROT_READ, MAP_ANON | MAP_PRIVATE, -1, 0);
  assert(addr != MAP_FAILED);
  printf("mmap returned pointer: %p\n", addr);

  /* Ensure mapped area is cleared. */
  assert(*(char *)(addr + 100) == 0);
  assert(*(char *)(addr + 1000) == 0);

  sigsegv_handled = 0;

  if (setjmp(return_to) == 0) {
    printf("Try to write to readonly memory\n");
    /* Try to write to readonly memory. It should raise SIGSEGV */
    *(char *)addr = '9';
  }

  assert(sigsegv_handled == 1);
  assert(*(char *)addr == 0);

  int error;
  error = mprotect(addr, pgsz, PROT_READ | PROT_WRITE);
  assert(error == 0);

  printf("sigsegv handled = %d\n", sigsegv_handled);
  *(char *)addr = '1';
  assert(*(char *)addr == '1');
  assert(sigsegv_handled == 1);

  printf("Before if\n");
  if (setjmp(return_to) == 0) {
    *(char *)(addr + pgsz) = 7;
  }

  assert(*(char *)(addr + pgsz + 1) == 0);
  assert(sigsegv_handled == 2);
  printf("Finish!!!\n");
  /* restore original behavior */
  signal(SIGSEGV, SIG_DFL);

  return 0;
}

int test_mmap_permissions(void) {
  void *addr = mmap(NULL, 2355, PROT_READ, MAP_ANON | MAP_PRIVATE, -1, 0);
  assert(addr != MAP_FAILED);

  /* Try to write to readonly memory. It should raise SIGSEGV */
  memset(addr, -1, 2355);

  return 1;
}

int test_mprotect_fewer_permissions(void) {
  size_t pgsz = getpagesize();
  signal(SIGSEGV, sigsegv_handler);
  void *addr = mmap(NULL, pgsz * 8, PROT_READ, MAP_ANON | MAP_PRIVATE, -1, 0);
  assert(addr != MAP_FAILED);
  printf("mmap returned pointer: %p\n", addr);

  /* Ensure mapped area is cleared. */
  assert(*(char *)(addr + 100) == 0);
  assert(*(char *)(addr + 1000) == 0);

  sigsegv_handled = 0;

  if (setjmp(return_to) == 0) {
    printf("Try to write to readonly memory\n");
    /* Try to write to readonly memory. It should raise SIGSEGV */
    *(char *)addr = '9';
  }

  assert(sigsegv_handled == 1);
  assert(*(char *)addr == 0);

  int error;
  error = mprotect(addr, pgsz, PROT_NONE);
  assert(error == 0);

  printf("sigsegv handled = %d\n", sigsegv_handled);

  if (setjmp(return_to) == 0) {
    char tmp = 'a';
    tmp = *(char *)addr;
    assert(tmp == 'a');
    assert(sigsegv_handled == 2);
  }

  /* restore original behavior */
  signal(SIGSEGV, SIG_DFL);

  return 0;
}
