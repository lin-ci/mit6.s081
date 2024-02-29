# [Lab 7: Multithreading](https://pdos.csail.mit.edu/6.828/2020/labs/thread.html)

- 切换到thread分支 `git switch thread`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:switching between threads (moderate)

- 实验要求：实现用户级线程。在xv6有两个文件：`user/uthread.c`和`user/uthread_switch.S`。`uthread.c`包含大多数用户级线程包，以及三个简单测试线程的代码。但缺少部分创建线程和在线程之间切换的代码，通过补充代码使得`uthread.c`可以运行。

    用户级线程本质上是以函数调用的形式执行`thread_switch()`，进行上下文切换，通过设置ra，sp使得在函数返回时切换到另一个线程。在用户态不能直接设置pc。线程需要之间自行协商让出CPU。
- 实验步骤：

1. 在`user/uthread_switch.S`中加入以下代码。

    ```text
      .globl thread_switch
    thread_switch:
      /* YOUR CODE HERE */
      sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

      ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
      ret    /* return to ra */
    
    ```

2. 在`user/uthread.c`中定义用户线程上下文结构体，并在`thread`结构体中使用。

    ```c++
    //用户线程上下文结构体
    struct context {
      uint64 ra;
      uint64 sp;

      // callee-saved
      uint64 s0;
      uint64 s1;
      uint64 s2;
      uint64 s3;
      uint64 s4;
      uint64 s5;
      uint64 s6;
      uint64 s7;
      uint64 s8;
      uint64 s9;
      uint64 s10;
      uint64 s11;
    };
    struct thread {
      char       stack[STACK_SIZE]; /* the thread's stack */
      int        state;             /* FREE, RUNNING, RUNNABLE */
      struct context  context;
    };
    ```

3. 在`thread_create()`对`context->ra,sp`进行初始化

    ```c++
    void 
    thread_create(void (*func)())
    {
      ...
      // YOUR CODE HERE
      t->context.ra = (uint64)func;
      t->context.sp = (uint64)t->stack + STACK_SIZE;
    }
    ```

4. 修改`thread_scheduler()`，添加线程切换语句。

    ```c++
    void 
    thread_schedule(void)
    {
      ...
      if (current_thread != next_thread) {         /* switch threads?  */
        next_thread->state = RUNNING;
        t = current_thread;
        current_thread = next_thread;
        /* YOUR CODE HERE
        * Invoke thread_switch to switch from t to next_thread:
        * thread_switch(??, ??);
        */
        thread_switch((uint64)&t->context, (uint64)&current_thread->context);
      } else
        next_thread = 0;
    } 
    ```

- 实验结果：

    ```bash
    $ uthread
    thread_a started
    thread_b started
    thread_c started
    thread_c 0
    thread_a 0
    thread_b 0
    thread_c 1
    thread_a 1
    thread_b 1
    ...
    thread_c 99
    thread_a 99
    thread_b 99
    thread_c: exit after 100
    thread_a: exit after 100
    thread_b: exit after 100
    thread_schedule: no runnable threads    
    ```

## Part 2:Using threads (moderate)

- 实验要求：`notxv6/ph.c`包含一个简单的哈希表，如果单个线程使用，该哈希表是正确的，但是多个线程使用时，该哈希表是不正确的。通过加锁使得能够正确运行。
- 实验步骤：

1. 为每个桶定义一个锁，并在main中初始化

    ```c++
    pthread_mutex_t lock[NBUCKET];
    for (int i = 0; i < NBUCKET; i++) 
      pthread_mutex_init(&lock[i], NULL);
    ```

2. 在put函数中加锁，加锁的范围：
   1. 查找是否存在key前，直到插入后。（粒度较大）
   2. 只对insert加锁。（因为代码保证了不会有多个线程同时相同插入相同的key，否则会导致相同key重复插入）

    ```c++
    static 
    void put(int key, int value)
    {
      int i = key % NBUCKET;

      // is the key already present?
      struct entry *e = 0;
      pthread_mutex_lock(&lock[i]);
      for (e = table[i]; e != 0; e = e->next) {
        if (e->key == key)
          break;
      }
      if(e){
        // update the existing key.
        e->value = value;
      } else {
        // the new is new.
        insert(key, value, &table[i], table[i]);
      }
      pthread_mutex_unlock(&lock[i]);
    }
    ```

- 实验结果：

    ```bash
    $ ./ph 4
    100000 puts, 4.231 seconds, 23635 puts/second
    3: 0 keys missing
    0: 0 keys missing
    2: 0 keys missing
    1: 0 keys missing
    400000 gets, 15.855 seconds, 25229 gets/second    
    ```

## Part 3:Barrier(moderate)

- 实验要求：将实现一个屏障（Barrier），当所有线程都调用barrier时，才能执行之后的代码，否则需要等待。
- 实验步骤

1. 在`notxv6/barrier.c`中完成`barrier()`函数。

    ```c++
    static void 
    barrier()
    {
      // YOUR CODE HERE
      //
      // Block until all threads have called barrier() and
      // then increment bstate.round.
      //
      pthread_mutex_lock(&bstate.barrier_mutex);
      bstate.nthread++;//对bstate.nthread修改需要获得锁
      if (bstate.nthread != nthread) {
        //先到达的线程需要释放锁并且等待
        pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
      }
      else {
        //最后一个到达的线程
        bstate.round++;
        bstate.nthread = 0;
        pthread_cond_broadcast(&bstate.barrier_cond);//唤醒其他线程
      }
      pthread_mutex_unlock(&bstate.barrier_mutex);
    }
    ```

- 实验结果

    ```bash
    $ ./barrier 2
    OK; passed
    ```
