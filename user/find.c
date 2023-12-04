#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/stat.h"

/**
* @brief 递归遍历指定目录下的所有文件，输出与filename名字相同的所有文件路径
*/
void find(char* path, char* filename) {
  char buf[512], * p;
  int fd;
  struct dirent de;//目录
  struct stat st;//文件信息

  if ((fd = open(path, 0)) < 0) {//不存在该路径
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if (fstat(fd, &st) < 0) {//fstat失败
    fprintf(2, "find: cannot fstat %s\n", path);
    close(fd);
    return;
  }
  if (st.type != T_DIR) {//不是目录
    fprintf(2, "usage: find <directory> <filename>\n");
    return;
  }
  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
    //递归深度过长，导致存储路径长度的buf长度不够
    fprintf(2, "find: path too long\n");
  }
  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)continue;
    memmove(p, de.name, DIRSIZ);//buf存储该层目录的文件路径
    p[DIRSIZ] = 0;//字符串结尾
    if (stat(buf, &st) < 0) {
      fprintf(2, "find: cannot stat %s\n", buf);
      continue;
    }
    if (st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) {//类型为.和..的目录不递归
      find(buf, filename);//类型为目录，递归处理
    }
    else if (strcmp(filename, p) == 0) {
      printf("%s\n", buf);
    }
  }
  close(fd);
}
int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(2, "usage: find <directory> <filename>\n");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}