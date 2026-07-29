#ifndef _STDDEF_H
#define _STDDEF_H

#include "stdint.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef int64_t ptrdiff_t;

#endif
