#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char** argv) {
  int fd_ptoc[2];//父进程->子进程
  int fd_ctop[2];//子进程->父进程
  pipe(fd_ptoc);
  pipe(fd_ctop);
  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork error\n");
  }
  else if (pid == 0) {//子进程
    close(fd_ptoc[1]);//父进程->子进程 只保留读端
    close(fd_ctop[0]);//子进程->父进程 只保留写端
    char buf;
    read(fd_ptoc[0], &buf, sizeof(buf));
    fprintf(1, "%d: received ping\n", getpid());
    write(fd_ctop[1], &buf, sizeof(buf));
    close(fd_ptoc[0]);
    close(fd_ctop[1]);
  }
  else {//父进程
    close(fd_ptoc[0]);//父进程->子进程 只保留写端
    close(fd_ctop[1]);//子进程->父进程 只保留读端
    char buf = 'C';
    write(fd_ptoc[1], &buf, sizeof(buf));
    read(fd_ctop[0], &buf, sizeof(buf));
    fprintf(1, "%d: received pong\n", getpid());
    close(fd_ptoc[1]);
    close(fd_ctop[0]);
  }
  exit(0);
}