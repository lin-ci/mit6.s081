# [Lab 4: traps](https://pdos.csail.mit.edu/6.828/2020/labs/traps.html)

- 切换到syscall分支 `git switch traps`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:RISC-V assembly (easy)

- 实验内容：xv6仓库中有一个文件user/call.c。执行make fs.img编译它，并在user/call.asm中生成可读的汇编版本。将回答存放在`answers-traps.txt`文件中。

- Q&A：

1. Q: 哪些寄存器保存函数的参数？例如，在main对printf的调用中，哪个寄存器保存13？

    A: a0-a7存放参数，a2存放13。
2. Q: main的汇编代码中对函数f的调用在哪里？对g的调用在哪里(提示：编译器可能会将函数内联)

    A: 如下，编译器对其进行优化，直接计算出结果12

    ```text
    000000000000001c <main>:

    void main(void) {
      ...
      printf("%d %d\n", f(8)+1, 13);
      24: 4635                  li  a2,13
      26: 45b1                  li  a1,12
    ```

3. Q: printf函数位于哪个地址？

    A: `jalr  1528(ra) # 628 <printf>`，函数地址为`0x628`
4. Q: 在main中printf的jalr之后的寄存器ra中有什么值？

    ```c++
    30: 00000097            auipc ra,0x0
    34: 5f8080e7            jalr  1528(ra) # 628 <printf>    
    ```

    A: 0x38

    `auipc`(`Add Upper Immediate to PC`)：`auipc rd imm`，将立即数高20位与PC相加，并将结果保存到ra寄存器。此时ra寄存器值为30。
    ![Alt text](image-6.png)

    `jalr` (`jump and link register`)：`jalr rd, offset(rs1)`跳转并链接寄存器。`jalr`指令会将当前`PC+4`保存在`rd`中，然后跳转到指定的偏移地址`offset(rs1)`。即将34+4=38保存在ra中，然后跳转到30+5f8=628。
    ![Alt text](image-7.png)
5. 运行以下代码。

    ```c++
    unsigned int i = 0x00646c72;
    printf("H%x Wo%s", 57616, &i);
    ```

    Q：程序的输出是什么？这是将字节映射到字符的ASCII码表。

    A：$(57616)_{10} = (e110)_{16}$ ，`0x00646c72`小端存储为`72-6c-64-00`，`72:r 6c:l 64:d 00:字符串结尾`。所以输出为：`HE110 World`
  
    Q：输出取决于RISC-V小端存储的事实。如果RISC-V是大端存储，为了得到相同的输出，你会把i设置成什么？是否需要将57616更改为其他值？

    A: i设置为`0x726c6400`，`57616`不需要改变。

    Q: 在下面的代码中，“y=”之后将打印什么(注：答案不是一个特定的值)？为什么会发生这种情况？

    ```c++
    printf("x=%d y=%d", 3);
    ```

    A: 函数调用时，y存放于a2中，由于没有提供y，所以为之前的a2的值。

## Part 2:Backtrace (moderate)

- 实验内容：实现一个`backtrace`函数，功能为输出内核栈的函数调用列表。
- 实验思路：函数调用时不断将其帧压入栈中，gcc将当前执行的函数的帧指针存储在`s0`寄存器中，帧中`-16`偏移的`fp`字段为指向上一个帧的指针。
    ![课堂笔记截图-帧的布局](image.png)

- 实验步骤：

1. 在`kernel/riscv.h`中添加以下函数来获取`s0`寄存器的值。

    ```c++
    static inline uint64
    r_fp()
    {
      uint64 x;
      asm volatile("mv %0, s0" : "=r" (x) );
      return x;
    }
    ```

2. 在`kernel/printf.c`中添加以下函数，同时将函数原型添加到`kernel/defs.h`。

    ```c++
    void
    backtrace() {
      uint64 fp, ra, high, low;
      printf("backtrace:\n");
      fp = r_fp();//fp为指向当前帧的指针
      high = PGROUNDUP(fp);
      low = PGROUNDDOWN(fp);//low~high为进程的内核栈所在的页面，只有一页
      while (fp < high && fp > low) {
        //返回地址在-8偏移的位置
        ra = *(uint64*)(fp - 8);//获取该地址的值，先将地址转化成指针，再解引用
        printf("%p\n", ra);
        //前一个帧指针在-16偏移的位置
        fp = *(uint64*)(fp - 16);//获取该地址的值，先将地址转化成指针，再解引用
      }
    }
    ```

3. 在`sys_sleep()`中插入一个对`backtrace()`函数的调用，然后运行`bttest`，它将会调用`sys_sleep()`。

    ```bash
    $ bttest
    backtrace:
    0x0000000080002cc2
    0x0000000080002b9e
    0x0000000080002888
    ```

4. 退出`qemu`后，运行`addr2line -e kernel/kernel`，将输入上面的地址，得到下面的输出。

    ```bash
    $ addr2line -e kernel/kernel
    0x0000000080002cc2
    /home/linxd/6S081/xv6-labs-2020/kernel/sysproc.c:61
    0x0000000080002b9e
    /home/linxd/6S081/xv6-labs-2020/kernel/syscall.c:140
    0x0000000080002888
    /home/linxd/6S081/xv6-labs-2020/kernel/trap.c:76
    ```

## Part 3:Alarm (hard)

- 实验内容：添加一个`sigalarm(interval, handler)`系统调用，如果一个程序调用了`sigalarm(n, fn)`，那么每当程序消耗了CPU时间达到n个`tick`，内核应当使应用程序函数`fn`被调用。当`fn`返回时，应用应当在它离开的地方恢复执行。

- 实验思路：调用`sigalarm()`时，记录其参数到`proc`结构体中。每个`tick`中断一次，对当前运行的程序`ticks++`，判断是否需要执行`fn`函数。如果需要，则将`epc`修改为`alarm_handler`的地址，当`trap`返回时，回到的是`fn`函数。对`sleep`的进程不进行判断，`ticks`也不变。在`fn`结束时调用`sigreturn`，切换回被中断的代码。

- 实验过程

1. 在`Makefile`中的`UPROGS`变量添加`$U/_alarmtest\`使得`alarmtest.c`能够被编译为用户程序。
2. 在`user/user.h`添加以下函数声明。

    ```c++
    int sigalarm(int ticks, void (*handler)());
    int sigreturn(void);
    ```

3. 在`user/usys.pl`添加以下代码，使得编译后能够生成`sigalarm`和`sigreturn`函数代码，使得用户程序能够调用这两个系统函数。

    ```c++
    entry("sigalarm");
    entry("sigreturn");
    ```

4. 在`kernel/syscall.h`中添加系统调用号。

    ```c++
    #define SYS_sigalarm 22
    #define SYS_sigreturn 23
    ```

5. 在`kernel/syscall.c`中添加以下代码。

    ```c++
    extern uint64 sys_sigalarm(void);
    extern uint64 sys_sigreturn(void);

    static uint64(*syscalls[])(void) = {
    ...
    [SYS_sigalarm] sys_sigalarm,
    [SYS_sigreturn] sys_sigreturn,
    };
    ```

6. 在`kernel/proc.h`中的`struct proc`添加以下字段。

    ```c++
    struct proc {
      ...
      int alarm_interval;          // 定时器间隔
      void (*alarm_handler)();     // 处理函数
      int ticks;                   // 距离上次定时器触发到目前的ticks
      int in_handle;               // 是否在执行处理函数
      struct trapframe* alarm_trapframe; //保存寄存器
    }
    ```

7. 在`kernel/sysproc.c`中添加`sys_sigalarm`和`sys_sigreturn`的函数实现。`sys_sigalarm`函数将调用的参数保存到`struct proc`中，`sys_sigreturn`函数将`p->trapframe`切换回执行`fn`前的状态，此时`trap`返回时，从被中断的地方开始执行。

    ```c++
    uint64
    sys_sigalarm(void)
    {
      int ticks;
      uint64 fn;
      if ((argint(0, &ticks) < 0) || (argaddr(1, &fn) < 0)) {
        return -1;
      }
      struct proc* p = myproc();
      p->alarm_interval = ticks;
      p->alarm_handler = (void(*)())fn;
      return 0;
    }
    uint64
    sys_sigreturn(void)
    {
      struct proc* p = myproc();
      memmove(p->trapframe, p->alarm_trapframe, sizeof(struct trapframe));
      p->in_handle = 0;
      return 0;
    }
    ```

8. 修改`usertrap`，需要判断此时是否正在执行`alarm_handler`，有可能其执行时间超过`interval`。

    ```c++
    void
    usertrap(void)
    {
      ...
      // give up the CPU if this is a timer interrupt.
      if (which_dev == 2) {
        p->ticks++;
        if (p->alarm_interval != 0 && p->ticks == p->alarm_interval && p->in_handle == 0) {
          memmove(p->alarm_trapframe, p->trapframe, sizeof(struct trapframe));
          p->trapframe->epc = (uint64)p->alarm_handler;
          p->ticks = 0;
          p->in_handle = 1;
        }
        yield();
      }
      ...
    }
    ```

9. 在`kernel/proc.c`中的`allocproc`和`freeproc`函数加入分配和释放`alarm_trapframe`的代码。

    ```c++
    static struct proc*
    allocproc(void)
    {
      ...
      if ((p->alarm_trapframe = (struct trapframe*)kalloc()) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
      }
      p->in_handle = 0;
      p->alarm_interval = 0;
      p->ticks = 0;
      p->alarm_handler = 0;
      ...
    }
    static void
    freeproc(struct proc* p)
    {
      ...
      if (p->alarm_trapframe)
        kfree((void*)p->alarm_trapframe);
      p->alarm_trapframe = 0;
      p->in_handle = 0;
      p->alarm_interval = 0;
      p->ticks = 0;
      p->alarm_handler = 0;
    }
    ```

- 实验结果：

    ```bash
    init: starting sh
    $ alarmtest
    test0 start
    ........alarm!
    test0 passed
    test1 start
    ...alarm!
    ..alarm!
    ..alarm!
    ...alarm!
    ..alarm!
    ..alarm!
    ..alarm!
    ..alarm!
    ...alarm!
    ..alarm!
    test1 passed
    test2 start
    ..............alarm!
    test2 passed
    ```
