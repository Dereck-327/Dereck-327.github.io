
# kernel 调试及优化

## 启动分析

### 内核启动过程分析 (Kernel Boot Analysis)

```bash
# 可读时间
dmesg -T
```

#### bootgraph (内核自带绘图工具)

```bash
# Linux 内核源码树中提供了一个脚本 scripts/bootgraph.pl。

# 捕获数据： 
dmesg > boot.log

# 生成 SVG： 
perl scripts/bootgraph.pl boot.log > boot.svg
# 这会生成一张矢量图，直观展示哪些内核函数（如 pci_init, ext4_init）执行时间最长。
```

### 用户态服务启动分析 (Systemd Analysis)

一旦内核完成初始化并启动 init 进程（通常是 systemd），性能分析的重心就转向了服务加载。systemd 自带了极其强大的分析工具族  

```bash
# 总体耗时概览
systemd-analyze
# 服务耗时排行榜 (Blame) 它会按耗时降序排列所有单元。注意：耗时长不代表它阻塞了启动，因为它可能是异步运行的。
systemd-analyze blame

```

#### 关键路径分析 (Critical-chain)

```bash
# 找出那些真正导致启动变慢的服务链：带有 @ 符号的时间点表示该服务启动时的时刻。
systemd-analyze critical-chain
```

#### 启动流程可视化图表

```bash
systemd-analyze plot > startup.svg
```
