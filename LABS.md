# xv6 操作系统实验

本仓库的起始代码和评分脚本对应 MIT 6.1810 **2025** 版。仓库名包含 2026，但不能据此套用其他年份的实验题。各实验从自己的原始分支独立实现，没有把上一实验的代码合并进下一实验。

## 分支和实现

| 原分支 | 实现分支 | 实现内容 | 官方要求 |
| --- | --- | --- | --- |
| util | [edidada/util](https://github.com/edidada/xv6-2026/tree/edidada/util) | sleep、sixfive、memdump、递归 find 与 -exec | [util](https://pdos.csail.mit.edu/6.1810/2025/labs/util.html) |
| syscall | [edidada/syscall](https://github.com/edidada/xv6-2026/tree/edidada/syscall) | interpose 掩码、路径放行、fork 继承、教学内核残留页实验、GDB 观察 | [syscall](https://pdos.csail.mit.edu/6.1810/2025/labs/syscall.html) |
| pgtbl | [edidada/pgtbl](https://github.com/edidada/xv6-2026/tree/edidada/pgtbl) | 只读 PID 共享页、页表打印、2 MiB 大页与拆分回收 | [pgtbl](https://pdos.csail.mit.edu/6.1810/2025/labs/pgtbl.html) |
| traps | [edidada/traps](https://github.com/edidada/xv6-2026/tree/edidada/traps) | 栈回溯、用户 alarm、完整寄存器恢复、防止处理函数重入 | [traps](https://pdos.csail.mit.edu/6.1810/2025/labs/traps.html) |
| cow | [edidada/cow](https://github.com/edidada/xv6-2026/tree/edidada/cow) | 写时复制 fork、物理页引用计数、写故障和 copyout | [cow](https://pdos.csail.mit.edu/6.1810/2025/labs/cow.html) |
| net | [edidada/net](https://github.com/edidada/xv6-2026/tree/edidada/net) | E1000 收发环、UDP 接收、端口绑定、每端口 16 包队列 | [net](https://pdos.csail.mit.edu/6.1810/2025/labs/net.html) |
| lock | [edidada/lock](https://github.com/edidada/xv6-2026/tree/edidada/lock) | 每 CPU 空闲页链表、批量窃取、写者优先的读写自旋锁 | [lock](https://pdos.csail.mit.edu/6.1810/2025/labs/lock.html) |
| fs | [edidada/fs](https://github.com/edidada/xv6-2026/tree/edidada/fs) | 双重间接块、65803 块文件、符号链接与 O_NOFOLLOW | [fs](https://pdos.csail.mit.edu/6.1810/2025/labs/fs.html) |
| mmap | [edidada/mmap](https://github.com/edidada/xv6-2026/tree/edidada/mmap) | 延迟文件映射、共享脏页写回、部分解除映射、fork/exit/exec 生命周期 | [mmap](https://pdos.csail.mit.edu/6.1810/2025/labs/mmap.html) |

`riscv` 是基础内核分支，没有独立的实验评分脚本。原始实验分支保留不动。切换实现分支，例如：

```sh
git fetch origin
git switch edidada/cow
```

## 设计要点

- **用户程序**：sixfive 按题目定义的分隔符识别完整数字，输出时去掉多余前导零；memdump 使用字节复制读取字段，避免未对齐访问；find 跳过 `.`、`..`，检查路径和参数数量，逐个等待外部命令完成。
- **系统调用与陷阱**：sandbox 状态属于进程，fork 时复制，进程槽复用时重置。alarm 保存整个 trapframe，sigreturn 的返回值保留原 a0。问答文件包含远程 GDB 和反汇编证据。
- **内存**：PID 页只给用户读权限。大页按 2 MiB 对齐，部分释放时转成 4 KiB 页；保留页内容不变。COW 只把原本可写的页标记为可复制，真正只读的代码页仍然禁止写入。
- **网络与并发**：网卡接收缓冲区先补充再交给协议层，避免再次 DMA 覆盖已交付的数据。UDP 丢包和接收完成都会释放缓冲区。每 CPU 分配器在禁止中断期间确定 CPU，将小型热缓存和批量空闲页池分别加锁，优先从批量池补充，内存紧张时仍可回收其他 CPU 的热缓存；读写锁原子记录读者、写者意图和活动写者，读者加入后再次检查写者意图，写者等已有读者离开后再进入。
- **持久化**：文件 inode 保持原来大小，改用 11 个直接块、1 个单重间接块、1 个双重间接块。mmap 保持文件引用，缺页时读文件；共享映射按脏位写回，每次事务限制为一个文件系统块，解除映射不扩展原文件长度。

## 验证方法

本地只做代码修改和静态差异检查，**没有本地编译**。每个实现分支均有 `.github/workflows/lab.yml`，由推送触发，在 Ubuntu 24.04 安装 RISC-V GCC、QEMU 和 GDB 后运行原始 `make grade`。保留官方测试及评分要求，不通过修改测试降低门槛。

```sh
gh run list --branch edidada/cow
gh run view RUN_ID --log
gh run view RUN_ID --log-failed
gh run download RUN_ID -n lab-results
```

评分产物包含 QEMU 输出；traps 另外保留 `call.asm` 和 `call.sym`；syscall 另外保留正常系统调用与空指针故障的 GDB 记录；pgtbl 另外保留页表测试和 usertests 的独立输出，并显式检查 `ALL TESTS PASSED`。

完整验收以每个分支**最新提交**对应的已完成 Actions 为准。排队或运行中不代表通过；旧提交通过也不能代替新提交的验证。实验范围为官方必做题，未宣称完成页面列出的可选挑战，也没有向 Gradescope 提交。
