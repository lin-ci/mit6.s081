# [Lab 6: Copy-on-Write Fork for xv6](https://pdos.csail.mit.edu/6.828/2020/labs/cow.html)

- 切换到cow分支 `git switch cow`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:Implement copy-on write(hard)

- 实验要求：在xv6内核中实现`copy-on-write fork`。如果修改后的内核同时成功执行`cowtest`和`usertests`程序就完成了。
- 实验步骤：

1. 在`kernel/riscv.h`定义标志位`PTE_F`，表示该页面是否为`COW`页面。页表项中的第7，8位为软件保留位，可以使用。

    ```c++
    #define PTE_F (1L << 8)
    ```

2. 在`kernel/kalloc.c`中为每一个物理页面使用一个引用计数，同时需要加锁。考虑以下情况：一个进程在执行`fork`，而另一个进程在`exit`（且第二个进程是由第一个进程`fork`得到的），由于`COW`，此时两个进程同时修改相同物理页面的引用计数。

    ```c++
    struct {
      struct spinlock lock;
      int cnt[PHYSTOP / PGSIZE];
    }refcnt;

    void
    kinit()
    {
      initlock(&kmem.lock, "kmem");
      initlock(&refcnt.lock,"refcnt");
      freerange(end, (void*)PHYSTOP);
    }
    ```

3. 在`kernel/kalloc.c`中添加两个关于引用计数的函数。

    ```c++
    void
    inc_refcnt(uint64 pa, int val) {
      acquire(&refcnt.lock);
      refcnt.cnt[(uint64)pa / PGSIZE] += val;
      release(&refcnt.lock);
    }

    int
    get_refcnt(uint64 pa) {
      int ret = 0;
      acquire(&refcnt.lock);
      ret = refcnt.cnt[(uint64)pa / PGSIZE];
      release(&refcnt.lock);
      return ret;
    }
    ```

4. 在`kernel/vm.c`中修改`kalloc()`，在首次分配物理内存时，引用计数设置为1。同时修改`kfree()`，当引用计数为0时，将其释放。

    ```c++
    void *
    kalloc(void)
    {
      struct run *r;

      acquire(&kmem.lock);
      r = kmem.freelist;
      if (r) {
        kmem.freelist = r->next;
        acquire(&refcnt.lock);
        refcnt.cnt[(uint64)r / PGSIZE] = 1;
        release(&refcnt.lock);
      }
      release(&kmem.lock);

      if(r)
        memset((char*)r, 5, PGSIZE); // fill with junk
      return (void*)r;
    }

    void
    kfree(void *pa)
    {
      struct run *r;

      if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

      // 引用计数不为0时直接返回
      acquire(&refcnt.lock);
      if (--refcnt.cnt[(uint64)pa / PGSIZE] != 0) {
        release(&refcnt.lock);
        return;
      }
      release(&refcnt.lock);
      // Fill with junk to catch dangling refs.
      memset(pa, 1, PGSIZE);

      r = (struct run*)pa;

      acquire(&kmem.lock);
      r->next = kmem.freelist;
      kmem.freelist = r;
      release(&kmem.lock);
    }
    ```

5. 在`kernel/kalloc.c`中添加函数判断是否为`cow`页面。需要判断`va`是否大于`MAXVA`，否则`usertests`会产生`panic`，因为在`user/usertests`的`copyout()`测试中，使用一个大地址作为参数传递给`read`函数。

    ```c++
    int is_cowpage(pagetable_t pagetable, uint64 va) {
      if (va >= MAXVA)
        return 0;
      pte_t* pte = walk(pagetable, va, 0);
      if (pte == 0)
        return 0;
      if ((*pte && PTE_V) == 0)
        return 0;
      return (*pte & PTE_F) ? 1 : 0;
    }
    ```

6. 在`kernel/kalloc.c`中添加为`cow`页面分配新空间的函数，返回新分配的物理地址，`va`需要对齐页边界。

    ```c++
    void*
    cowalloc(pagetable_t pagetable, uint64 va) {
      if (va % PGSIZE != 0)
        return 0;
      uint64 pa = walkaddr(pagetable, va);
      if (pa == 0)
        return 0;
      pte_t* pte = walk(pagetable, va, 0);

      if (get_refcnt(pa) == 1) {
        //只有一个进程对该物理地址进行引用，直接修改PTE
        *pte |= PTE_W;
        *pte &= ~PTE_F;
        return (void*)pa;
      }
      else {
        //重新分配空间
        char* mem = kalloc();
        if (mem == 0)
          return 0;

        memmove(mem, (char*)pa, PGSIZE);
        *pte &= ~PTE_V;//需要将pte的有效标志清除，再执行mappages，否则会被判定为重复映射
        if (mappages(pagetable, va, PGSIZE, (uint64)mem, (PTE_FLAGS(*pte) | PTE_W) & ~PTE_F) != 0) {
          //直接|PTE_W是因为只对可写的页面设置PTE_F
          kfree(mem);
          *pte |= PTE_V;
          return 0;
        }
        kfree((char*)pa);
        return mem;
      }
    }

    ```

7. 在`kernel/kalloc.c`中，修改`freerange()`。

    ```c++
    void
    freerange(void *pa_start, void *pa_end)
    {
      char *p;
      p = (char*)PGROUNDUP((uint64)pa_start);
      for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
        refcnt.cnt[(uint64)p / PGSIZE] = 1;//kfree中将cnt-1，所以先设置为1
        kfree(p);
      }
    }    
    ```

8. 在`kernel/vm.c`中，修改`uvmcopy()`。`uvmcopy()`用于将父进程的页表复制到子进程的页表，取消原先的重新分配内存，对只读页面直接复制页表项，对可写页面设置`PTE_F`，表示其为`COW`页面。

    ```c++
    int
    uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
    {
      pte_t *pte;
      uint64 pa, i;
      uint flags;
      //char *mem;

      for(i = 0; i < sz; i += PGSIZE){
        if((pte = walk(old, i, 0)) == 0)
          panic("uvmcopy: pte should exist");
        if((*pte & PTE_V) == 0)
          panic("uvmcopy: page not present");
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        if (flags & PTE_W) {
          //只对可写的页面设置PTE_F
          flags = (flags | PTE_F) & (~PTE_W);
        }
        if (mappages(new, i, PGSIZE, pa, flags) != 0) {
          uvmunmap(new, 0, i / PGSIZE, 1);
          return -1;
        }
        *pte = PA2PTE(pa) | flags;//修改父进程pte
        inc_refcnt(pa, 1);
      }
      return 0;
    }
    ```

9. 修改`kernel/trap.c`中的`usertrap()`，增加`lazy allocation`。

    ```c++
    void
    usertrap(void)
    {
      int which_dev = 0;

      if((r_sstatus() & SSTATUS_SPP) != 0)
        panic("usertrap: not from user mode");

      // send interrupts and exceptions to kerneltrap(),
      // since we're now in the kernel.
      w_stvec((uint64)kernelvec);

      struct proc *p = myproc();
      
      // save user program counter.
      p->trapframe->epc = r_sepc();
      uint64 cause = r_scause();
      if(cause== 8){
        // system call

        if(p->killed)
          exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        p->trapframe->epc += 4;

        // an interrupt will change sstatus &c registers,
        // so don't enable until done with those registers.
        intr_on();

        syscall();
      } else if((which_dev = devintr()) != 0){
        // ok
      } else if (cause == 13 || cause == 15) {
        uint64 fault_va = r_stval();
        if (fault_va >= p->sz || is_cowpage(p->pagetable, fault_va) == 0 || cowalloc(p->pagetable, PGROUNDDOWN(fault_va)) == 0)
          p->killed = 1;
      } else {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        p->killed = 1;
      }

      if(p->killed)
        exit(-1);

      // give up the CPU if this is a timer interrupt.
      if(which_dev == 2)
        yield();

      usertrapret();
    }
    ```

10. 在`kernel/vm.c`中修改`copyout()`，添加`lazy allocation`。

    ```c++
    int
    copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
    {
      uint64 n, va0, pa0;

      while(len > 0){
        va0 = PGROUNDDOWN(dstva);
        if (is_cowpage(pagetable, va0)) {
          pa0 = (uint64)cowalloc(pagetable, va0);
        } else pa0 = walkaddr(pagetable, va0);
        if(pa0 == 0)
          return -1;
        n = PGSIZE - (dstva - va0);
        if(n > len)
          n = len;
        memmove((void *)(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
      }
      return 0;
    }    
    ```

11. 将使用到的函数添加到kernel/defs.h中

    ```c++
    // kalloc.c
    int             is_cowpage(pagetable_t pagetable, uint64 va);
    void*           cowalloc(pagetable_t pagetable, uint64 va);
    void            inc_refcnt(uint64 pa, int val);
    // vm.c
    pte_t*          walk(pagetable_t pagetable, uint64 va, int alloc);
    ```

- 实验结果：

    ```bash
    $ cowtest
    simple: ok
    simple: ok
    three: ok
    three: ok
    three: ok
    file: ok
    ALL COW TESTS PASSED
    $ usertests
    ...
    test fourteen: OK
    test bigfile: OK
    test dirfile: OK
    test iref: OK
    test forktest: OK
    test bigdir: OK
    ALL TESTS PASSED
    ```
