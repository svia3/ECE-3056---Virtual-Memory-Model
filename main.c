#include <stdio.h>
#include <stdlib.h>

#include "vmsim.h"

FILE *open_trace(const char *filename) {
  return fopen(filename, "r");
}

addr_t prev_addr;
int next_line(FILE* trace) {
  if (feof(trace) || ferror(trace)) return 0;
  else {
    char t;
    unsigned long long va, pa;
    unsigned sz;
    fscanf(trace, "%c %llx %llx %u\n", &t, &va, &pa, &sz);
    prev_addr = pa & (0x00FFFFFF); //force addresses to 24 bits
    byte_t val = memory_access(prev_addr, (t == 'w'), (byte_t)(sz));

    //printf("%u\n",val);
  }
  return 1;
}

int main(int argc, char **argv) {
  FILE *input;

  if (argc != 2) {
    fprintf(stderr, "Usage:\n  %s <trace>\n", argv[0]);
    return 1;
  }

  input = open_trace(argv[1]);
  if (!input)
  {
    fprintf(stderr, "Trace file %s not found!\n", argv[1]);
    return 1;
  }

  system_init();
  while (next_line(input));
  volatile byte_t* mem_ptr = system_shutdown();
  vm_print_stats(); //Print the stats of the cache


  return 0;
}
