#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"      // file system

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
find(char *path, char *filename)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    printf("find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    printf("find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type)
  {
  case T_FILE:
    printf("Usage: find path filename\n");
    break;

  case T_DIR:
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
      printf("find: path too long\n");
      break;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
      
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) 
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if (stat(buf, &st) < 0) {
        printf("find: cannot stat %s\n", buf);
        continue;
      }

      if (st.type == T_FILE) {
        // printf("%s %d\n", buf, st.type);
        if (strcmp(de.name, filename) == 0) {
          printf("%s\n", buf);
        }
      } else if (st.type == T_DIR) {
        // printf("path: %s\n", de.name);
        find(buf, filename);
      }
    }

    break;
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if (argc < 3) {
    printf("Usage: find path filename\n");
    exit(1);
  } else {
    // printf("path: %s, filename: %s\n", argv[1], argv[2]);
    find(argv[1], argv[2]);
  }

  exit(0);
}