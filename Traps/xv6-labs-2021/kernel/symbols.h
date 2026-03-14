#include "types.h"
struct symbol {
  uint64 addr;
  char* name;
};

extern struct symbol symbols[];
extern int nsymbols;