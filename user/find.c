#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "user/user.h"

static int failed;

static void
find(char *path, char *name, char **cmd, int nargs)
{
  struct stat st;
  struct dirent de;
  char buf[512], *base = path;
  int fd = open(path, O_RDONLY);
  if(fd < 0 || fstat(fd, &st) < 0){
    fprintf(2, "find: cannot read %s\n", path);
    if(fd >= 0) close(fd);
    failed = 1;
    return;
  }
  for(char *p = path; *p; p++)
    if(*p == '/') base = p+1;
  if(strcmp(base, name) == 0){
    if(!nargs)
      printf("%s\n", path);
    else {
      char *args[MAXARG];
      for(int i = 0; i < nargs; i++) args[i] = cmd[i];
      args[nargs] = path;
      args[nargs+1] = 0;
      int pid = fork(), status;
      if(pid == 0){
        close(fd);
        exec(args[0], args);
        fprintf(2, "find: exec %s failed\n", args[0]);
        exit(1);
      }
      if(pid < 0 || wait(&status) < 0 || status != 0) failed = 1;
    }
  }
  if(st.type == T_DIR){
    if(strlen(path)+DIRSIZ+2 > sizeof(buf)){
      fprintf(2, "find: path too long\n");
      failed = 1;
      close(fd);
      return;
    }
    strcpy(buf, path);
    char *p = buf + strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(!de.inum) continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(strcmp(p, ".") && strcmp(p, ".."))
        find(buf, name, cmd, nargs);
    }
  }
  close(fd);
}

int
main(int argc, char **argv)
{
  if(argc < 3 || (argc > 3 && (argc < 5 || strcmp(argv[3], "-exec"))) ||
     argc-4 > MAXARG-2){
    fprintf(2, "usage: find path name [-exec command args ...]\n");
    exit(1);
  }
  find(argv[1], argv[2], argv+4, argc > 3 ? argc-4 : 0);
  exit(failed);
}
