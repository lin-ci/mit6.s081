#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char** argv) {
  char* xargv[MAXARG] = { 0 };
  int cnt = argc - 1;
  for (int i = 0; i < argc; ++i) {
    xargv[i] = argv[i + 1];//保存原来的参数
  }
  char line[512], buf;
  int st = 0, ed = 0;//参数的开始位置，下一个字符写入的位置
  while (read(0, &buf, sizeof(buf)) == sizeof(buf)) {
    line[ed++] = buf;
    if (buf == '\n') {//换行符，执行命令
      line[ed - 1] = 0;//字符串结尾
      xargv[cnt++] = line + st;//加入一个参数
      xargv[cnt] = 0;//不存在下一个参数
      if (fork() == 0) {
        exec(xargv[0], xargv);
      }
      wait(0);
      st = ed;//下一个参数的开始位置
      cnt = argc - 1;
    }
    else if (buf == ' ') {
      if (st < ed - 1) {
        //获取到一个参数
        line[ed - 1] = 0;
        xargv[cnt++] = line + st;
        st = ed;
      }
      else {
        //忽略多余空格
        st = ed;
      }
    }
  }
  if (1) {//末尾不是换行符，对剩余参数执行命令
    line[ed - 1] = 0;
    xargv[cnt++] = line + st;//加入一个参数
    xargv[cnt] = 0;//不存在下一个参数
    if (fork() == 0) {
      exec(xargv[0], xargv);
    }
    wait(0);
  }
  exit(0);
}