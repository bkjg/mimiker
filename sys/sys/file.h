#ifndef _SYS_FILE_H_
#define _SYS_FILE_H_

#include <stdint.h>

typedef int32_t         off_t;
#define off_t           int32_t

struct fileops {
  const char *fo_name;

  int (*fo_mmap) (struct file *, off_t *, size_t, int, int *, int *, struct uvm_object **, int *);
};

#endif /* _SYS_FILE_H_ */
