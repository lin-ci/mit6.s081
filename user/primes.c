#include "kernel/types.h"
#include "user/user.h"
/*
* @brief 接收父进程传来的数字，输出第一个数，将剩下的不被第一个数整除的数发给子进程。
* @param 父进程与当前进程通信的管道读端
*/
void primes(int rd_fd) {
  int fst = 0;
  if (read(rd_fd, &fst, sizeof(fst)) == sizeof(int)) {
    //接收到的第一个数为质数
    fprintf(1, "prime %d\n", fst);
  }
  else {//没有数据直接返回
    exit(0);
  }
  int pipe_fd[2];//当前进程->子进程
  pipe(pipe_fd);
  int pid = fork();
  if (pid == 0) {//子进程
    close(pipe_fd[1]);//关闭子进程写端
    primes(pipe_fd[0]);
  }
  else {//当前进程
    close(pipe_fd[0]);//关闭当前进程读端
    int num;
    while (read(rd_fd, &num, sizeof(num)) == sizeof(num)) {//接收父进程传来的数字
      if (num % fst == 0)continue;//被fst整除的数不是质数
      write(pipe_fd[1], &num, sizeof(num));//传给子进程
    }
    close(pipe_fd[1]);//已经没有数据发送，关闭写端防止子进程阻塞
    wait(0);
  }
  exit(0);
}
int main(int argc, char** argv) {
  int pipe_fd[2];
  pipe(pipe_fd);//父进程->子进程
  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork error\n");//输出到标准错误
    exit(1);
  }
  else if (pid == 0) {//子进程
    close(pipe_fd[1]);//关闭子进程写端
    primes(pipe_fd[0]);
  }
  else {//父进程
    close(pipe_fd[0]);//关闭父进程读端
    for (int i = 2; i <= 35; ++i) {
      int num = i;
      write(pipe_fd[1], &num, sizeof(num));
    }
    close(pipe_fd[1]);//已经没有数据发送，关闭写端防止子进程阻塞
    wait(0);
  }
  exit(0);
}