---
title: petalinux
tags: [os]
categories: [linux]
description: petalinux system
date: 2025-05-12 10:55:41
top_img: /image/银狼.jpg
cover: /image/银狼.jpg
---



# 常用指令

1 source /opt/pkg/petalinux/settings.sh
2 cd workspace/petalinux/
3 petalinux-create -t project --template zynqMP -n lark
4 cd lark
5 petalinux-config --get-hw-description ../../xsa_files/second/
6 petalinux-config -c kernel
7 petalinux-config -c rootfs
8 petalinux-build
9 petalinux-package --boot --fsbl --fpga --u-boot --force
10 petalinux 2022.2
 kernel源码：/home/lifeng/workspace/petalinux/lark/build/tmp/work-shared/zynqmp-generic/kernel-source
 编译过程产生的目录：home/lifeng/workspace/petalinux/lark/build/tmp/work/zynqmp_generic-xilinx-linux/linux-xlnx/5.15.36+gitAUTOINC+19984dd147-r0/linux-zynqmp_generic-standard-build
 编译结果目录：home/lifeng/workspace/petalinux/lark/build/tmp/work/zynqmp_generic-xilinx-linux/linux-xlnx/5.15.36+gitAUTOINC+19984dd147-r0/image

11 petalinux 2022.2
 uboot源码:/home/lifeng/workspace/petalinux/lark/build/tmp/work/zynqmp_generic-xilinx-linux/u-boot-xlnx/v2021.01-xilinx-v2022.2+gitAUTOINC+b31476685d-r0/git
 编译过程目录：/home/lifeng/workspace/petalinux/lark/build/tmp/work/zynqmp_generic-xilinx-linux/u-boot-xlnx/v2021.01-xilinx-v2022.2+gitAUTOINC+b31476685d-r0/build
 编译结果目录：/home/lifeng/workspace/petalinux/lark/build/tmp/work/zynqmp_generic-xilinx-linux/u-boot-xlnx/v2021.01-xilinx-v2022.2+gitAUTOINC+b31476685d-r0/image

12 rootfs目录：
/home/lifeng/workspace/petalinux/lark/images/linux/rootfs.tar.gz,解压以后，可以

13 清理编译：
petalinux-build -x mrproper,删除tmp文件，images/，build/， components/plnx_workspace
petalinux-build -x cleanall,删除tmp和sstate

14 拷贝源码
petalinux-devtool modify linux-xlnx
petalinux-devtool modify u-boot-xlnx
这样uboot和linux源码就会拷贝到components/yocto/workspace/sources/目录下。
注意：
14.1 只有成功编译petalinux以后，执行上面2条命令才有效;
14.2 修改上面2个目录下的源代码，编译petalinux，修改就会生效；

15 设备本地编译：
petalinux-config-->yocto settings-->add pre-mirror url-->回车，输入下面内容，也就是download所在目录：
file:///home/lifeng/codes/share_files/2022.2/downloads
petalinux-config-->yocto settings-->local sstate feeds settings-->回车，输入下面内容，也就是aarch64所在目录：
/home/lifeng/codes/share_files/2022.2/aarch64

16 安装nfs服务器
1 sudo apt-get install nfs-kernel-server,安装nfs服务器
2 sudo apt-get install nfs-common  安装nfs客户端
3 sudo gedit /etc/exports，在最下面加入这么一行：
/home/lifeng/nfs *(rw,sync,no_root_squash)
4 在家目录新建nfs目录：
mkdir -p /home/lifeng/nfs
chmod 777 /home/lifeng/nfs
5 启动nfs服务：
sudo /etc/init.d/nfs-kernel-server restart

17 配置system-user.dtsi,目录/project-spec/meta-user/recipes-bsp/device-tree/files/
/include/"system-conf.dtsi"
/ {
 amba {
  serial@ff010000{
   status = "disabled";
  };
 };
 aliases {
  /delete-property/ serial1;
  /delete-property/ pd_uart1;
 };
};
/delete-node/ &uart1;

&gem3 {
 status = "okay";
 compatible = "cdns,zynq-gem", "cdns,gem";
 phy-mode = "rgmii-id";

 fixed-link {
  speed = <1000>;
  full-duplex;
  pause;
  asym-pause;
 };
};

18 tftp网络设置
pc本机设置192.168.1.113，virtualbox手动设置ipv4 ip地址192.168.1.240,主机和ubuntu虚拟机
互相可以ping通。
设备ping 240，现实alive，表示设备与虚拟机通信成功；一旦成功以后，出现不成功的情况，请重启虚拟机；

19 直接下载image.ub,不行，启动不了。
tftpboot 0x200000 Image //190M
tftpboot 0x10000000 rootfs.cpio.gz.u-boot //59M
tftpboot 0x1000 system.dtb
booti 0x200000 0x10000000 0x1000

2024111030
10:00
1 petalinux-config，只设置initranfs和fpga manager，然后petalinux-build；
2 petalinux-package --boot --fsbl zynqmp_fsbl.elf --fpga --u-boot --force生成boot.bin
3 通过vitis，zynqmp_fsbl.elf和boot.bin烧录设备；
4
tftpboot 0x200000 Image //190M
tftpboot 0x10000000 rootfs.cpio.gz.u-boot //59M
tftpboot 0x1000 system.dtb
booti 0x200000 0x10000000 0x1000
5 可以成功启动设备；

20241109
1 petalinux-create -t project --template zynqMP -n lark
2 cd lark
3 petalinux-config --get-hw-description ../../xsa
4 petalinux-config-->yocto settings-->add pre-mirror url-->回车，输入下面内容，也就是download所在目录：
file:///home/lifeng/codes/share_files/2022.2/downloads
5 petalinux-config-->yocto settings-->local sstate feeds settings-->回车，输入下面内容，也就是aarch64所在目录：
/home/lifeng/codes/share_files/2022.2/aarch64
6 设置文件系统initram类型，然后保存退出；
7 petalinux-build编译，一定要先编译一次，否则很麻烦；
8 cd image/linux，执行petalinux-package --boot --fsbl zynqmp_fsbl.elf --fpga --u-boot --force；
9 拷贝Image，rootfs.cpio.gz.u-boot， system.dtb到tftp下载目录；
10 设备上电（已经烧录uboot），到uboot数秒按下任意键盘按键；
11 setenv ipaddr xxx.xxx.1.xxx, setenv serverip xxx.xxx.1.xxx；
12 ping serverip,显示host serverip is alive；
13 分别执行下面命令：
tftpboot 0x200000 Image
tftpboot 0x10000000 rootfs.cpio.gz.u-boot //59M
tftpboot 0x1000 system.dtb
booti 0x200000 0x10000000 0x1000
14 输入用户名：petalinux，设置2次密码进入系统；
到了这里，整个系统已经运行起来，只不过文件系统是内存类型，也就是我们作各种实验的环境；

15 到工程一级目录，执行petalinux-create -t modules --name fpgaversion --enable，新建一个驱动并且在rootfs中选中；
16 修改hawaii/project-spec/meta-user/recipes-modules/fpgaversion/files/fpgaversion.c驱动源文件；
17 回到hawaii目录执行petalinux-build编译，出错的话，向上翻，找到.c文件出错的地方，修改，继续编译，直到没有错误完全通过；
18 执行步骤13和14
19 查找新添加的ko文件在哪，find ./ -name 模块名字，我的是在lib/modules/5.15.36-xilinx-v2022.2/extra目录下
20 sudo chmod 755 模块名字，sudo insmod 模块名字,这时候成功的话，应该打印出一些驱动里的输出，这里顺便提以下，现在的内核printk函数，有输出级别的选项，我都是加个err级别的，如：printk(KERN_ERR"hello world\n");否则不会输出到终端；
21 查看/dev/目录下是否有新建的设备文件，还可以通过cat /proc/devices查看被系统使用的设备，如果正常的话，/dev目录下有设备，cat /proc/devices也能看到设备；
22 编译应用程序，首先source /opt/Xilinx/Vitis/2022.2/settings64.sh
23 aarch64-linux-gnu-gcc test.c -o test，注意不要使用arm-linux-gnueabihf-gcc test.c -o test，这里一定要注意，我在这里耽搁时间了，否则你下载到设备上，运行提示：no file or directory,如果open失败，则可能是设备文件需要权限，sudo chmod 755 dev/xxxx
24 设备上电以后，ifconfig查看网络是否起来，没有的话：sudo ifconfig eth0 192.168.1.xxx netmask 255.255.255.0 up
25 tftp -g -r test 192.168.1.112(启动tftp服务器的ip)
26 sync
27 sudo chmod 755 test
28 ./test,如果应用报错open dev/xxx 失败，那就 sudo chmod 755 /dev/xxx,给硬件设备文件权限
29 然后就没了。

## 设备树

```dtsi
[label:] node-name[@unit-address] {
  [properties definitions]
  [child nodes]
};
```

1. unit-address 一般是指代设备地址或寄存器首地址。如果设备没有设备地址或寄存器就不用写。
2. [child nodes]是这个挂在在这个设备节点上的子节点。

## compatible属性

compatible 属性也叫兼容性，这个属性在我们后面讲平台设备是才会用到，现在可以简单了解一下。他的值是字符串列表，是平台设备框架中设备和驱动关联的关键，
在驱动代码中定义一个 of 匹配表来和 compatible 属性对应。

## pinctrl 子系统 和 gpio (读写寄存器)

pinctrl_uart1_default 节点。这个节
点下面没有属性，而是有 4 个子节点：mux、conf、conf-rx、conf-tx。其中 mux 是配置 pin 脚的
复用功能，其他三个是配置 pin 脚的电气特性，conf 中设置通用的特性，conf-rx 中设置输入引
脚和相关特性，conf-tx 中设置输出引脚和相关特性。

## 并发

### 原子操作

```c
typedef struct {
   int counter;
} atomic_t;
```

### 自旋锁
>
> 自旋锁的一个特点就是自旋（忙等待），会使得等待锁的线程持续占用 CPU 资源
某个线程要访问共享资源时，需要先获取相应的锁即上锁，只要不释放
这个锁即解锁，别的线程就无法获取锁也就无法访问共享资源。此时需要却没有获取到自旋锁
的线程就会一直处于等带状态(阻塞)。

### 信号量

与自旋锁相比，信号量有两个优势：

1. 信号量可以让等待信号量的线程进入休眠，减少 CPU 的占用；
2. 信号量支持对多个线程同时访问共享资源。
信号量结构体定义如下：

```c
struct semaphore {
  raw_spinlock_t lock;
  unsigned int count;
  struct list_head wait_list;
};
```

count 指信号量支持同时访问共享资源的线程数。

## 计时器

### 超时回绕

把数据无符号数强转成有符号数巧妙的规避了回绕的问题，不用传统的比较运算符去比较，而使用下面几个宏来代替：

```c
#define time_after(unknown,known) ((long)(known) - (long)(unknown)<0)
#define time_before(unkonwn,known) ((long)(unknown) - (long)(known)<0)
#define time_after_eq(unknown,known) ((long)(unknown) - (long)(known)>=0)
#define time_before_eq(unknown,known) ((long)(known) - (long)(unknown)>=0)

```

know 为设定的时间
