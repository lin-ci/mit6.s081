# [Lab 8: locks](https://pdos.csail.mit.edu/6.828/2020/labs/lock.html)

- 切换到lock分支 `git switch lock`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:Memory allocator (moderate)

- 实验要求：

    实验前：所有CPU共享一块空闲内存，当多个CPU都需要分配和释放内存时，需要争夺保护该共享内存的锁，造成锁的竞争较大。

    实验后：为每个CPU维护一个空闲链表，每个链表都有自己的锁。分配和释放内存时，每个CPU优先查看自己所拥有空闲链表，再查看其他CPU的空闲链表。
  
- 实验步骤：

1. 将`kernel/kalloc.c`中的`kmem`定义为一个数组，每个`CPU`对应一个。

    ```c++
    struct {
      struct spinlock lock;
      struct run *freelist;
    } kmem[NCPU];
    ```

2. 修改`kernel/kalloc.c`中的`kinit()`：
   1. 按照要求，将每个锁的名字初始化为`kmem`开头的名字。
   2. 此时执行`freerange()`时，将所有内存挂到执行`kinit()`的`CPU`（即`CPU 0`）的空闲链表上。

    ```c++
    void
    kinit()
    {
      char lockname[NCPU];
      for (int i = 0; i < NCPU; ++i) {
        snprintf(lockname, sizeof(lockname), "kmem_%d", i);
        initlock(&kmem[i].lock, lockname); 
      }
      freerange(end, (void*)PHYSTOP);
    }
    ```

3. 修改`kernel/kalloc.c`中的`kfree()`：
   1. 使用`cpuid()`和它返回的结果时必须关中断。
   2. 即中断必须在`id=cpuid()`前关闭，当不再使用`id`变量时再打开。因为有可能开中断后，发生`timer interrupt`，当前进程`sleep`，再次醒来时可能在不同的`CPU`上运行，此时`id！=cpuid()`。
   3. 思考：在原来的代码，为什么不用关中断？
      1. ans：在`acquire()`中已经关闭了中断。

    ```c++
      void
      kfree(void *pa)
      {
        struct run *r;

        if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
          panic("kfree");

        // Fill with junk to catch dangling refs.
        memset(pa, 1, PGSIZE);

        r = (struct run*)pa;

        //将释放的内存放到对应CPU的空闲列表上
        push_off();
        int id = cpuid();//获取CPUid时需要关中断
        acquire(&kmem[id].lock);
        r->next = kmem[id].freelist;
        kmem[id].freelist = r;
        release(&kmem[id].lock);
        pop_off();
      }
    ```

4. 修改`kernel/kalloc.c`中的`kalloc()`，使用`cpuid()`和它返回的结果时必须关中断。

    ```c++
    void *
    kalloc(void)
    {
      struct run *r;

      push_off();
      int id = cpuid();
      acquire(&kmem[id].lock);
      r = kmem[id].freelist;
      if(r)
        kmem[id].freelist = r->next;//先查看当前CPU的空闲链表
      else {
        for (int i = 0; i < NCPU; ++i) {//遍历其他CPU的空闲链表
          if (i == id)continue;
          acquire(&kmem[i].lock);
          r = kmem[i].freelist;
          if (r) {
            kmem[i].freelist = r->next;
            release(&kmem[i].lock);
            break;
          }
          release(&kmem[i].lock);
        }
      }
      release(&kmem[id].lock);

      if(r)
        memset((char*)r, 5, PGSIZE); // fill with junk
      pop_off();
      return (void*)r;
    }
    ```

- 结果：

1. 实验前：

    ```bash
    xv6 kernel is booting

    hart 1 starting
    hart 2 starting
    init: starting sh
    $ kalloctest
    start test1
    test1 results:
    --- lock kmem/bcache stats
    lock: kmem: #fetch-and-add 2070927 #acquire() 433016
    lock: bcache: #fetch-and-add 0 #acquire() 334
    --- top 5 contended locks:
    lock: kmem: #fetch-and-add 2070927 #acquire() 433016
    lock: proc: #fetch-and-add 564642 #acquire() 136272
    lock: virtio_disk: #fetch-and-add 430783 #acquire() 57
    lock: proc: #fetch-and-add 319693 #acquire() 136287
    lock: proc: #fetch-and-add 213041 #acquire() 136272
    tot= 2070927
    test1 FAIL
    start test2
    total free number of pages: 32499 (out of 32768)
    .....
    test2 OK
    ```

2. 实验后

    ```bash
    xv6 kernel is booting

    hart 2 starting
    hart 1 starting
    init: starting sh
    $ kalloctest
    start test1
    test1 results:
    --- lock kmem/bcache stats
    lock: bcache: #fetch-and-add 0 #acquire() 334
    --- top 5 contended locks:
    lock: proc: #fetch-and-add 394830 #acquire() 102033
    lock: proc: #fetch-and-add 313716 #acquire() 102033
    lock: proc: #fetch-and-add 269428 #acquire() 102033
    lock: proc: #fetch-and-add 222546 #acquire() 102033
    lock: proc: #fetch-and-add 222134 #acquire() 102034
    tot= 0
    test1 OK
    start test2
    total free number of pages: 32499 (out of 32768)
    .....
    test2 OK
    ```

3. `#fetch-and-add`表示由于尝试获取另一个内核已经持有的锁而进行的循环迭代次数。

## Part 2:Buffer cache (hard)

- 实验要求：

    实验前：`buffer cache`的每个节点连接起来形成链表，同时有一个锁用来保护该链表的结构。从磁盘读取`block`时，将其存入内存中的`buffer cache`，此时需要从链表中找到或者分配节点，需要获取保护该`buffer cach`e的锁。多个进程密集地使用文件系统时，锁竞争较大。

    实验后：不再将所有`buffer cache`的节点放在一个链表，将其分成多个链表，放在不同的桶中。获取节点时，根据`block num`，计算其在可能哪个桶中。若没有该`cache`，则从自己或者其他桶中分配节点，并将其插入对应的桶中。此时将减少锁竞争。

- 实验步骤：

1. 在`kernel/bio.c`中，修改`struct bcache`。
   1. 使用质数个数的桶，减少`Hash`冲突。

    ```c++
    #define NBUCKET 13
    #define HASH(id) (id%NBUCKET)

    struct hashbuf {
      struct buf head;
      struct spinlock lock;
    };

    struct {
      struct buf buf[NBUF];
      struct hashbuf buckets[NBUCKET];
      // Linked list of all buffers, through prev/next.
      // Sorted by how recently the buffer was used.
      // head.next is most recent, head.prev is least.
    } bcache;
    ```

2. 在`kernel/bio.c`中，修改`binit()`。
   1. 初始化时将所有节点挂到`bucket0`上。
   2. 初始化每个桶的锁，以及头节点。

    ```c++
    void
    binit(void)
    {
      struct buf *b;
      char lockname[16];
      for (int i = 0; i < NBUCKET; ++i) {
        snprintf(lockname, sizeof(lockname), "bcache_%d", i);
        initlock(&bcache.buckets[i].lock, lockname);

        // 头结点
        bcache.buckets[i].head.prev = &bcache.buckets[i].head;
        bcache.buckets[i].head.next = &bcache.buckets[i].head;
      }

      for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->next = bcache.buckets[0].head.next;
        b->prev = &bcache.buckets[0].head;
        initsleeplock(&b->lock, "buffer");
        bcache.buckets[0].head.next->prev = b;
        bcache.buckets[0].head.next = b;
      }
    }
    ```

3. 在`kernel/buf.h`中，根据提示，在`buf`增加字段`timestamp`。用途：在原始方案中，每次`brelse`将释放的节点放置到链表头，节点的使用频率从前往后依次递减（除了正在使用的节点），查找时从后往前，找到的第一个空闲的即为最久未使用的。而使用`timestamp`进行最久未使用的判定，则`brelese`时，无需将节点移动到头部。

    ```c++
    struct buf {
      int valid;   // has data been read from disk?
      int disk;    // does disk "own" buf?
      uint dev;
      uint blockno;
      struct sleeplock lock;
      uint refcnt;
      struct buf *prev; // LRU cache list
      struct buf *next;
      uchar data[BSIZE];
      uint timestamp;
    };
    ```

4. 在`kernel/bio.c`中，修改`brelse()`。只需要更新`timestamp`和`refcnt`即可。

    ```C++
    void
    brelse(struct buf *b)
    {
      if(!holdingsleep(&b->lock))
        panic("brelse");
      int id = HASH(b->blockno);//blockno受hashub.lock保护，但此时持有buf的sleeplock，refcnt>0，不会有其他CPU对blockno进行修改，访问是安全的。
      releasesleep(&b->lock);

      acquire(&bcache.buckets[id].lock);
      b->refcnt--;
      //更新timestamp
      acquire(&tickslock);
      b->timestamp = ticks;
      release(&tickslock);

      release(&bcache.buckets[id].lock);
    }
    ```

5. 在`kernel/bio.c`中修改`bget()`:
   1. 代码中：有可能`CPU`持有桶`A`的锁，要获取桶`B`的锁，此时另一个`CPU`持有桶`B`的锁，要获取桶`A`的锁，造成死锁。但出现该情况说明所有节点均被占用，没有可用节点。就算避免了死锁，代码最后也会执行`panic()`。所以代码中不对死锁做额外判断。
   2. 避免死锁的方法：
      1. 破坏循环等待条件。制定一个规则，当`CPU`获取到了桶`A`的锁时，能够获取桶`B`的锁，那么获取到了桶`B`的锁的`CPU`则不能获取桶`A`的锁。例如当获取到了桶`A`的锁时，只能获取到`(A+1)%NBUCKET~(A+len)%NBUCKET`之间的桶的锁，其中`len=floor(NBUCKET/2)`。
      2. 按桶的大小获取锁。当`CPU`获取到了桶`A`的锁时，想要获取桶`B`的锁。如果`B>A`则直接获取，否则先释放`A`锁，获得`B`锁再获得`A`锁，但此时违反了原子性。即释放`A`锁时再获取`A`锁的时间内，有可能另一个`CPU`在桶`A`中加入了对应的`block`。导致对应的`block`在`buffer cache`出现两次。

    ```c++
    static struct buf*
    bget(uint dev, uint blockno)
    {
      struct buf* b;
      struct buf* free = 0;
      int id = HASH(blockno);
      acquire(&bcache.buckets[id].lock);

      // 根据blockno查找对应id所在的桶
      for(b = bcache.buckets[id].head.next; b != &bcache.buckets[id].head; b = b->next){
        if(b->dev == dev && b->blockno == blockno){//找到对应块的cache
          b->refcnt++;
          //记录timestamp
          acquire(&tickslock);
          b->timestamp = ticks;
          release(&tickslock);
          //释放桶的锁，获取节点的锁
          release(&bcache.buckets[id].lock);
          acquiresleep(&b->lock);
          return b;
        }
        if (b->refcnt == 0 && (free == 0 || free->timestamp > b->timestamp))free = b;
      }

      // id桶有空闲节点
      if (free) {
        free->dev = dev;
        free->blockno = blockno;
        free->valid = 0;
        free->refcnt = 1;
        release(&bcache.buckets[id].lock);
        acquiresleep(&free->lock);
        return free;
      }
      // Not cached.
      // Recycle the least recently used (LRU) unused buffer.
      // 查找其他buckets是否有空闲节点
      for (int i = (id + 1) % NBUCKET; i != id; i = (i + 1) % NBUCKET) {
        acquire(&bcache.buckets[i].lock);
        for (b = bcache.buckets[i].head.prev; b != &bcache.buckets[i].head; b = b->prev) {
          if (b->refcnt == 0 && (free == 0 || free->timestamp > b->timestamp))free = b;
        }
        if (free) {
          free->dev = dev;
          free->blockno = blockno;
          free->valid = 0;
          free->refcnt = 1;

          //将节点取出
          free->prev->next = free->next;
          free->next->prev = free->prev;
          //插入id桶
          free->prev = &bcache.buckets[id].head;
          free->next = bcache.buckets[id].head.next;
          bcache.buckets[id].head.next->prev = free;
          bcache.buckets[id].head.next = free;

          release(&bcache.buckets[i].lock);
          release(&bcache.buckets[id].lock);
          acquiresleep(&free->lock);
          return free;
        }
        release(&bcache.buckets[i].lock);
      }
      panic("bget: no buffers");
    }
    ```

6. 在`kernel/bio.c`中修改`bpin()`和`bunpin()`

    ```c++
    void
    bpin(struct buf* b) {
      int id = HASH(b->blockno);
      acquire(&bcache.buckets[id].lock);
      b->refcnt++;
      release(&bcache.buckets[id].lock);
    }

    void
    bunpin(struct buf* b) {
      int id = HASH(b->blockno);
      acquire(&bcache.buckets[id].lock);
      b->refcnt--;
      release(&bcache.buckets[id].lock);
    }    
    ```

- 实验结果

    ```bash
    xv6 kernel is booting

    hart 2 starting
    hart 1 starting
    init: starting sh
    $ bcachetest
    start test0
    test0 results:
    --- lock kmem/bcache stats
    --- top 5 contended locks:
    lock: virtio_disk: #fetch-and-add 13072774 #acquire() 1191
    lock: proc: #fetch-and-add 1485244 #acquire() 106150
    lock: proc: #fetch-and-add 1357452 #acquire() 106522
    lock: proc: #fetch-and-add 1008237 #acquire() 106149
    lock: proc: #fetch-and-add 978373 #acquire() 106172
    tot= 0
    test0: OK
    start test1
    test1 OK
    ```
