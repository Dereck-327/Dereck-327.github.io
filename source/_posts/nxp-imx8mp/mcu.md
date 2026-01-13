
# IMX8MP-EVK MCU

## uboot启动M7

进入uboot后 查看mmc设备

```bash
mmc list
```

看到如下

```txt
FSL_SDHC: 1  # SD
FSL_SDHC: 2 (eMMC)
```

再查看emmc 第一个分区的内容，会有m7的程序

```bash
fatls mmc 2:1
```

直接运行

1. 对于编译出来的运行在 TCM上的固件

TCM (Tightly Coupled Memory) - 紧耦合存储器  
位置：通常位于核心处理器芯片内部或非常靠近核心的地方（片上内存，OCRAM/SRAM）。  
速度：极快，通常与 CPU 核心时钟同步，访问速度接近 L1 Cache，无延迟。  
用途：主要用于存储 关键代码（如实时操作系统 RTOS 内核、中断处理程序、设备驱动程序）和 实时数据。在异构系统（如 i.MX M7 核心）中，M7 的代码通常被复制到 TCM 中执行。  
大小：较小（通常为几十 KB 到几 MB）。  
特性：不可缓存。处理器直接访问它，因此访问是可预测的，非常适合 实时 应用。

```bash
fatload mmc 2:1 0x48000000 mcore-demos/imx8mp_m7_DDR_hello_world.bin;cp.b 0x48000000 0x7e0000 20000;
bootaux 0x7e0000 # 执行固件的命令
```

他的步骤是将2:1分区上的固件加载到DDR上，再从DDR临时内存上拷贝到M7核心实际运行的内存（cp.b 表示按字节复制）

2. 对于运行在DRAM上的固件

```bash
fatload mmc 2:1 0x80000000 hello_world.bin
dcache flush # 确保数据刷新了
bootaux 0x80000000
```

3. 对于运行在flash上的固件

```bash
sf probe # 初始化 SPI Flash
sf read 0x80000000 0 4 # 读取 Flash 头部  从 SPI Flash 读取数据到 DDR 内存进行检查。0x80000000: DDR 目标地址。0: SPI Flash 起始地址 (偏移量 0)。4: 读取长度（4 字节）。通常用于读取 Flash 上的魔数或 ID，确认内容。
fatload mmc 2:1 0x80000000 flash.bin
dcache flush 
sf erase 0 0x100000 # 擦除目标区域 0: 擦除的起始偏移量  0x100000: 擦除的长度，等于 1 MB。Flash 必须先擦除才能写入。
sf write 0x48000000 0 0x100000 # 写入固件 0x48000000: DDR 源地址。 0: SPI Flash 目标偏移量 0; 0x100000: 写入长度
bootaux 0x8000000
```

**提示**：如果os和M7同时运行，需要设置特殊的设备树，在uboot设置 setenv fdtfile 'imx8mp-evk-rpmsg.dtb'

## linux启动M7

使用的是Linux Remote Processor (rproc)模块, 系统的设备树得用*-rpmsg.dtb  
kernel中是默认将`imx_rpmsg_tty`以模块加载进去了  
设备树也不需要改  

1. 在uboot中启动M核

```bash
# 准备和初始化协核 (Cortex-M)
run prepare_mcore
boot
```

2. 进入系统后设置并启动m核程序

```bash
# 启动虚拟串口 （查看消息 nxp官方的demo可以用这个虚拟串口来查看）
insmod /usr/lib/modules/6.12.34-lts-next-gbe78e49cb433/kernel/drivers/rpmsg/imx_rpmsg_tty.ko

# 将需要启动的程序放到 lib/firware 下 （也可以在其他路径 但是需要改默认固件默认路径，如下）
echo -n <firmware_path> > /sys/module/firmware_class/parameters/path

# 将需要启动的程序写进配置 一般N=0
echo -n <firmware_name.elf> > /sys/class/remoteproc/remoteproc<N>/firmware

# 启动程序
echo start > /sys/class/remoteproc/remoteproc<N>/state

# 关闭程序

echo stop > /sys/class/remoteproc/remoteproc<N>/state

```





