# ToyOS Final

省略平时作业代码实现以及最终持久化大作业实现，只是提供相关思路。

## 持久化大实验

### Part0——准备工作

- `okconfig`文件夹添加相关`.ok`文件
- 修改框架代码
  - 修改`user/include/ulib.h` `user/ulib/syscall.c` `lib/include/sysnum.h` `kernel/src/syscall.c`支持`mkfifo`


此时居然已经拿下10分了。

### Part1——支持大文件打包

- 修改`utils/mkfs.c`，支持大文件打包，支持二级索引
  - 修改`NDIRECT`宏定义为11
  - 修改`uint32_t addrs[NDIRECT + 1 + 1];`
  - 定义新的宏`#define NDOUBLE_INDIRECT (NINDIRECT * NINDIRECT)`
  - 修改`iwalk`函数处理二级索引

主要逻辑也就是获取addrs中二级索引内容，从而获得相对应的一级索引所在逻辑块地址，计算一级索引对应编号，从而得到一级索引地址。进一步获取二级索引在一级索引指向的逻辑块之中的编号，返回对应的逻辑块被映射到的内存地址。

### Part2——大文件支持

要求：修改inode的索引格式，将磁盘inode中的索引项调整为11项直接索引，1项一级间接索引，1项二级间接索引，让我们的磁盘文件系统支持的最大文件大小达到(11+1024+1024×1024)×4KiB≈4100MiB。

先前支持大文件打包已经基本实现，照着逻辑修改对应地方的代码逻辑即可。

涉及inode相关的：
- `fs.c`中的`dinode`结构
- `iwalk` `itrunc`

`iwalk`增加了二级索引

其中`itrunc`也需要清空二级索引以及用到的一级索引使用的逻辑块

### Part3——硬链接

修改磁盘文件系统，使其支持硬链接，同时实现link系统调用创建硬链接。

为了实现硬链接，需要修改磁盘inode的结构，新增一个引用计数项，用来管理有多少个目录项指向该inode（建议通过将某些成员从int改为short来挤出空间，以保持inode大小不变，因为我们要求inode大小为2的幂。

原先磁盘inode的结构如下：
```c
typedef struct dinode {
  uint32_t type;   // file type
  uint32_t device; // if it is a dev, its dev_id
  uint32_t size;   // file size
  uint32_t addrs[NDIRECT + 2]; // data block addresses, 11 direct and 1 indirect和一个二级间接索引
} dinode_t;
```

修改某些成员的大小挤出空间给引用计数？inode大小上文已知为64字节固定大小，type类型的大小压缩，毕竟只有三种类型，uint16_t完全够用。

何为硬链接？
- 硬链接实际上新建了一个指向与原来文件相同inode的链接，每一个inode都包含一个“链接数”，记录了有多少个硬链接指向此inode，当硬链接计数归零，代表这个inode指向的文件数据不再被需要，操作系统会释放其数据块和inode本身。这和编程语言中的GC（garbage collection）机制非常类似。
- 正因为硬链接指向了相同的inode，因此创建硬链接必须要在同一分区内（无论是物理分区或逻辑分区），因为在其他分区是找不到这个inode的。
- 新建的硬链接实际上和原本的文件对象地位是平等的，若新建了硬链接后，又rm删除原文件对象，实际上和mv命令移动文件是一样的效果。

首先修改type为`uint16_t`并为dinode增加引用计数。
```c
typedef struct {
  uint16_t type;   // file type
  uint16_t ref;     // 引用计数
  uint32_t device; // if it is a dev, its dev_id
  uint32_t size;   // file size
  uint32_t addrs[NDIRECT + 2]; // data block addresses, 12 direct and 1 indirect，大实验修改为11个直接索引+1个间接索引+1个二级索引
} dinode_t;
```

同时`mkfs`中申请dinode时初始化引用计数为1。

`fs.c`中同样的，修改dinode结构体。同时修改`dialloc`，申请时初始化引用计数为1。

创建硬链接的系统调用该如何写？由于创建的硬链接指向同一个物理磁盘上的inode，故而只要在新文件路径的父目录中添加目录项，该目录项的inode对应为源文件的，而非申请即可。

复用原先代码中的iopen和ilookup代码。只不过这里将找不到存在该文件就alloc一个改为就使用源文件对应的物理dinode。~~（写的有点屎山，不过有效）~~

至此已经可以通过硬链接相关的测试用例，不过添加了硬链接之后删除文件应该也要随之改变？
- 简单思路，修改`iremove`，覆盖目录项操作肯定要做，但是对应dinode的引用不为1，则不设置`del`位为1，这样只要还有硬链接指向该`dinode`，就不会真正将其从硬盘删除。

### Part4——软链接

修改磁盘文件系统，使其支持软链接，同时实现symlink系统调用创建软链接。

软链接可以视为一种类型不同的普通文件，其内容是链接到的文件的路径，打开软链接文件就相当于先打开它本身，读取它的内容，再打开它链接到的文件。

按照linux课讲的，其实也就是用一个小文件存储大文件的路径。有些low啊。


只过了一个测试用例，尴尬了，看了下本地的测试用例，应该是open时就要对于软链接进行处理了。

所以定义一种文件类型为`TYPE_LINK`用于软链接，`open`文件的逻辑修改一下即可，如果是软链接，先读取文件内容，再打开对应文件并返回其fd，核心代码如下：

存在问题？边缘情况！可能软链接文件还是指向一个软链接

至此，我们的toyos已经具备了使用软链接的能力。

### Part5——管道

根据linux课程我们知道，linux中一共有7种文件类型，管道也是其中一种，所以我们不如新建一种文件类型。不过这里的匿名管道真的是文件系统中的一类吗？

定义文件类型`TYPE_PIPE`

接下来呢，完全没有思路......在各类大模型的指导下，拿下了惊人的两分。

参照`linux0.11`源码进行推倒重来

自上而下进行分解：不打破抽象层！
- 管道也是一类文件，需要读和写的方法，先前的虚拟文件系统我们已经实现了普通文件和设备文件的读和写，理应这里也抽象出对于匿名管道文件的读和写方法。
- 类比`linux0.11`中的`m_inode`，设计一个结构体用于存储匿名管道相关的属性，使得能够共享同一块缓冲区。
- 通过信号量进行读和写的平衡

管道结构体如下

把管道也添加到文件结构体之中去

既然不关注底层实现只要系统调用符合，我仿照系统打开文件表，全局维持了一个管道池，进行管道的分配和销毁。


接下来我们就去实现匿名管道的系统调用创建一个匿名管道

申请两个文件描述符，一个作为读端，一个作为写端，并将两文件指向统一pipe缓存区内存，同时两文件赋予不同的读和写权限。

接着匿名文件的读和写也是我们此刻需要关注的。

首先是读操作

通过读端获取pipe共享区并进行读取，有些烦的就是读和写的逻辑，以及如何进行同步，具体流程就不赘述了，写的也比较垃圾。

读端的逻辑也差不多。

最后注意一下关闭某一端时候有可能读端或者写端全部关闭，需要唤醒写端或者读端。


至此，我们的系统也支持了匿名管道。

### Part6——命名管道

如何复用匿名管道？

差最后一个测试用例？最后命名管道的test2也没能通过，虽然助教提示了就是简单的边界情况，但de了一天也是无功而返，可能确实是有名管道的实现逻辑存在重大问题，因为我自己思考的时候也觉得好多地方不应该那样写，但似乎也不能想到更好的解决方案，只能硬着头皮往下写了。

首先仿造匿名管道，给我们的虚拟文件系统增加了一类文件系统`TYPE_FIFO`。

接着直奔主题编写系统调用`sys_mkfifo`，我们知道命名管道是一个存在于硬盘上的文件，所以我们这里给它分配一个inode，使得这个文件能够在硬盘上持久化，后续需要使用的话也能够按照路径找到，直接调用`iopen`函数即可，没有的话就创建一个`TYPE_FIFO`类型的文件。

接着我的想法感觉并不是很科学，只是想先将命名管道的功能实现，所以我就使用了先前匿名管道使用的管道池。将第一个位置特定给有名管道（这可能是不过的原因吗？）因此我们目前的系统之中同时只能有一个命名管道。

并对其进行了一些常规的初始化操作。由此我们就完成了有名管道系统调用的创建工作，不过没搞明白`mode`在创建时该如何使用，再添加一些属性标识该管道具有的权限？所以未加理会。

同样的读和写就复用了匿名管道那一套，这里也就不加以赘述了。

改进：只是当时就有疑问，匿名管道是两个文件描述符，且描述符顺序确定，所以可以判断哪一个进程关闭了是读端还是写端，有名管道只有一个文件，也只有打开时获得的一个文件描述符，如何做到这点？不过现在想来，`open`的逻辑单独改一下也返回两个文件描述符不就得了。所以关于是否写端和读端关闭这里我就当时写的感觉自己都觉得一眼错😀。如今也不能进行测试，我上面也只能谈一谈我觉得我现在觉得比较正确的思路。

现在实现的效果如下：


### Part7——进程文件系统

实现进程文件系统的大致思路也比较简单，在进程创建或者状态转换时同意调用一个写进程文件的函数更新一下进程当前的状态即可。

由于原先结构体中各个我们需要的进程的相关信息也都有了，对应创建文件夹、文件并将内容写入即可。

不过值得注意的是我们的0号进程并没有ppid，需要特殊处理一下，还有可能进程刚刚`alloc`，其`cwd`也为空，特殊处理一下即可。

再在`init_proc` `proc_alloc` `proc_addready`等地方调用该函数即可。

进程文件的删除？考虑到最多进程有限，直接将对应pid文件的状态设置为`UNUSED`其实也可以了。


文末是我在进行持久化大实现过程中回顾文件相关章节任务以辅助我完成上述内容。

## 虚拟文件系统

基于原先简陋磁盘文件实现虚拟文件系统

**系统打开文件表**记录系统打开的所有文件

`proc_t`中维持数组——文件描述符和用户打开文件表，其中下标表示用户描述符，下标对应项表示打开的文件

文件系统调用，能够支持常见的文件处理程序。

将文件中的内容输出到屏幕上怕不就是**甚至可以在自己写的操作系统里看轻小说**

## 磁盘文件系统

### 类UNIX磁盘文件系统

磁盘基本构造：
- 磁盘的第0扇区是MBR，第1~255扇区是操作系统内核kernel的ELF文件，再后面是用户程序。

逻辑块：
- 一个扇区是512字节，这个粒度对于我们要实现的磁盘文件系统来说还是太小了，所以我们把8个扇区看成一个逻辑块（block）一起管理，一个逻辑块的大小是4KiB。
- 组成：逻辑块编号的零点就是扇区编号的零点，也就是MBR和kernel占据前256÷8=32个逻辑块，即第0~31逻辑块，磁盘文件系统管理第32逻辑块开始的部分。

磁盘文件系统区域：
- 整个磁盘的大小为128MiB，即总共128MiB÷0.5KiB=262144个扇区，262144÷8=32768个逻辑块，用户文件部分占后32768-32=32736个逻辑块，也就是32736×4KiB=127.875MiB。

查看编译后的文件，发现`user.img`文件大小恰好为`127.875MiB`

整个磁盘的结构：
```text
        [ boot.img | kernel.img |                      user.img                      ]
        [   mbr    |   kernel   | super block | bit map | inode blocks | data blocks ]
sect    0          1          256           264       272            512        262144
block   0                      32            33        34             64         32768
```

磁盘文件系统(user.img)部分组成：4部分
- super block，位于32号逻辑块，主要存放文件系统的一些关键信息
- bitmap，位于33号逻辑块，通过位图的方式标记每一个逻辑块是否被使用（对应比特位为1即为被使用）bitmap占一个逻辑块，总共有4096×8=32768个比特，正好对应整个磁盘32768个逻辑块
- inode blocks，位于34~63号逻辑块（占30块），用于存储磁盘文件的inode 磁盘文件的inode主要存放这个文件的一些关键信息，比如文件多大，文件数据占用了哪几个block 一个inode的大小是64字节，因此一个逻辑块可以存放4096÷64=64个inode，总共可以存放64×30=1920个inode
- 第i个inode位于第34+(i/64)逻辑块的第(i%64)*64字节处，另外我们**约定第0个inode不会被使用**，同时inode为0表示目录项无效
- data blocks，位于64~32767号逻辑块，用于存储磁盘文件的数据

inode？
- 索引节点，意思是通过索引节点可以找到文件的内容
- 磁盘上的inode结构：
```c
typedef struct dinode {
  uint32_t type;
  uint32_t device;
  uint32_t size;
  uint32_t addrs[NDIRECT + 1];
} dinode_t;
```

**降低难度，只用实现这三种文件的类型(普通文件、目录、设备文件)即可，在必做任务中不必支持硬链接或软链接，因此磁盘上的inode中也不必包含引用计数信息。**

后续留坑，inode大小上文已知为64字节固定大小，type类型的大小压缩，毕竟只有三种类型，uint16_t完全够用，此外注意这里的引用计数，未来也要加以引进。

addrs就是文件地址，也即这个文件数据所占用的逻辑块的编号，其中前NDIRECT（12）项是直接索引，即数组对应项就是逻辑块的编号；最后一项是间接索引，在它指向的逻辑块中存放文件地址，一个逻辑块可以存放4096÷4=1024个地址，一个地址4字节32位。因此一个文件的数据最多使用12+1024=1036个逻辑块，也就是说在我们的磁盘文件系统支持的文件最大大小是1036×4KiB=4144KiB，大约为4.05MiB。

大文件？添加二级索引！

目录如何表示？目录项集合！目录项结构如下：

```c
#define MAX_NAME  (31 - sizeof(uint32_t))

typedef struct dirent {
  uint32_t inode;
  char name[MAX_NAME + 1];
} dirent_t;

```

一个目录项占32字节，前4字节表示这个文件的inode编号（为0表示该目录项无效），后28字节表示这个文件的名字。

目录是由目录项构成的文件，说得再直白一点就是目录文件以二进制形式存储struct dirent的数组，所以用户程序想要遍历文件夹时，只需要以只读权限打开目录文件并逐个读取struct dirent即可

和Linux一样，目录中需要包含名字为.和..的目录项，它们分别指向目录自身以及上一级目录（对于根目录，..也指向自身），我们约定这两个目录项总是位于目录文件的最前面。

### 磁盘文件系统的构造

实现一个打包文件为磁盘文件系统的工具`mkfs`——使用这个工具来构建给QEMU运行的磁盘中的用户文件部分，即第32~32767号逻辑块。

首先用open打开了目标文件（如不存在会创建，如已存在会清空内容），然后用ftruncate把文件的大小扩展到(32768-32)×4KiB=127.875MiB（使用全0填充），接着用mmap把文件的内容映射到内存，通过编辑内存编辑相关文件。

相关数据结构：
```c
typedef union {
  uint8_t  u8buf[BLK_SIZE];
  uint32_t u32buf[BLK_SIZE / 4];
} blk_t;
```
是一个联合体，你可以使用u8buf按字节操作也可以使用u32buf按4字节操作，`BLK_SIZE`为4096对应一个逻辑块4KiB，即4096字节。

```c
// super block，我们只是用它的前16字节存放数据，其实没什么大用，因为都是定值
typedef struct {
  uint32_t bitmap; // bitmap位于的逻辑块编号，33
  uint32_t istart; // inode blocks区开始的逻辑块编号，34
  uint32_t inum;   // 总共可以使用的inode总数，1920
  uint32_t root;   // 根目录的inode编号，1
} sb_t;

// inode在磁盘上的结构，和前面介绍的一致
typedef struct {
  uint32_t type;   // 文件类型
  uint32_t device; // 设备号（如果是设备文件）
  uint32_t size;   // 文件大小
  uint32_t addrs[NDIRECT + 1]; // 文件地址
} dinode_t;

// 目录项的结构，和前面介绍的一致
typedef struct {
  uint32_t inode; // 文件的inode编号
  char name[MAX_NAME + 1]; // 文件名
} dirent_t;

struct {blk_t blocks[IMG_BLK];} *img; // 指向目标文件映射到的内存地址
sb_t *sb; // 指向super block映射到的内存地址
blk_t *bitmap; // 指向bitmap映射到的内存地址
dinode_t *root; // 指向根目录的inode映射到的内存地址

```

**为了支持大文件，上述`inode`结构是否需要一起改变？结果感觉是显然的。**

img就是上面所提到的，我们所构造的目标文件映射到的内存地址，在设置好它后，我们调用init_disk进行一些基本的初始化，包括设置sb、bitmap指针指向其对应的位置。

同时调用ialloc给根目录申请一个inode(inode 1)并设置root指针，设置super block的一些数据，设置bitmap的前64比特为1（因为能被分配给用户文件数据使用的data blocks区从64号逻辑块开始），然后调用iappend给根目录添加.和..两个文件项


一些提供好的API，不过也只是该工具中可以使用？

```c
blk_t *bget(uint32_t no); // 返回编号为no的逻辑块映射到的内存地址
dinode_t *iget(uint32_t no); // 返回编号为no的inode映射到的内存地址
uint32_t balloc(); // 申请一个空闲的逻辑块，设置bitmap中它的对应位，返回其编号（可以看到是从64号依次申请）
uint32_t ialloc(int type); // 申请一个空闲的inode，设置其type，返回其编号（可以看到是从1号依次申请）
```

需要自己实现的函数？

```c
blk_t *iwalk(dinode_t *file, uint32_t blk_no);
```

返回file对应的文件数据使用的第blk_no个逻辑块被映射到的内存地址，如果不存在这个逻辑块，就申请一个
前面介绍过，inode中的addrs数组记录了这个文件使用的逻辑块，前NDIRECT（12）个是直接索引，最后一个是间接索引
如果blk_no比12小，说明这个逻辑块在直接索引里，直接取对应下标对应的逻辑块编号即可，不过如果是0就代表还没有索引，需要调用balloc申请一个逻辑块设置在inode里并返回
如果blk_no不小于12，那就说明在间接索引里，那就先把blk_no减去12，得到的就是在间接索引中的下标，然后打开间接索引使用的逻辑块（如果间接索引是0，和上面一样balloc一个并设置inode），然后找到里面的第blk_no个索引项（已经减过12了，因为索引都是4字节，因此用u32buf取下标比较方便），和前面一样，如果是0的话就balloc并设置再返回

**可以看出这里支持二级索引也需要修改**

```c
void iappend(dinode_t *file, const void *buf, uint32_t size);
```

给file对应的文件尾部附加buf开始的size字节数据

你可以一个逻辑块一个逻辑块的追加——当前文件大小除以BLK_SIZE（4096）可以得到要写的逻辑块在文件索引里是第几项，然后调用iwalk得到对应的逻辑块，当前文件大小对BLK_SIZE取模可以得到要写的字节的开始位于逻辑块的偏移量，因此这次最多能写size和BLK_SIZE-偏移量中的较小值，写的时候直接利用memcpy在buf和逻辑块直接复制即可，写完后将文件大小和buf向后移动本次写的字节数，size减去这么多字节——如此这么循环直至全部写完

往我们的磁盘文件系统之中写文件：
```c
// 往我们的磁盘文件系统之中添加文件，添加到磁盘的根目录之中
void add_file(char *path) {
  static uint8_t buf[BLK_SIZE];
  // 打开该文件
  FILE *fp = fopen(path, "rb");
  if (!fp) panic("file not exist");
  // alloc a inode，为该文件申请inode，类型全部都是普通文件？
  uint32_t inode_blk = ialloc(TYPE_FILE);
  dinode_t *inode = iget(inode_blk);
  // append dirent to root dir，为根目录添加目录项
  dirent_t dirent;
  dirent.inode = inode_blk;
  // 获取文件名最后一部分
  strcpy(dirent.name, basename(path));
  // 将该文件目录项追加到根目录之中
  iappend(root, &dirent, sizeof dirent);
  // write the file's data, first read it to buf then call iappend
  // TODO();
  size_t rdbytes;
  // 进行读取，将内容读取到buf之中，每个元素大小为1字节，一共读取BLK_SIZE个字节
  while ((rdbytes = fread(buf, 1, BLK_SIZE, fp)) > 0)
  {
    /* code */
    // 使用iappend将buf中读取到的内容写入我们的磁盘文件系统之中去
    iappend(inode, buf, rdbytes);
  }
  
  fclose(fp);
}
```

### 磁盘文件系统的解释

自己实现一个磁盘文件系统？

重写实现虚拟文件系统所用到的API，只要实现虚拟文件系统时没有打破抽象层，仅仅修改磁盘文件系统的实现也不会对其造成影响？

进程结构体之中的`cwd`，表示进程的“当前目录”？这样进程访问文件就是从“当前目录”开始，而不必每次从根目录开始。

inode在操作系统中的结构？
```c
// 注意和之前磁盘内的inode作区分，磁盘内的inode管理的是文件的信息，操作系统中的inode管理的是打开的文件的信息，因此除了磁盘中inode的信息，还需要包括一些别的信息，比如打开的是哪个文件（打开的文件的inode编号）以及打开的文件的引用计数等等
typedef struct inode inode_t;
struct inode {
  int no;
  int ref;
  int del;
  dinode_t dinode;
};
// 多出的三个成员
// no表示inode的编号
// ref表示inode的引用计数
// del表示删除记号
```

```c
// 操作系统可以打开的inode，称之为活动inode表
static inode_t inodes[INODE_NUM];
```

所以操作系统对磁盘文件系统的管理，**归根结底就是对操作系统中inode的管理**——**打开文件到活动inode表里，根据操作系统中的inode读写对应的文件**，为此，我们需要一些操作inode的API，不过在这些API之下，我们还需要一些直接操作磁盘的API（包括读写磁盘，管理磁盘inode和磁盘逻辑块）来帮助我们实现操作inode的API

已经实现的按逻辑块读写？
```c
// 读取no号逻辑块off偏移量处的size字节的内容
void bread(void *dst, uint32_t size, uint32_t no, uint32_t off);
// 写入no号逻辑块off偏移量处size字节的内容
void bwrite(const void *src, uint32_t size, uint32_t no, uint32_t off);
// 把no号逻辑块全部清空
void bzero(uint32_t no);
```

初始化磁盘文件系统时，我们会调用init_fs函数，在现在的类UNIX磁盘文件系统中，它会从磁盘中读取super block的信息并存在sb中。

与磁盘inode(binode_t)相关的API：
```c
// 读取第no个inode到内存中
void diread(dinode_t *di, uint32_t no);
// 把内容写入第no个inode
void diwrite(const dinode_t *di, uint32_t no);
// 申请一个磁盘inode并返回编号，不同于mkfs.c中的实现，因为在操作系统中磁盘inode是可以被回收的，因此你需要遍历所有inode（注意0号inode永不使用），找到一个空闲的（type是TYPE_NONE的），然后设置其type（其他成员我们约定在difree时已经清零），设置完后还要调用diwrite将其写回磁盘，最后返回其编号；不必考虑没有空闲磁盘inode的情况。
uint32_t dialloc(int type);
// 回收编号为no的磁盘inode，已经实现好了，其实实现也很简单，只要把这个inode的所有成员清零就行，因为TYPE_NONE就是0，故而可以内重新alloc
void difree(uint32_t no);
```

这里个人感觉wiki描述不是很准确，应该是把no号inode读到内存或者写no号inode(磁盘)，什么叫`读到内存或者写到内存`


与逻辑块申请释放相关的API：
```c
// 这个API负责申请一个空闲的逻辑块，将其清零，并返回其编号
// 不同于mkfs.c中的实现，因为在操作系统中逻辑块是可以被回收的，因此你需要遍历bitmap，找到一个为0的比特，可以看到我们现在是一次遍历32个比特，如果第i次的这32个比特不是全1，就找出其中一个为0的比特，假设是在第j位，那就说明第32*i+j号逻辑块是空闲的，调用bzero将其清零，然后将这个比特置为1再用bwrite写回磁盘；不必考虑没有空闲逻辑块的情况
uint32_t balloc();
// 负责回收第blkno号逻辑块，首先使用bread读取这个逻辑块在bitmap上对应的比特所在的字节，再将其对应的比特清零，再用bwrite写回，也就是简单将对应的bitmap写成0
void bfree(uint32_t blkno);
```


与操作系统之中活动inode表相关的API：操作系统中的inode，而非磁盘上面的
```c
// 负责在活动inode表中打开编号为no的inode，首先需要遍历活动inode表，看这个编号的inode是否已经打开，如是，只用增加它的引用计数并返回指向它的指针即可
// 如果没有，就在活动inode表中找一个空位（ref为0），初始化它——设置no为no，设置ref为1，设置del为0并调用diread读取磁盘inode部分，然后返回指向它的指针，不必考虑没有空位的情况
inode_t *iget(uint32_t no);
// 这个API负责将inode中的磁盘inode部分写回磁盘，每当修改系统inode中的磁盘inode部分时，都需要调用这个函数
void iupdate(inode_t *inode);
```


正式操作inode的API：
```c
// 从inode代表的文件的off偏移量处，读取len字节到内存的buf里，返回读取的字节数（或-1如果失败）
int iread(inode_t *inode, uint32_t off, void *buf, uint32_t len);
// 从内存的buf里，写len字节到inode代表的文件的off偏移量处，返回写入的字节数（或-1如果失败）
// 允许off+len超过写之前文件的大小，此时会更新文件新大小为off+len
int iwrite(inode_t *inode, uint32_t off, const void *buf, uint32_t len);
```

```c
// 用于解析路径，用途是返回指向path的下一级路径的指针，并将这一级的名字存到name中
const char* skipelem(const char *path, char *name);
// 创建目录时使用，用于给inode对应的文件（一定是目录）创建.和..的两个目录项
void idirinit(inode_t *inode, inode_t *parent);
// 用于遍历parent这个目录，找到其中名字为name的文件并打开，返回打开的inode
inode_t *ilookup(inode_t *parent, const char *name, uint32_t *off, int type);
// 用于打开path路径指向的文件所在的目录，然后将文件名（即path的最后一部分）记录在name中
inode_t *iopen_parent(const char *path, char *name);
// 用于打开path路径指向的文件本身，如果文件不存在且type非TYPE_NONE就以type创建该文件（要求沿途目录必须存在），否则返回NULL
inode_t *iopen(const char *path, int type);
// 读写磁盘文件所需要的API，返回inode对应的文件数据使用的第no个逻辑块的编号，如果不存在这个逻辑块，就申请一个，这里也与索引相关，需要注意
uint32_t iwalk(inode_t *inode, uint32_t no);
// 从inode代表的文件的off偏移量处，读取len字节到内存的buf里，返回读取的字节数
int iread(inode_t *inode, uint32_t off, void *buf, uint32_t len);
// 从内存的buf里，写len字节到inode代表的文件的off偏移量处，返回写入的字节数
int iwrite(inode_t *inode, uint32_t off, const void *buf, uint32_t len);
// 这个API清空inode代表的文件的所有数据，即bfree其使用的所有逻辑块（包括数据使用的逻辑块和间接索引使用的逻辑块），然后将其所有索引项清零，并且设置该文件的大小为0，注意这个函数修改了inode的磁盘部分，因此需要调用iupdate写回磁盘，也与索引相关
void itrunc(inode_t *inode);

inode_t *idup(inode_t *inode); // 自增inode的引用计数并返回自身
void iclose(inode_t *inode); // 自减inode的引用计数，如果为0且设置了del，清空该文件的内容并回收磁盘inode（删除文件）
uint32_t isize(inode_t *inode); // 返回inode代表的文件的大小
int itype(inode_t *inode); // 返回inode代表的文件的类型
uint32_t ino(inode_t *inode); // 返回inode代表的文件的inode标号
int idevid(inode_t *inode); // 如果inode代表的文件是设备文件，返回其设备号，否则返回-1
void iadddev(const char *name, int id); // 向文件系统中注册一个设备，名字为name，设备号为id

```

上述`iwalk` `itrunc`均与索引相关。


删除磁盘文件相关API：
```c
// 用于监测目录是否为空，要求inode必须是一个目录
// 目录的内容是一个个目录项，并且前两个目录项一定是.和..，因此目录为空等价于剩下的目录项均为无效目录项（inode为0）
int idirempty(inode_t *inode);
// 用于删除path路径指向的文件，成功返回0，失败返回-1
int iremove(const char *path);
```


### 磁盘文件系统和其他

系统调用：
```c
// 改变当前进程的cwd为path这一路径指向的目录，成功返回0，失败返回-1
int chdir(const char *path);
// 删除该文件？直接调用iremove即可
int unlink(const char *path);
```

