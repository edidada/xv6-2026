#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
  if(argc < 2){
    fprintf(2, "usage: sixfive file ...\n");
    exit(1);
  }
  for(int i = 1; i < argc; i++){
    int fd = open(argv[i], O_RDONLY);
    if(fd < 0){
      fprintf(2, "sixfive: cannot open %s\n", argv[i]);
      exit(1);
    }
    char c, token[512];
    int n = 0, valid = 1, mod5 = 0, mod6 = 0, nr;
    // Track remainders so even long decimal numbers cannot overflow.
    do {
      nr = read(fd, &c, 1);
      if(nr < 0){
        fprintf(2, "sixfive: read failed\n");
        close(fd);
        exit(1);
      }
      if(nr == 0 || strchr(" -\r\t\n./,", c)){
        if(n && valid && (mod5 == 0 || mod6 == 0)){
          token[n] = 0;
          char *number = token;
          while(number[0] == '0' && number[1]) number++;
          printf("%s\n", number);
        }
        n = mod5 = mod6 = 0;
        valid = 1;
      } else if(c >= '0' && c <= '9'){
        mod5 = (mod5 * 10 + c - '0') % 5;
        mod6 = (mod6 * 10 + c - '0') % 6;
        if(n < sizeof(token)-1)
          token[n++] = c;
        else
          valid = 0;
      } else {
        valid = 0;
      }
    } while(nr);
    close(fd);
  }
  exit(0);
}
