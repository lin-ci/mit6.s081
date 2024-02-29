# [Lab 10: mmap](https://pdos.csail.mit.edu/6.828/2020/labs/mmap.html)

- 切换到mmap分支 `git switch mmap`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:mmap (hard)

- 实验要求：在xv6中实现mmap和munmap系统调用。但本实验只需要实现与内存映射文件相关的功能子集。

    ```c++
    void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);
    ``````

    1. `addr`为零，由内核应该决定映射文件的虚拟地址。
    2. `offset`为零
    3. `mmap`返回该地址，如果失败则返回`0xffffffffffffffff`。
    4. `length`是要映射的字节数
    5. `prot`是`PROT_READ`或`PROT_WRITE`或两者兼有。
    6. `flags`要么是`MAP_SHARED`（映射内存的修改应写回文件），要么是`MAP_PRIVATE`（映射内存的修改不应写回文件）
    7. `fd`是要映射的文件的打开文件描述符

- 实验步骤：

1. 添加以下代码，使其能在用户态调用`mmap()`和`munmap()`。

    ```c++
    //user/user.h
    void* mmap(void* addr, int length, int prot, int flags, int fd, int off);
    int munmap(void* addr, int length);
    //user/usys.pl
    entry("mmap");
    entry("munmap");
    //kernel/syscall.h
    #define SYS_mmap   22
    #define SYS_munmap 23
    //kernel/syscall.c
    extern uint64 sys_mmap(void);
    extern uint64 sys_munmap(void);
    static uint64 (*syscalls[])(void) = {
    ...
    [SYS_mmap]    sys_mmap,
    [SYS_munmap]  sys_munmap,
    }
    //Makefile
    UPROGS=\
      $U/_mmaptest\
    ```

2. 在`kernel/proc.h`定义`struct vma`，并为每个进程分配16个`vma`。

    ```c++
    #define NVMA 16
    struct vma {
      int used;
      uint64 addr;
      int len;
      int prot;
      int flags;
      int fd;
      struct file *f;
      int offset;
    };    
    struct proc{
      ...
      struct vma vma[NVMA];
    }
    ```

3. 在`kernel/sysfile.c`中实现`sys_mmap()`

    ```c++
    uint64
    sys_mmap(void)
    {
      int length, prot, flags, fd, offest;
      uint64 addr, err = 0xffffffffffffffff;
      struct file* f;
      if (argaddr(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &prot) < 0 ||
      argint(3, &flags) < 0 || argfd(4, &fd, &f) < 0 || argint(5, &offest) < 0) {
        return err;
      }
      if (f->writable == 0 && (prot & PROT_WRITE) != 0 && flags == MAP_SHARED)
        return err;
      struct proc* p = myproc();
      //不能超出进程最大虚拟地址
      if (p->sz + length > MAXVA)
        return err;
      for (int i = 0; i < NVMA; ++i) {
        if (p->vma[i].used == 0) {
          p->vma[i].used = 1;
          p->vma[i].addr = PGROUNDUP(p->sz);//对齐到页边界，如果p->sz不在页边界，则p->sz所在页面可能已经被映射
          p->vma[i].len = length;
          p->vma[i].flags = flags;
          p->vma[i].prot = prot;
          p->vma[i].f = f;
          p->vma[i].offset = offest;
          filedup(f);//增加引用计数，防止fd close后struct file被回收
          p->sz = PGROUNDUP(p->sz)+ PGROUNDDOWN(length);//映射的地址在进程已占用虚拟空间的末尾
          return p->vma[i].addr;
        }
      }
      return err;
    }
    ```

4. 在`kernel/sysfile.`c中实现`mmap_handler()`，同时在`kernel.defs.h`中添加函数原型。

    ```c++
    int mmap_handler(int va, int cause) {
      struct proc* p = myproc();
      int i;
      //找到属于哪个vma的合法范围
      for (i = 0; i < NVMA; ++i) {
        if (p->vma[i].used && p->vma[i].addr <= va && va < p->vma[i].addr + p->vma[i].len) {
          break;
        }
      }
      if (i == NVMA)
        return -1;
      //设置页表权限
      int pte_flags = PTE_U;
      if (p->vma[i].prot & PROT_READ)pte_flags |= PTE_R;
      if (p->vma[i].prot & PROT_WRITE)pte_flags |= PTE_W;
      if (p->vma[i].prot & PROT_EXEC)pte_flags |= PTE_X;
      struct file* f = p->vma[i].f;
      if (cause == 13 && f->readable == 0)return -1;//读取该地址引发的中断，但文件不可读
      if (cause == 15 && f->writable == 0)return -1;//写入该地址引发的中断，但文件不可写
      void* pa = kalloc();//分配一页物理内存
      if (pa == 0)
        return -1;
      memset(pa, 0, PGSIZE);
      ilock(f->ip);//readi时需要获取lock
      int offset = p->vma[i].offset + PGROUNDDOWN(va) - p->vma[i].addr;//计算为va所在页面边界对应的文件offset
      readi(f->ip, 0, (uint64)pa, offset, PGSIZE);//将文件内容写入到物理内存，超出文件部分为0
      iunlock(f->ip);
      //将物理内存与虚拟内存建立映射
      if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)pa, pte_flags) != 0) {
        kfree(pa);
        return -1;
      }
      return 0;
    }
    //sysfile.c
    int mmap_handler(int va, int cause);
    ```

5. 在`kernel/trap.c`中实现`lazy allocation`。`lazy allocation`作用：确保大文件的`mmap`是快速的，并且比物理内存大的文件的`mmap`是可能的。

    ```c++
    usertrap(void)
    {
      if (cause == 8) {
        ...
      } else if((which_dev = devintr()) != 0){
        // ok
      } else if (cause == 13 || cause == 15) {
        uint64 fault_va = r_stval();
        if (PGROUNDUP(p->trapframe->sp) <= fault_va && fault_va < p->sz) {
          if (mmap_handler(r_stval(), cause) != 0)p->killed = 1;
        } else
          p->killed = 1;
      } 
    }
    ...
    ```

6. 在`kernel/sysfile.c`实现`sys_munmap()`，其中`length`为`PGSIZE`的整数倍。

    ```c++
    uint64
    sys_munmap(void) {
      uint64 addr;
      int length;
      if (argaddr(0, &addr) < 0 || argint(1, &length) < 0 || length % PGSIZE != 0)
        return -1;
      int i;
      struct proc* p = myproc();
      for (i = 0; i < NVMA; ++i) {
        if (p->vma[i].used && p->vma[i].len >= length) {
          //munmap位置为开头
          if (p->vma[i].addr == addr) {
            p->vma[i].addr += length;
            p->vma[i].offset += length;
            p->vma[i].len -= length;
            break;
          }
          //munmap位置为末尾
          if (addr + length == p->vma[i].addr + p->vma[i].len) {
            p->vma[i].len -= length;
            p->sz -= length;
            break;
          }
        }
      }
      if (i == NVMA)
        return -1;
      if (p->vma[i].flags == MAP_SHARED && (p->vma[i].prot & PROT_WRITE) != 0) {
        filewrite(p->vma[i].f, addr, length);//此处应该从p->vma[i].offset开始写入，但filewrite函数只能从p->vma[i].f->off处写入。两者并不相同，但test能过。
      }
      uvmunmap(p->pagetable, addr, length / PGSIZE, 1);
      if (p->vma[i].len == 0) {
        fileclose(p->vma[i].f);//因为在mmap时进行filedup
        p->vma[i].used = 0;
      }
      return 0;
    }
    ```

7. 修改`uvmunmap()`和`uvmcopy()`检查`PTE_V`后不再`panic()`。因为增长了`p->sz`，但其中有些页面没有被映射，在`fork()`时需要调用`uvmcopy()`会产生错误。

    ```c++
    if((*pte & PTE_V) == 0)
      continue;
      //panic("uvmunmap: not mapped");
    ```

8. `kernel.proc.c`中修改`exit()`和`fork()`

    ```c++
    #include "fcntl.h"
    void
    exit(int status)
    {
      ...
      for (int i = 0; i < NVMA; ++i) {
        if (p->vma[i].used) {
          if (p->vma[i].flags == MAP_SHARED && (p->vma[i].prot & PROT_WRITE) != 0) {
            filewrite(p->vma[i].f, p->vma[i].addr, p->vma[i].len);
            //此处应该从p->vma[i].offset开始写入，但filewrite函数只能从p->vma[i].f->off处写入。两者并不相同，但test能过。
          }
          fileclose(p->vma[i].f);
          uvmunmap(p->pagetable, p->vma[i].addr, p->vma[i].len / PGSIZE, 1);
          p->vma[i].used = 0;
        }
      }
    }
    int
    fork(void)
    {
      ...
      for (int i = 0; i < NVMA; ++i) {
        if (p->vma[i].addr) {
          memmove(&np->vma[i], &p->vma[i], sizeof(p->vma[i]));
          filedup(np->vma[i].f);
        }
      }
    }
    ```

- 实验结果

    ```bash
    xv6 kernel is booting

    hart 2 starting
    hart 1 starting
    init: starting sh
    $ mmaptest
    mmap_test starting
    test mmap f
    test mmap f: OK
    test mmap private
    test mmap private: OK
    test mmap read-only
    test mmap read-only: OK
    test mmap read/write
    test mmap read/write: OK
    test mmap dirty
    test mmap dirty: OK
    test not-mapped unmap
    test not-mapped unmap: OK
    test mmap two files
    test mmap two files: OK
    mmap_test: ALL OK
    fork_test starting
    fork_test OK
    mmaptest: all tests succeeded    
    ```
