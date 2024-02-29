# [Lab 9: file system](https://pdos.csail.mit.edu/6.828/2020/labs/fs.html)

- 切换到fs分支 `git switch fs`
- 运行xv6 `make qemu`
- 查看成绩 `make grade`

## Part 1:Large files (moderate)

- 实验要求：修改文件系统，使得可以支持的最大文件大小从268个块增加到65803个块。

    修改前：一个xv6 inode包含12个直接块号和一个一级间接块号，一级间接块指一个最多可容纳256个块号的块，总共12+256=268个块。

    修改后：增加一个二级间接块，此时总共256*256+256+11个块（11而不是12，二级间接块需要一个直接块号）。
- 实验步骤：

1. 在`kernel/fs.h`中添加宏定义。

    ```c++
    #define NDIRECT 11
    #define NINDIRECT (BSIZE / sizeof(uint))
    #define N2INDIRECT (BSIZE / sizeof(uint))*(BSIZE / sizeof(uint))
    #define MAXFILE (NDIRECT + NINDIRECT + N2INDIRECT)
    ```

2. 由于修改了NDIRECT，`kernel/fs.h`中的`struct dinode`和`kernel/file.h`中的`struct inode`中的`addrs`数量需要修改。

    ```c++
    struct dinode {
      short type;           // File type
      short major;          // Major device number (T_DEVICE only)
      short minor;          // Minor device number (T_DEVICE only)
      short nlink;          // Number of links to inode in file system
      uint size;            // Size of file (bytes)
      uint addrs[NDIRECT+2];   // Data block addresses
    };

    struct inode {
      uint dev;           // Device number
      uint inum;          // Inode number
      int ref;            // Reference count
      struct sleeplock lock; // protects everything below here
      int valid;          // inode has been read from disk?

      short type;         // copy of disk inode
      short major;
      short minor;
      short nlink;
      uint size;
      uint addrs[NDIRECT+2];
    };
    ```

3. 修改`kernel/fs.c`中的`bmap()`。其中对`inode->addrs[]`的修改由`bmap()`的调用者调用`iupdate()`后写入`disk`。

    例如：在`writei()`中

    ```c++
    // write the i-node back to disk even if the size didn't change
    // because the loop above might have called bmap() and added a new
    // block to ip->addrs[].
    iupdate(ip);
    ```

    ```c++
    static uint
    bmap(struct inode *ip, uint bn)
    {
      ...
      bn -= NINDIRECT;
      if (bn < N2INDIRECT) {
        //获取二级间接块
        if ((addr = ip->addrs[NDIRECT + 1]) == 0)
          ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
        bp = bread(ip->dev, addr);
        a = (uint*)bp->data;
        //获取一级间接块
        int idx = bn / NINDIRECT;
        if ((addr = a[idx]) == 0) {
          //此时对二级间接块进行修改
          a[idx] = addr = balloc(ip->dev);
          log_write(bp);
        }
        brelse(bp);
        bp = bread(ip->dev, addr);
        a = (uint*)bp->data;
        //获取直接块号
        idx = bn % NINDIRECT;
        if ((addr = a[idx]) == 0) {
          //此时对一级间接块进行修改
          a[idx] = addr = balloc(ip->dev);
          log_write(bp);
        }
        brelse(bp);
        return addr;
      }
      panic("bmap: out of range");
    }
    ```

4. 修改`kernel/fs.c`中的`itrunc()`，不用对删除的间接块进行`logwrite()`，只需调用`bfree()`将块号回收即可。

    ```c++
    void
    itrunc(struct inode *ip)
    {
      ...
      struct buf* bp1;
      uint* a1;
      if (ip->addrs[NDIRECT + 1]) {
        bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
        a = (uint*)bp->data;
        for (i = 0; i < NDIRECT; ++i) {
          //遍历每个一级间接块
          if (a[i]) {
            bp1 = bread(ip->dev, a[i]);
            a1 = (uint*)bp1->data;
            for (j = 0; j < NDIRECT; ++j) {
              //遍历每个直接块
              if (a1[j]) {
                bfree(ip->dev, a1[j]);
              }
            }
            brelse(bp1);
            bfree(ip->dev,a[i]);
          }
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT + 1]);
        ip->addrs[NDIRECT + 1] = 0;
      }
      ip->size = 0;
      iupdate(ip);
    }
    ```

- 实验结果：

    ```bash
    $ bigfile
    ...................................................
    wrote 65803 blocks
    bigfile done; ok
    ```

## Part 2:Symbolic links (moderate)

- 实验要求：

    在xv6中实现软连接，即增加一种`T_SYMLINK`类型的文件。文件中的内容为指向另一个文件的路径。

    实现一个系统调用`symlink(char* target,char* path)`，创建一个`path`指向的文件，内容为`target`，其中`target`执行的文件可以不存在。

    `open`系统调用时，访问该软链接指向的文件，若该文件也为软链接，则递归处理。为了避免死循环，规定递归深度不超过10。当指定`O_NOFOLLOW`时，可以打开这个软连接文件而非其指向的文件。

    其他系统调用（如`link`和`unlink`）对符号链接本身进行操作。不需要修改。
- 实验步骤：

1. 首先，为`symlink`创建一个新的系统调用号，在`user/usys.pl`、`user/user.h`中添加一个条目。并修改在`kernel/syscall.c`及`Makefile`。

    ```c++
    //user/usys.pl
    entry("symlink");
    //user/user.h
    int symlink(char* target, char* path);
    //kernel/syscall.h
    #define SYS_symlink 22
    //kernel/syscall.c
    extern uint64 sys_symlink(void);

    static uint64 (*syscalls[])(void) = {
    ...
    [SYS_symlink] sys_symlink,
    };
    //Makefile
    UPROGS=\
      ...
      $U/_symlinktest\
    ```

2. 添加宏定义。

    ```c++
    //kernel/stat.h
    #define T_SYMLINK 4   //symlink
    //kernel/fcntl.h
    #define O_NOFOLLOW  0x100
    ```

3. 在`kernel/sysfile.c`实现`sys_symlink()`。创建`path`指向的文件，内容为`target`。

    ```c++
    uint64
    sys_symlink()
    {
      char target[MAXPATH], path[MAXPATH];
      struct inode* ip;
      //获取用户传递的参数
      if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0) {
        return -1;
      }
      //对磁盘的修改需要开启事务
      begin_op();
      ip = create(path, T_SYMLINK, 0, 0);//创建文件，并返回带锁的inode
      if (ip == 0) {
        end_op();
        return -1;
      }
      //向该文件写入target
      if (writei(ip, 0, (uint64)target, 0, MAXPATH) != MAXPATH) {
        iunlockput(ip);
        end_op();
        return -1;
      }
      iunlockput(ip);
      end_op();
      return 0;
    }
    ```

4. 修改`kernel/sysfile.c`中的`sys_open()`，当文件类型
为`SYMLINK`且未设置`O_NOFOLLOW`时，循环获取其指向的文件。

    ```c++
    #define MAX_SYMLINK_DEPTH 10
    
    uint64
    sys_open(void)
    {
      char path[MAXPATH];
      int fd, omode;
      struct file *f;
      struct inode *ip;
      int n;

      if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
        return -1;

      begin_op();

      ...

      if (ip->type == T_SYMLINK) {
        int depth = 0;
        while (ip->type==T_SYMLINK&&!(omode & O_NOFOLLOW)) {
          depth++;
          if (depth > MAX_SYMLINK_DEPTH)break;
          if (readi(ip, 0, (uint64)path, 0, MAXPATH) != MAXPATH) {
            iunlockput(ip);
            end_op();
            return -1;
          }
          iunlockput(ip);
          ip = namei(path);
          if (ip == 0) {
            end_op();
            return -1;
          }
          ilock(ip);
        }
        if (depth > MAX_SYMLINK_DEPTH) {
          iunlock(ip);
          end_op();
          return -1;
        }
      }
      
      ...
      return fd;
    }

    ```

- 实验结果：

    ```bash
    xv6 kernel is booting

    init: starting sh
    $ symlinktest
    Start: test symlinks
    test symlinks: ok
    Start: test concurrent symlinks
    test concurrent symlinks: ok    

    $ usertests
    test dirfile: OK
    test iref: OK
    test forktest: OK
    test bigdir: OK
    ALL TESTS PASSED
    ```
