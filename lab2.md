# [Lab 2: system calls](https://pdos.csail.mit.edu/6.828/2020/labs/syscall.html)

- 切换到syscall分支 `git switch syscall`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:System call tracing (moderate)

- 实验内容：添加一个系统调用`trace`来跟踪打印进程的系统调用，`trace`将其系统调用参数保存在进程结构体`struct proc`中的`trace_mask`字段。之后，当该进程执行系统调用时，根据`trace_mask`字段判断是否打印跟踪。

- 实验步骤：

1. 在`Makefile`中的`UPROGS`变量加入`$U/_trace\`。`user/trace.c`作为一个用户程序，调用系统调用`trace`。
2. 在`user/user.h`中添加`int trace(int);`函数声明.
3. 在`user/usys.pl`中添加`entry("trace");`，生成`trace`系统调用的汇编代码，即`user/user.h`中`trace`函数的实现。

    ```pl
    sub entry {
      my $name = shift;
      print ".global $name\n";
      print "${name}:\n";
      print " li a7, SYS_${name}\n";
      print " ecall\n";
      print " ret\n";
    }
    entry("trace");
    ```

    即将系统调用号`SYS_trace`放入寄存器`a7`，然后调用`ecall`指令陷入内核态。
4. 在`kernel/syscall.h`中添加系统调用号定义`#define SYS_trace  22`。
5. 在`kernel/proc.h`中的`struct proc`添加字段`int trace_mask`。
6. 在`kernel/sysproc.c`中添加`sys_trace`的实现
  
    ```c++
    uint64
    sys_trace(void)
    {
      //获取系统调用参数，保存到proc的trace_mask字段
      argint(0, &(myproc()->trace_mask));
      return 0;
    }
    ```

7. 修改`kernel/syscall.c`，在进行系统调用后，根据系统调用号是否在proc->trace_mask里面，判断是否输出跟踪。

    ```c++
    extern uint64 sys_trace(void);

    static uint64(*syscalls[])(void) = {
      ...
      [SYS_trace]   sys_trace,
    }
    static char* syscalls_name[] = {
    [SYS_fork]    "fork",
    [SYS_exit]    "exit",
    [SYS_wait]    "wait",
    [SYS_pipe]    "pipe",
    [SYS_read]    "read",
    [SYS_kill]    "kill",
    [SYS_exec]    "exec",
    [SYS_fstat]   "fstat",
    [SYS_chdir]   "chdir",
    [SYS_dup]     "dup",
    [SYS_getpid]  "getpid",
    [SYS_sbrk]    "sbrk",
    [SYS_sleep]   "sleep",
    [SYS_uptime]  "uptime",
    [SYS_open]    "open",
    [SYS_write]   "write",
    [SYS_mknod]   "mknod",
    [SYS_unlink]  "unlink",
    [SYS_link]    "link",
    [SYS_mkdir]   "mkdir",
    [SYS_close]   "close",
    [SYS_trace]   "trace",
    };

    syscall(void)
    {
      int num;
      struct proc* p = myproc();

      num = p->trapframe->a7;
      if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        p->trapframe->a0 = syscalls[num]();
        //系统调用号是否在mask里面
        if ((1 << num) & p->trace_mask)
          printf("%d: syscall %s -> %d\n", p->pid, syscalls_name[num], p->trapframe->a0);
      }
      else {
        printf("%d %s: unknown sys call %d\n",
                p->pid, p->name, num);
        p->trapframe->a0 = -1;
      }
    }
    ```

8. 在`kernel/proc.c`中，`fork`时将trace_mask拷贝到子进程。

    ```c++
    int
    fork(void)
    {
      ...
      safestrcpy(np->name, p->name, sizeof(p->name));

      np->trace_mask = p->trace_mask;//将trace_mask拷贝到子进程

      pid = np->pid;
      ...
    }
    ```

- 效果：

  ```bash
  $ trace 2147483647 grep hello README
  3: syscall trace -> 0
  3: syscall exec -> 3
  3: syscall open -> 3
  3: syscall read -> 1023
  3: syscall read -> 966
  3: syscall read -> 70
  3: syscall read -> 0
  3: syscall close -> 0
  $
  ```

## Part 2:Sysinfo (moderate)

- 实验内容：添加一个系统调用`sysinfo`，参数为`struct sysinfo`结构体指针，结构体包含空闲内存的字节数`freemem`，和系统进程数`nproc`。系统调用将结果填入该结构体。测试程序为sysinfotest。

- 实验步骤：

1. 在`Makefile`中的`UPROGS`变量加入`$U/_sysinfotest\`。`user/sysinfotest.c`作为一个用户态的程序，调用系统调用`sysinfo`。
2. 在`user/user.h`中添加`struct sysinfo;`结构体声明和`int sysinfo(struct sysinfo*);`函数声明。
    - `kernel/sysinfo`中`struct sysinfo`的定义：

      ```c++
      struct sysinfo {
        uint64 freemem;   // amount of free memory (bytes)
        uint64 nproc;     // number of process
      };
      ```

3. 在`user/usys.pl`中添加`entry("sysinfo");`，生成`sysinfo`系统调用的汇编代码，即`user/user.h`中`sysinfo`函数的实现。

    ```pl
    sub entry {
      my $name = shift;
      print ".global $name\n";
      print "${name}:\n";
      print " li a7, SYS_${name}\n";
      print " ecall\n";
      print " ret\n";
    }
    entry("sysinfo");
    ```

    即将系统调用号`SYS_sysinfo`放入寄存器`a7`，然后调用`ecall`指令陷入内核态。
4. 在`kernel/syscall.h`中添加`#define SYS_sysinfo 23`。
5. 在`kernel/syscall.c`中添加如下代码。

    ```c++
    extern uint64 sys_sysinfo(void);

    static uint64(*syscalls[])(void) = {
      ...
      [SYS_sysinfo]   sys_sysinfo,
    }
    static char* syscalls_name[] = {
      ...
      [SYS_sysinfo]   "sys_sysinfo",
    };
    ```

6. 在`kernel/sysproc.c`中添加`sys_sysinfo(void)`函数。

    ```c++
    #include "sysinfo.h"
    uint64
    sys_sysinfo(void)
    {
      struct sysinfo info;
      freebytes(&info.freemem);//获取空闲内存数
      procnum(&info.nproc);//或许系统进程数
      uint64 dst;
      argaddr(0, &dst);//获取用户空间的虚拟地址
      if (copyout(myproc()->pagetable, dst, (char*)&info, sizeof(info)) < 0)//将info结构体拷贝回用户空间
        return -1;
      return 0;
    }
    ```

7. 在`kernel/defs.h`中添加函数原型`void freebytes(uint64*);`，在`kernel/kalloc.c`中添加函数用于获取空闲内存数。
  
    ```c++
    //获取空闲内存数
    void freebytes(uint64* ret) {
      *ret = 0;
      struct run* p = kmem.freelist;
      acquire(&kmem.lock);
      while (p) {
        *ret += PGSIZE;
        p = p->next;
      }
      release(&kmem.lock);
    }  
    ```

8. 在`kernel/defs.h`中添加函数原型`void procnum(uint64*);`，在`kernel/proc.c`中添加函数用于获取进程数。

    ```c++
    //获取系统进程数
    void procnum(uint64* dst) {
      *dst = 0;
      struct proc* p;
      for (p = proc; p < &proc[NPROC]; p++) {
        if (p->state != UNUSED)
          (*dst)++;
      }
    }
    ```

- 效果：

  ```bash
  $ sysinfotest
  sysinfotest: start
  sysinfotest: OK
  $ 
  ```
