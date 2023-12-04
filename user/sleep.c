#include "kernel/types.h"
#include "user/user.h"//前两行顺序不能相反

int main(int argc, char** argv) {
  if (argc != 2) {//参数错误
    fprintf(2, "usage: sleep <time>\n");//fd=2 为标准错误
    exit(1);
  }
  sleep(atoi(argv[1]));//系统调用
  exit(0);
}