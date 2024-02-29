# [Lab 5: xv6 lazy page allocation](https://pdos.csail.mit.edu/6.828/2020/labs/lazy.html)

- 切换到lazy分支 `git switch lazy`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:Eliminate allocation from sbrk() (easy)

- 实验要求：删除`sbrk(n)`系统调用中的页面分配代码，只将进程的大小（`myproc()->sz`）增加n，然后返回旧的大小。
- 实验步骤
  
1. 修改`kernel/sysproc.c`。若此时在xv6中执行`echo hi`，返回错误是因为在`user/sh.c`中执行了`malloc()`，`malloc()`调用了`sbrk()`。

    ```c++
    uint64
    sys_sbrk(void)
    {
      int addr;
      int n;

      if(argint(0, &n) < 0)
        return -1;
      addr = myproc()->sz;
      // if(growproc(n) < 0)
      //   return -1;
      myproc()->sz += n;
      return addr;
    }    
    ```

## Part 2:Lazy allocation (moderate)

- 实验要求：在trap中添加lzay allocation。新分配一个物理页面并映射到发生错误的地址，然后返回到用户空间，让进程继续执行。
- 实验步骤

1. 修改`kernel/trap.c`。

    ```c++
    void
    usertrap(void)
    {
      ...
      if(cause == 8){
        // system call

        ...
      } else if (cause == 13 || cause == 15){
        uint64 fault_va = r_stval();
        char* pa;
        if (PGROUNDDOWN(p->trapframe->sp) <= fault_va && fault_va < p->sz && (pa = kalloc()) != 0) {
          //该地址在堆区
          memset(pa, 0, PGSIZE);
          if (mappages(p->pagetable, PGROUNDDOWN(fault_va), PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_X | PTE_U) != 0) {
            kfree(pa);
            p->killed = 1;
          }
        }
        else {
          //不在堆区直接kill
          p->killed = 1;
        }
      } ...
    } 
    ```

2. 若分配的空间过大，则前三级页表可能没有页表项，导致`walk()`返回0。若分配空间较小，则可能存在前三级页表，但返回的页表项`PTE_V=0`。

    ```c++
    if ((pte = walk(pagetable, a, 0)) == 0)
      continue;
      //panic("uvmunmap: walk");
    if ((*pte & PTE_V) == 0)
      continue;
      //panic("uvmunmap: not mapped");
    ```

- 实验结果

    ```bash
    $ echo hi
    hi    
    ```

## Part 3:Lazytests and Usertests (moderate)

- 实验要求：

1. 处理sbrk()参数为负的情况。
2. 如果某个进程在高于sbrk()分配的任何虚拟内存地址上出现页错误，则终止该进程。
3. 在fork()中正确处理父到子内存拷贝。
4. 处理这种情形：进程从sbrk()向系统调用（如read或write）传递有效地址，但尚未分配该地址的内存。
5. 正确处理内存不足：如果在页面错误处理程序中执行kalloc()失败，则终止当前进程。
6. 处理用户栈下面的无效页面上发生的错误。

- 实验步骤：

1. 在`kernel/sysproc.c`中的`sys_sbrk()`处理参数为负的情况。

    ```c++
    uint64
    sys_sbrk(void)
    {
      int addr;
      int n;

      if(argint(0, &n) < 0)
        return -1;
      struct proc* p = myproc();
      addr = p->sz;
      // if(growproc(n) < 0)
      //   return -1;
      uint64 sz = p->sz;
      if (n > 0) {
        p->sz += n;
      }
      else if (sz + n > 0) {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
        p->sz = sz;
      }
      else {
        return -1;
      }
      return addr;
    }
    ```

2. 在fork()中正确处理父到子内存拷贝。

    ```c++
    if ((pte = walk(old, i, 0)) == 0)
      continue;
      //panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0)
      continue;
        //panic("uvmcopy: page not present");
    ```

3. `sbrk()`后将该地址直接传递给系统调用，此时经过`usertrap()`时的`cause`是因为`syscall`而不是缺页异常，所以在`usertrap()`中不会为其分配物理页面，导致`usertest`报错。可以在获取参数时，增加`lazy allocation`。

    ```c++
    int
    argaddr(int n, uint64 *ip)
    {
      *ip = argraw(n);
      struct proc* p = myproc();
      if (walkaddr(p->pagetable, *ip) == 0) {
        if (PGROUNDUP(p->trapframe->sp) <= *ip && *ip < p->sz) {
          char* pa = kalloc();
          if (pa == 0)
            return -1;
          memset(pa, 0, PGSIZE);
          if (mappages(p->pagetable, PGROUNDDOWN(*ip), PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_X | PTE_U) != 0) {
            kfree(pa);
            return -1;
          }
        }
        else {
          return -1;
        }
      }
      return 0;
    }    
    ```

- 实验结果：

    ```bash
    $ lazytests
    lazytests starting
    running test lazy alloc
    test lazy alloc: OK
    running test lazy unmap
    test lazy unmap: OK
    running test out of memory
    test out of memory: OK
    ALL TESTS PASSED
    $ usertests
    ...
    test dirfile: OK
    test iref: OK
    test forktest: OK
    test bigdir: OK
    ALL TESTS PASSED
    ```
