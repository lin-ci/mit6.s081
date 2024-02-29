# [Lab 3: page tables](https://pdos.csail.mit.edu/6.828/2020/labs/pgtbl.html)

- 切换到pgtbl分支 `git switch pgtbl`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:Print a page table (easy)

- 实验内容：添加一个打印页表内容的函数

- 实验步骤：

1. 在`kernel/exec.c`中的`exec()`函数`return argc`之前添加`if (p->pid == 1)vmprint(p->pagetable);`
2. 在`kernel/defs.h`中添加函数原型`void vmprint(pagetable_t);`
3. 在`vm.c`中仿照`freewalk`函数加入以下两个函数。

    ```c++
    //递归打印页表
    void _vmprint(pagetable_t pagetable, int level) {
      for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if (pte & PTE_V) {
          for (int j = 0; j < level; j++) {
            if (j)printf(" ");
            printf("..");
          }
          uint64 child = PTE2PA(pte);//将页表项转化成对应的地址
          printf("%d: pte %p pa %p\n", i, pte, child);
          if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            //该页表项的地址指向下一级页表
            _vmprint((pagetable_t)child, level + 1);
          }
        }
      }
    }

    //打印页表
    void vmprint(pagetable_t pagetable) {
      printf("page table %p\n", pagetable);
      _vmprint(pagetable, 1);
    }
    ```

- 效果：

  ```bash
  hart 1 starting
  hart 2 starting
  page table 0x0000000087f6e000
  ..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
  .. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
  .. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
  .. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
  .. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
  ..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
  .. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
  .. .. ..510: pte 0x0000000021fdd807 pa 0x0000000087f76000
  .. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
  init: starting sh
  $
  ```

## Part 2:A kernel page table per process (hard)

- 实验内容：为每一个用户进程分配一个内核页表。（在Part3部分将进程的虚拟地址空间复制到内核页表中。这样在内核访问进程的虚拟地址时，就不需要先执行`walk`函数将其转化为物理地址再访问。）
- 实验步骤：

1. 在`kernel/proc.h`中的`struct proc`添加进程内核页表字段`pagetable_t proc_kpgtb;`。
2. 在`kernel/vm.c`中仿照`kvminit`和`kvmmap`添加以下函数，用于分配进程的内核页表，同时在`kernel/def.s`添加函数原型`pagetable_t proc_kvminit()`和`void proc_kvmmap(pagetable_t, uint64, uint64, uint64, int)`。

    ```c++
    void proc_kvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm) {
      if (mappages(pagetable, va, sz, pa, perm) != 0)
        panic("proc_kvmmap");
    }
    //分配进程内核页表
    pagetable_t
    proc_kvminit() {
      pagetable_t proc_kpgtb = uvmcreate();
      if (proc_kpgtb == 0)return 0;
      proc_kvmmap(proc_kpgtb, UART0, UART0, PGSIZE, PTE_R | PTE_W);
      proc_kvmmap(proc_kpgtb, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
      proc_kvmmap(proc_kpgtb, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
      proc_kvmmap(proc_kpgtb, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);
      proc_kvmmap(proc_kpgtb, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);
      proc_kvmmap(proc_kpgtb, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
      return proc_kpgtb;
    }
    ```

3. 在`kernel/proc.c`中的`allocproc`函数分配进程内核页表，将`procinit`函数中分配进程内核栈的代码移动到`allocproc`函数中。并将`kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);`改为`proc_kvmmap(p->proc_kpgtb, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);`

    除了第一个进程，每个进程都是由其他进程`fork`或者`exec`产生的。对于创建第一个进程，对应`kernel/proc.c`的`userinit()`。其都会调用`allocproc`分配可用的进程结构体。根据提示，在`allocproc`中加入分配进程内核页表的代码。此时所有进程都分配了内核页表。

    ```c++
    void
    procinit(void)
    {
      struct proc *p;
      
      initlock(&pid_lock, "nextpid");
      for(p = proc; p < &proc[NPROC]; p++) {
          initlock(&p->lock, "proc");
      }
      kvminithart();
    }

    static struct proc*
    allocproc(void)
    {
      ...
      //分配进程内核页表
      p->proc_kpgtb = proc_kvminit();
      if (p->proc_kpgtb == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
      }
      // Allocate a page for the process's kernel stack.
      // Map it high in memory, followed by an invalid
      // guard page.
      char* pa = kalloc();
      if (pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int)(p - proc));
      proc_kvmmap(p->proc_kpgtb, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
      p->kstack = va;
      ...
    }
    ```

4. 在`kernel/vm.c`加入切换进程内核页表的函数，同时在`kernel/defs.h`中加入函数原型`void proc_kvminithart(pagetable_t)`。

    ```c++
    //切换进程内核页表
    void
    proc_kvminithart(pagetable_t proc_kpgtb)
    {
      w_satp(MAKE_SATP(proc_kpgtb));
      sfence_vma();
    }
    ```

5. 修改`kernel/proc.c`中的`scheduler()`。对于第一个进程以及`fork`得到的新进程，状态为`RUNABLE`，需要等待被调度，当其被调度时，切换到进程的内核页表，之后执行该进程的内核线程。之后从`trap`返回，切换为进程的页表，执行进程的代码。

    ```c++
    void
    scheduler(void)
    {
      ...
      //swtch之前执行的是进程调度，使用的是内核页表
      //swtch执行后切换到进程的内核线程，需要切换为进程的内核页表
      proc_kvminithart(p->proc_kpgtb);
      swtch(&c->context, &p->context);
      //swtch返回时执行进程调度，切换回内核页表。当进程被kill时，进程的内核页表会被释放，所以不能继续使用进程的内核页表。
      kvminithart();
      ...
    }
    ```

6. 在`kernel/proc.c`的`freeproc`函数释放内核页表及内核栈。

    ```c++
    static void
    freeproc(struct proc* p)
    {
      ...
      //释放进程内核页表中的内核栈
      uvmunmap(p->proc_kpgtb, p->kstack, 1, 1);
      p->kstack = 0;
      //释放进程内核页表
      if (p->proc_kpgtb)
        proc_freekpgtb(p->proc_kpgtb);
      ...
    }
    ```

7. 在`kernel/proc.c`中添加函数释放进程内核页表（只需解除映射即可，即将页表项设置为0），同时在`kernel/defs.h`中添加函数原型`void proc_freekpgtb(pagetable_t)`。

    ```c++
    //释放进程内核页表
    void
    proc_freekpgtb(pagetable_t proc_kpgtb)
    {
      //一个页表有2^9=512页表项
      for (int i = 0; i < 512; i++) {
        pte_t pte = proc_kpgtb[i];
        if (pte & PTE_V) {
          proc_kpgtb[i] = 0;
          if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            //页表项地址执行下一级页表，递归释放
            uint64 child = PTE2PA(pte);
            proc_freekpgtb((pagetable_t)child);
          }
        }
      }
      kfree((void*)proc_kpgtb);//释放页表所在的页面
    }
    ```

8. 修改`vm.c`中的`kvmpa()`函数，使用进程的内核页表，并引入头文件`#include "spinlock.h"`和`#include "proc.h"`。`kvmpa`用来将进程内核栈中的虚拟地址转化为物理地址，此时内核栈在进程的内核页表中。

    ```c++
    uint64
    kvmpa(uint64 va)
    {
      ...
      //pte = walk(kernel_pagetable, va, 0);
      pte = walk(myproc()->proc_kpgtb, va, 0);//modify
      ...
    }
    ```

- 效果：

  ```bash
  $ usertests
  ...
  test dirfile: OK
  test iref: OK
  test forktest: OK
  test bigdir: OK
  ALL TESTS PASSED
  $ 
  ```

## Part 3:Simplify copyin/copyinstr (hard)

- 实验内容：将用户空间的映射添加到每个进程的内核页表（上一节中创建），以允许`copyin`（和相关的字符串函数`copyinstr`）直接解引用用户指针。
- 实验步骤：

1. 在`kernel/vm.c`添加函数将进程页表复制到进程内核页表，同时在`kernel/defs.h`添加函数原型`void utokvmcopy(pagetable_t, pagetable_t, uint64, uint64)`。

    对于`pte`需要直接赋值，而不使用`mappages`是因为在`exec`中，`proc_kpgtbl`已经有原先进程的页表项，此时需要调用`utokvmcopy`重新映射新进程的页表项。如果是`mappages`则会`remap`。

    ```c++
    //将进程页表复制到进程内核页表
    void
    utokvmcopy(pagetable_t pagetable, pagetable_t proc_kpgtbl, uint64 oldsz, uint64 newsz)
    {
      pte_t* pte, * new_pte;
      uint64 pa, i;
      uint flags;
      //PGROUNDDOWN(oldsz)~PGROUNDUP(oldsz)的页面已经在上次被映射。
      oldsz = PGROUNDUP(oldsz);
      for (i = oldsz; i < newsz; i += PGSIZE) {
        if ((pte = walk(pagetable, i, 0)) == 0)
          panic("utokvmcopy: pte should exist");
        if ((*pte & PTE_V) == 0)
          panic("utokvmcopy: page not present");
        if ((new_pte = walk(proc_kpgtbl, i, 1)) == 0)
          panic("utokvmcopy: pte should exist");
        
        pa = PTE2PA(*pte);
        //在内核态无法访问设置了PTE_U的页面，需要将其移除
        flags = (PTE_FLAGS(*pte)) & (~PTE_U);
        *new_pte = PA2PTE(pa) | flags;
      }
    }
    ```

2. 在修改进程的用户页表的函数中 `fork()`、`exec()`和`growproc()`，同步更新进程的内核页表。其中`exec()`和`fork()`创建了一个新的页表。`growproc`修改了进程空间的大小，代码中只对进程空间变大的情况更新进程内核页表，变小的情况没有更新，但也能通过测试。

    ```c++
    //kernel/exec.c
    int
    exec(char* path, char** argv)
    {
      ...
      //同步更新到进程内核页表
      utokvmcopy(pagetable, p->proc_kpgtb, 0, sz);
      // Commit to the user image.
      ...
    }
    ```

    ```c++
    //kernel/proc.c
    int
    fork(void)
    {
      ...
      // Copy user memory from parent to child.
      if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        freeproc(np);
        release(&np->lock);
        return -1;
      }
      np->sz = p->sz;
      np->parent = p;
      //将进程页表的修改同步更新到进程内核页表
      utokvmcopy(np->pagetable, np->proc_kpgtb, 0, np->sz);
      ...
    }
    ```

    ```c++
    //kernel/proc.c
    int
    growproc(int n)
    {
      ...
      if(n > 0){
        if (PGROUNDUP(sz + n) >= PLIC) {
          //进程虚拟地址不能超过PLIC地址
          return -1;
        }
        if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
          return -1;
        }
        utokvmcopy(p->pagetable, p->proc_kpgtb, sz - n, sz);//此时sz已经加上了n
      } 
      ...
    }    
    ```

3. 修改`copyin`和`copyinstr`，同时将`copyin_new`和`int copyinstr_new`函数原型添加到`kernel/sefs.h`中，其中`copyin_new`和`copyinsrt_new`函数位于`kernel/vmcopyin.c`中。修改后执行`copyin`和`copyinstr`函数时，使用的是`satp`指向的页表（即进程的内核页表），可以直接访问进程的地址空间。而不用先访问进程的页表，调用`walk()`将虚拟地址转化成物理地址，再进行访问。

    ```c++
    //vmcopyin.c
    int copyin_new(pagetable_t, char*, uint64, uint64);
    int copyinstr_new(pagetable_t, char*, uint64, uint64);
    ```

    ```c++
    int
    copyin(pagetable_t pagetable, char* dst, uint64 srcva, uint64 len)
    {
      return copyin_new(pagetable, dst, srcva, len);
    }
    int
    copyinstr(pagetable_t pagetable, char* dst, uint64 srcva, uint64 max)
    {
      return copyinstr_new(pagetable, dst, srcva, max);
    }
    ```

4. 在`kernel/proc.c`中的`userinit()`中，为第一个进程添加用户空间的映射。

    ```c++
      ...
      p->sz = PGSIZE;
      utokvmcopy(p->pagetable, p->proc_kpgtb, 0, p->sz);
      ...
    ```

5. 解释为什么在`copyin_new()`中需要第三个测试`srcva + len < srcva`：给出`srcva`和`len`值的例子，这样的值将使前两个测试为假（即它们不会导致返回-1），但是第三个测试为真 （导致返回-1）。

    ```c++
    if (srcva >= p->sz || srcva+len >= p->sz || srcva+len < srcva)
    return -1;
    ```

    ans：len+srcva溢出时。两个相加结果为负数。
