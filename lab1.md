# [Lab 1: Xv6 and Unix utilities](https://pdos.csail.mit.edu/6.828/2020/labs/util.html)

- 获取代码 `git clone git://g.csail.mit.edu/xv6-labs-2020`
- 切换到util分支 `git switch util`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:sleep(Easy)

- 实验内容：实现sleep程序，在sleep程序中调用`sleep`系统调用，代码应位于`user/sleep.c`

- 实验步骤：

1. 添加`sleep.c`及以下代码

    ```c++
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
    ```

2. 在`Makefile`中的`UPROGS`变量加入`$U/_sleep\`

- 效果：

    ```bash
    $ sleep 10
    $
    ```

## Part 2:pingpong(Easy)

- 实验内容：实现pingpong程序，父子进程使用管道进行通信。父进程发送一个字节到子进程，子进程收到后打印`"<pid>: received ping"`并发送一个字节给父进程后退出，父进程收到后打印`"<pid>: received pong"`后退出，代码应位于`user/pingpong.c.`

- 实验步骤：

1. 添加`pingpong.c`及以下代码

    ```c++
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
    ```

2. 在`Makefile`中的`UPROGS`变量加入`$U/_pingpong\`

- 效果：

  ```bash
  $ pingpong
  4: received ping
  3: received pong
  ```

## Part 3:Primes(Moderate/Hard)

- 实验内容：使用管道编写prime sieve(筛选素数)的并发版本。实现primes程序，每个进程收到父进程发来的数字后，输出第一个数字（质数），将剩下的不被第一个数字整除的发送给子进程。代码应位于`user/primes.c`

- 实验步骤：

1. 添加`primes.c`及以下代码

    ```c++
    #include "kernel/types.h"
    #include "user/user.h"
    /*
    * @brief 接收父进程传来的数字，输出第一个数，将剩下的不被第一个数整除的数发给子进程。
    * @param 父进程与当前进程通信的管道读端
    */
    void primes(int rd_fd) {
      int fst = 0;
      if (read(rd_fd, &fst, sizeof(fst)) == sizeof(int)){
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
    ```

2. 在`Makefile`中的`UPROGS`变量加入`$U/_primes\`

- 效果：

    ```bash
    $ primes
    prime 2
    prime 3
    prime 5
    prime 7
    prime 11
    prime 13
    prime 17
    prime 19
    prime 23
    prime 29
    prime 31
    ```

## Part 4:find(Moderate)

- 实验内容：写一个简化版本的UNIX的find程序：查找指定目录下具有特定名称的所有文件，代码应位于`user/find.c`

- 实验步骤：

1. 添加`find.c`及以下代码

    ```c++
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
    ```

2. 在`Makefile`中的`UPROGS`变量加入`$U/_find\`

- 效果：

    ```bash
    $ echo > b
    $ mkdir a
    $ echo > a/b
    $ find . b
    ./b
    ./a/b
    ```

## Part 5:xargs(Moderate)

- 实验内容：编写一个简化版的UNIX的xargs程序：它从标准输入中按行读取，并且为每一行执行一个命令，将每一行作为参数提供给命令。代码应位于`user/xargs.c`

- 实验步骤：

1. 添加`xargs.c`及以下代码

    ```c++
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
    ```

2. 在`Makefile`中的`UPROGS`变量加入`$U/_xargs\`

- 效果：

    ```bash
    $ sh < xargstest.sh
    $ $ $ $ $ $ hello
    hello
    hello
    $ $ 
    ```
