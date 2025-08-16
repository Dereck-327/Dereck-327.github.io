---
title: OS System        # 标题
data: 2025-05-12 10:44:27
tags: [os]             # 标签
categories: [linux]              # 分类
description: os system  # 描述
top_img: /image/jizi.png # 顶部背景图
cover: /image/动漫少女.jpg   # 文章封面
---
- [linux常用指令](#linux常用指令)
  - [systemctl](#systemctl)
- [petalinux 系统制作](#petalinux-系统制作)
  - [系统引导和文件系统的制作](#系统引导和文件系统的制作)
  - [initram临时系统文件制作](#initram临时系统文件制作)
  - [ext4文件系统制作](#ext4文件系统制作)
    - [编译结果的文件解释](#编译结果的文件解释)
      - [BOOT.bin](#bootbin)
      - [image.ub](#imageub)
      - [system.bit(bitstream)](#systembitbitstream)
      - [rootfs\_cpio.tar.gz](#rootfs_cpiotargz)
      - [zynqmp\_fsbl.elf](#zynqmp_fsblelf)
    - [u-boot](#u-boot)
    - [SysMonPSU](#sysmonpsu)
      - [pgrep 获取指定进程信息](#pgrep-获取指定进程信息)
      - [驱动创建](#驱动创建)
      - [应用创建](#应用创建)
    - [gdb 远程调试](#gdb-远程调试)
- [create modules 驱动](#create-modules-驱动)
  - [linuxptp](#linuxptp)
    - [内核配置](#内核配置)
    - [rootfs设置](#rootfs设置)
  - [设置自动登录](#设置自动登录)
- [PetaLinux patch生成和应用方法整理](#petalinux-patch生成和应用方法整理)
  - [patch](#patch)
    - [2021.1及以后的版本](#20211及以后的版本)
  - [petalinux-build 清理](#petalinux-build-清理)
    - [清理所有构建输出和缓存](#清理所有构建输出和缓存)
    - [或者使用](#或者使用)
    - [清理特定组件](#清理特定组件)
    - [清理内核](#清理内核)
    - [清理 u-boot](#清理-u-boot)
    - [清理根文件系统](#清理根文件系统)
    - [清理设备树](#清理设备树)

## 自启动 使用 systemmd 服务

- 在petalinux或yocto构建的系统中
.service 文件定义服务的启动、停止、依赖关系、运行环境等
.bb (BitBake) 配方文件，用于定义如何获取、编译、安装一个软件包，并将其集成到镜像中

- 自启动的流程
BitBake 构建系统 → 根据 .bb 文件安装服务 → 镜像打包 → 系统启动 → systemd 加载服务 → 按依赖顺序启动服务
- 详细流程

1. BitBake 构建阶段
通过 .bb 文件中的 do_install 将可执行程序和服务文件安装到临时目录（${D}）。
FILES 变量声明哪些文件需要被打包到最终的根文件系统镜像（如 hawaii_algorithm 和 hawaii_algorithm.service）。

2. 镜像生成阶段
所有通过 FILES 声明的文件被包含在镜像中，例如：
/usr/bin/hawaii_algorithm
/lib/systemd/system/hawaii_algorithm.service

3. 系统启动阶段
Linux 内核启动后，由 systemd 作为初始化系统（PID=1）接管。
systemd 读取 /lib/systemd/system/ 中的服务文件，并根据 WantedBy 配置将服务链接到对应的目标（如 multi-user.target）。
根据依赖关系（After= 和 Requires=）按顺序启动服务。

4. 服务激活过程
当系统进入 multi-user.target 时，systemd 启动所有与该目标关联的服务。
服务按依赖顺序启动：例如 network.target 先于 hawaii_algorithm.service 启动。

### create app

1. petalinux-create -t apps --template install --name service_name --enable 创建一个app服务

### file path

1. 到项目的 project-spec/meta-user/recipes-apps/service_name/files 目录下 创建想要添加的自启动 .service文件(自动生成的 service_name 文件可以不用管)

### 服务编写

1. 编写 .service 文件，以autostart为例
需要启动两个服务 一个是算法解析模块 一个是应用模块
这里将我的可执行程序拷贝到了files目录下 以便.bb文件 生成配方把文件放到指定的目录

> 算法模块

```service
[Unit]
Description=Hawaii Algorithm Service  
After=network-online.target  
After=serial-getty@ttyPS0.service  
Requires=network-online.target      
Requires=serial-getty@ttyPS0.service  
        

[Service]
Type=simple
ExecStart=/petalinux/hawaii_algorithm  
Restart=on-failure
User=root
StandardOutput=journal    
StandardError=journal 

# 如果目录不存在，启动前自动创建
ExecStartPre=/bin/mkdir -p /petalinux
ExecStartPre=/bin/mkdir -p /petalinux/log/ps
ExecStartPre=/bin/mkdir -p /petalinux/log/algorithm
ExecStartPre=/bin/chmod 755 /petalinux
ExecStartPre=/bin/chmod 644 /petalinux/log/ps
ExecStartPre=/bin/chmod 644 /petalinux/log/algorithm

[Install]
WantedBy=multi-user.target
```

> 应用模块

```service
[Unit]
Description=Hawaii autostart service
After=hawaii_algorithm.service  
Requires=hawaii_algorithm.service  
After=serial-getty@ttyPS0.service     
Requires=serial-getty@ttyPS0.service  
After=multi-user.target   

[Service]
Type=simple
# ExecStart=/etc/init.d/autostart start  
# 或直接执行命令（推荐）：
ExecStart=/petalinux/hawaii 
Restart=on-failure
User=root
StandardOutput=journal
StandardError=journal


[Install]
WantedBy=multi-user.target  # 设置启动目标
```

### 配方编写

4. 编写.bb(BitBake) 文件

```bb
#
# This file is the autostart recipe. 
#

SUMMARY = "Simple autostart application" 
# SECTION = "PETALINUX/apps" 
LICENSE = "MIT" 
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302" 


SRC_URI = "file://hawaii_algorithm \
           file://hawaii \
           file://hawaii_algorithm.service \
           file://hawaii.service \
            file://zlog.conf"

# 继承 systemd 类以支持服务管理
inherit systemd

# 启用 systemd 服务
SYSTEMD_SERVICE:${PN} = "hawaii_algorithm.service hawaii.service"

SYSTEMD_AUTO_ENABLE:${PN} = "disable"

S = "${WORKDIR}"

do_install() {
    # 创建目录
    install -d -m 0644 ${D}/petalinux

    # 安装可执行程序到 /petalinux
    install -m 0755 ${S}/hawaii_algorithm ${D}/petalinux/
    install -m 0755 ${S}/hawaii ${D}/petalinux/
    install -m 0644 ${S}/zlog.conf ${D}/petalinux/

    # 安装 systemd 服务文件到 /lib/systemd/system
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/hawaii_algorithm.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${S}/hawaii.service ${D}${systemd_system_unitdir}/
}

# 定义安装后的文件路径（关键！）
FILES:${PN} += " \
    ${systemd_system_unitdir}/hawaii.service \
    ${systemd_system_unitdir}/hawaii_algorithm.service \
    /petalinux/hawaii \
    /petalinux/hawaii_algorithm \
    /petalinux/zlog.conf \
"
```

# linux常用指令

## systemctl

1. 查看活跃的服务 : systemctl list-units --type=service --state=running
2. 查看指定服务状态: systemctl status ***.service
3. 启动关闭重启: systemctl start servicename ; systemctl stop <service_name>; systemctl restart <service_name>
4. 获取服务日志: sudo journalctl -u hawaii.service -xe --no-pager
5. journal查看指定日志的输出 : journalctl -u hawaii.service

# petalinux 系统制作

## 系统引导和文件系统的制作

zynqmp板子需要用BOOT.BIN 和 *_fsbl.elf 来启动, 需要先编一个ext4的文件系统再打包成BOOT.BIN

1. **petalinux-create -t project --template zynqMP -n hawaii**
cd hawaii
petalinux-config --get-hw-description ../xsa

2. **petalinux-config**  

  > 配置yocto 配置离线编译 注意download路径需要再前面添加file://  
  > 设置文件系统类型为initram  

  ```bash
  petalinux-config-->yocto settings-->add pre-mirror url-->回车，输入下面内容，也就是download所在目录：
  file:///home/hjk/peta-localfiles/downloads
  petalinux-config-->yocto settings-->local sstate feeds settings-->回车，输入下面内容，也就是aarch64所在目录：
  /home/hjk/peta-localfiles/aarch64
  ```

  设置自动登录 : petalinux-config -c rootfs → Image Features → auto login
  
3. **需要配置一下网络**
    1. petalinux-config -c u-boot  
    -->boot options-->enable boot arguments->earlycon console=ttyPS0,115200n8 mem=2G@0x0 root=/dev/mmcblk0p2 rw rootwait rootfstype=ext4 cpuidle.off=1 cma=192M@0x10000000  
    > 这里的0x10000000就是某一个hp口在vivado里分配的数据空间.
    2. 返回-->选择enable a default value for bootcmd，输入内容：
    bootm 20000000（这是16进制）
    3. -->选择enable preboot，输入内容：
    run scsi_init;usb start;mdio write 0 0x1f 0x4000;sleep 1;mdio write 0 0x0D 0x001F;mdio write 0 0x0E 0x0573;mdio write 0 0x0D 0x401F;mdio write 0 0x0E 0x0001;mdio write 0 0x0D 0x001F;mdio write 0 0x0E 0x056a;mdio write 0 0x0D 0x401F;mdio write 0 0x0E 0x5f41;mdio write 0 0x0D 0x001F;mdio write 0 0x0E 0x0602;mdio write 0 0x0D 0x401F;mdio write 0 0x0E 0x0003;fatload mmc 0:1 20000000 image.ub;fatload mmc 0:1 10000000 system.bit;fpga loadb 0 10000000 100;
4. petalinux-build 编译一下
5. petalinux-package --boot --u-boot --force 生成boot.bin，将生成的BOOT.BIN  和 fsbl.elf 烧写到板子上
6. 启动卡进u-boot 设置ip (1 2 可视情况使用)
    1. env default -a回复uboot默认参数
    2. 重启进入u-boot (回复默认环境变量之后需要重启)
    2. setenv ipaddr 192.168.1.10
    3. setenv serverip 192.168.1.112
    4. setenv tftp "tftpboot 10000000 system.bit;fpga loadb 0 10000000 1000;tftpboot 0x200000 Image;tftpboot 0x10000000 rootfs.cpio.gz.u-boot;tftpboot 0x1000 system.dtb;booti 0x200000 0x10000000 0x1000"

> 注意这里可以在ui界面里复制不了copy来的内容 可以去 meta-user/recipes-bsp/u-boot/files 下修改cfg文件 添加对应的配置

``` cfg
CONFIG_USE_BOOTARGS=y
CONFIG_BOOTARGS="earlycon console=ttyPS0,115200n8 mem=2G@0x0 root=/dev/mmcblk0p2 rw rootwait rootfstype=ext4 cpuidle.off=1 cma=192M@0x10000000"
CONFIG_BOOTCOMMAND="bootm 20000000"

CONFIG_PREBOOT="run scsi_init;usb start;mdio write 0 0x1f 0x4000;sleep 1;mdio write 0 0x0D 0x001F;mdio write 0 0x0E 0x0573;mdio write 0 0x0D 0x401F;mdio write 0 0x0E 0x0001;mdio write 0 0x0D 0x001F;mdio write 0 0x0E 0x056a;mdio write 0 0x0D 0x401F;mdio write 0 0x0E 0x5f41;mdio write 0 0x0D 0x001F;mdio write 0 0x0E 0x0602;mdio write 0 0x0D 0x401F;mdio write 0 0x0E 0x0003;fatload mmc 0:1 20000000 image.ub;fatload mmc 0:1 10000000 system.bit;fpga loadb 0 10000000 100;"
```

在同级目录下的 platform-top.h 头文件记录了一些u-boot下的环境变量，可以提前在这里设置好

```h
#include <configs/xilinx_zynqmp.h>

#define CONFIG_EXTRA_ENV_SETTINGS \
    "ipaddr=192.168.1.10\0" \
    "serverip=192.168.1.111\0" \
    "tftp=tftpboot 10000000 system.bit;fpga loadb 0 10000000 1000;tftpboot 0x200000 Image;tftpboot 0x10000000 rootfs.cpio.gz.u-boot;tftpboot 0x1000 system.dtb;booti 0x200000 0x10000000 0x1000\0"\

```

## initram临时系统文件制作

1. petalinux-config

> 配置yocto 配置离线编译 注意download路径需要再前面添加file://
> 设置文件系统类型为initram
petalinux-config-->yocto settings-->add pre-mirror url-->回车，输入下面内容，也就是download所在目录：
file:///home/lifeng/codes/share_files/2022.2/downloads

petalinux-config-->yocto settings-->local sstate feeds settings-->回车，输入下面内容，也就是aarch64所在目录：
/home/lifeng/codes/share_files/2022.2/aarch64

3. petalinux-build 编译一遍
4. petalinux-package --boot --u-boot --force 打包
5. 将编译完的images/linux目录下的Image rootfs.cpio.gz.u-boot 和 system.dtb 拷贝到tftp目录下
6. 进入u-boot 运行 run tftp
7. 后续见 [ext4文件系统](#ext4文件系统制作)

## ext4文件系统制作

1. petalinux-config 修改文件系统类型为ext4
2. petalinux-build 编译
3. 登录临时文件系统后， 制作硬盘分区并同步文件(已经有分区了就不需要再做了)
    第一个fat32，第二个ext4：
    1. 取消分区1的挂载 sudo umount /dev/mmcblk0p1
2. sudo fdisk /dev/mmcblk0-->n-->p-->1-->+1024M-->t-->b-->n-->p-->2-->回车-->回车-->p-->w (根据提示做就行，实在不行就删除了再做)
3. sudo mkfs.vfat -F 32 /dev/mmcblk0p1
4. sudo mkfs.ext4 /dev/mmcblk0p2

4. 下载image.ub和system.bit到mmcblk0p1：
    mkdir test
    sudo mount /dev/mmcblk0p1 test
    sudo scp hjk@192.168.1.112:/mnt/Data/workspace/peta_prj/hawaii_16K_2GB_h128_202503251750/hawaii/images/linux/image.ub ./test
    sudo scp hjk@192.168.1.112:/mnt/Data/workspace/peta_prj/hawaii_16K_2GB_h128_202503251750/hawaii/images/linux/system.bit ./test
    sync
    sudo umount /dev/mmcblk0p1
5. 下载rootfs.tar.gz到mmcblk0p2 并解压
    sudo mount /dev/mmcblk0p2 ./test
    sudo scp hjk@192.168.1.112:/mnt/Data/workspace/peta_prj/hawaii_16K_2GB_h128_202503251750/hawaii/images/linux/rootfs.tar.gz ./test
    sudo tar -xzvf ./test/rootfs.tar.gz
    sync
    sudo umount /dev/mmcblk0p2
    sudo reboot

### 编译结果的文件解释

#### BOOT.bin

用于 Zynq 系列 SoC 的引导文件，包含了引导加载程序（Bootloader）、FSBL（First Stage Boot Loader）、FPGA 位流（FPGA Bitstream）和其他引导所需的文件。
引导加载程序会加载 BOOT.bin 文件到 SoC 中，然后执行其中的 FSBL，接着启动 Linux 内核或者其他操作系统。
BOOT.bin 文件通常由 Xilinx Vivado 工具生成，其中包含了 FSBL 和 FPGA 位流的打包，用于引导 Zynq 系列 SoC 并初始化系统。

#### image.ub  

通常包含了引导加载程序（Bootloader）、内核（Kernel）和根文件系统（Root File System）等

#### system.bit(bitstream)

包含了 FPGA 的配置信息以及设计在 FPGA 中的逻辑实现。

#### rootfs_cpio.tar.gz

根文件系统的打包文件，用于存储整个文件系统的内容。这种文件通常包含了操作系统中的文件、目录结构、库文件、配置文件等。其中，.cpio 表示使用 cpio 工具进行打包，
.tar.gz 表示使用 tar 工具进行打包并使用 gzip 进行压缩。在嵌入式系统中，根文件系统是操作系统运行时所需的文件系统，包含了操作系统核心和用户空间程序所需的文件。

#### zynqmp_fsbl.elf

是 Zynq 系列 SoC 中的第一阶段引导加载程序（FSBL），用于启动系统并配置硬件环境。
FSBL 负责初始化处理器、DDR 存储器、外设等硬件，并加载 Linux 内核或其他操作系统到内存中。
zynqmp_fsbl.elf 是 FSBL 的可执行文件

### u-boot

U-Boot是嵌入式系统中常用的引导加载程序，负责初始化硬件并加载操作系统。
of_**系列函数是设备树相关函数

### SysMonPSU

AMS Analog Mixed Signal
XADC Xilinx Analog-to-Digital Converter

在/sys/bus/iio/devices/iio\:device0 下会更新设备的一些状态信息

#### pgrep 获取指定进程信息

#### 驱动创建

petalinux-create -t modules --name fpgaversion --enable

#### 应用创建

petalinux-create -t apps --template install --name autoinsmod --enable

### gdb 远程调试

1. 在petalinux项目中运行, 编译gdbserver

```console
petalinux-config -c rootfs
```

Filesystem Package
    --> misc
        ---> gdb
            --> [*]gdbserver

2. petalinux-build 之后用这个系统即可

3. 启动系统后运行

```console
sudo gdbserver 宿主机ip:端口号 ./hawaii 
```

hawaii运行需要sudo权限

在宿主机vscode中添加项目launch.json (设置根据自己的设置更改)

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "(gdb) 启动",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/bin/hawaii",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb-multiarch",
      "miDebuggerServerAddress": "192.168.1.10:1234",
      "setupCommands": [
        {
          "description": "为 gdb 启用整齐打印",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ]
    },
  ]
}
```

打完断点后直接gdb启动就行

# create modules 驱动

自动添加module

```bash
#!/bin/bash
modules=("dmacloud" "dmaraw" "fittingconfig" "fpgaversion" "jesd204rx0"
        "jesd204rx1" "pcld32hz8ch" "quadspi" "scan32hz18b" "systemconfig"
        "tn581tia")

for module in "${modules[@]}"; do
        petalinux-create -t modules --name "$module" --enable
done

apps=("autoinsmod" "autostart")
for app in "${apps[@]}"; do
  petalinux-create -t apps --template install --name "$app" --enable
done
 
```

petalinux-create -t modules --name dmacloud --enable
petalinux-create -t modules --name dmaraw --enable
petalinux-create -t modules --name fittingconfig --enable
petalinux-create -t modules --name fpgaversion --enable
petalinux-create -t modules --name jesd204rx0 --enable
petalinux-create -t modules --name jesd204rx1 --enable
petalinux-create -t modules --name pcld32hz8ch --enable
petalinux-create -t modules --name quadspi --enable
petalinux-create -t modules --name scan32hz18b --enable
petalinux-create -t modules --name systemconfig --enable
petalinux-create -t modules --name tn581tia --enable
<!-- petalinux-create -t modules --name pac194x5x --enable -->

## linuxptp

### 内核配置

1. 进入内核配置界面 petalinux-config -c kernel
   Device Drivers → Networking support → Timestamping in Phy devices
2. Device Drivers → PTP clock support → XLILINX PTP CLOCK
3. Device Drivers → PPS support → PPS client using GPIO
4. Device Drivers → Network device support → Ethernet driver support → Generate hardware packet timestamps

### rootfs设置

petalinux-config → Filesystem Packages → base → linuxptp
petalinux-config → Filesystem Packages → base → linuxptp-dev

## 设置自动登录

1. petalinux-config -c rootfs

# PetaLinux patch生成和应用方法整理

## patch

见[patch生成和应用](https://adaptivesupport.amd.com/s/article/Petalinuxpatch%E7%94%9F%E6%88%90%E5%92%8C%E5%BA%94%E7%94%A8%E6%96%B9%E6%B3%95%E6%95%B4%E7%90%86?language=zh_CN)

### 2021.1及以后的版本

1. 直接通过petalinux工具中的命令，把源码下载到当前的petalinux工程中然后直接按照需要修改，命令格式如下：

```bash
petalinux-devtool modify <recipe-name>
# 例如
petalinux-devtool modify linux-xlnx

```

Linux kernel的源码会被下载到<plnx-proj-root>/components/yocto/workspace/sources/<recipe-name>文件夹下。然后可以直接在这个位置对kernel的源码进行修改，修改完成后直接用petalinux-build就可以编译进去了。
如果不知道确切的recipe的名字的话，可以使用petalinux-devtool search <key word>来查找。
例如：petalinux-devtool search xen

## petalinux-build 清理

### 清理所有构建输出和缓存

petalinux-build -x mrproper

### 或者使用

petalinux-build -x distclean

### 清理特定组件

petalinux-build -c <component> -x clean

### 清理内核

petalinux-build -c kernel -x clean

### 清理 u-boot

petalinux-build -c u-boot -x clean

### 清理根文件系统

petalinux-build -c rootfs -x clean

### 清理设备树

petalinux-build -c device-tree -x clean


