---
title: linux kernel
tags: [os]
categories: [linux]
description: linux kernel learning
date: 2025-05-12 11:02:41
top_img: /image/动漫少女.jpg
cover: /image/动漫少女.jpg
---


# kernel 编译常见错误

### 编译选项

#### 证书

make[1]: ***[/home/hjk/Desktop/workspace/00.linux-kernel/linux-6.14.2/Makefile:1994: .] Error 2
make:*** [Makefile:251: __sub-make] Error 2

### 编译时内存不足(多半是虚拟机分配的内存空间不足)

```shell
# 1. 创建分区
sudo dd if=/dev/zero of=/swapfile bs=1M count=1024    # 1 * 1024 = 1024 创建 1 g 的内存分区
sudo mkswap /swapfile
sudo swapon /swapfile

# free -m    #可以查看内存使用
# 创建完交换分区之后就可以继续编译
# 编译完之后记得用以下命令关闭交换分区
# 某次我就是忘了关闭交换分区，导致开不了机，然后切换 tty1 ，登进去之后关闭交换分区才可以进入桌面的。

#2. 关闭分区
sudo swapoff /swapfile
sudo rm /swapfile
```

分配swap内存分区的时候可能会遇到文件busy打不开
dd: failed to open '/swapfile': Text file busy
可以关闭swap分区再尝试进行分区

```shell
sudo swapoff -a
```

# 文件挂载

D 是共享文件夹名称

```shell
# 查看共享文件夹
vmware-hgfsclient 

# 创建挂载目录
sudo mkdir /mnt/hgfs/d  
# 挂载 
sudo mount -t fuse.vmhgfs-fuse .host:/D /mnt/hgfs/d/ -o allow_other 
# 卸载
sudo umount -a fuse.vmhgfs-fuse .host:/D /mnt/hgfs/d/
```

系统启动后自动挂载共享文件夹

```shell
sudo vim /etc/fstab
# 在文件最后加入
.host:/D        /mnt/hgfs/d/    fuse.vmhgfs-fuse        allow_other     0       0
```

# kernel debug调试
>
> 使用qemu模拟对应的硬件平台, 然后使用gdb和vscode远程调试
>
## 虚拟机ubuntu设置

1. 安装gdb和对应的qemu-system-目标平台
2. 编译完的内核 vmlinux 需要开启debug info 对应去menuconfig中修改即可 默认是带调试信息的
3. 启动qemu
qemu-system-x86_64 -kernel build_x64/arch/x86_64/boot/bzImage -append "console=ttyS0 nokaslr" -nographic -s -S

## 宿主机vscode设置

remote ssh连接上设备后， 配置.vscode/launch.json
可以通过ctrl+alt+p  选择设置launch.json

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "(gdb) 启动",
      "type": "cppdbg",
      "request": "launch",
      "program": "/usr/src/00.kernel/linux-5.15.180/build_x64/vmlinux",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb-multiarch",
      "miDebuggerServerAddress": "localhost:1234",
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

选中源码后使用gdb调试即可(注意先把断点打好)

qemu-system-aarch64 -machine virt -cpu cortex-a57 -kernel ./Image -drive file=./rootfs.ext4,format=raw,if=virtio -append "root=/dev/vda console=ttyS0" -s -S -smp 1 -nographic -serial mon:stdio

# 使用WSL代替vmware虚拟机（推荐）

WSL自行下载
内核编译参考[](https://github.com/0voice/linux_kernel_wiki/blob/main/%E6%96%87%E7%AB%A0/QEMU%E8%B0%83%E8%AF%95Linux%E5%86%85%E6%A0%B8%E7%8E%AF%E5%A2%83%E6%90%AD%E5%BB%BA.md)

退出gdb  ctrl + D
退出qemu 先按 ctrl + a, 再按 c(x)

启动qemu
qemu-system-x86_64 -kernel ./00.kernel/linux-5.10.236/arch/x86_64/boot/bzImage  -hda ./rootfs.img  -append "root=/dev/sda console=ttyS0" -s -S -smp 1 -nographic

```shell
qemu-system-x86_64 -kernel ./00.kernel/linux-5.10.236/arch/x86_64/boot/bzImage  -hda ./rootfs.img  -append "root=/dev/sda console=ttyS0" -s -S -smp 1 -nographic
# -s -S -smp 1 : -S 是Stop  -smp 是指定cpu数量 -nographic是不使用图形输出窗口

# -kernel # 指定编译好的内核镜像 -hda # 指定硬盘 -append "root=/dev/sda" 指示根文件系统 console=ttyS0 把QEMU的输入输出定向到当前终端上 -nographic 不使用图形输出窗口 -s 是-gdb tcp::1234缩写，监听1234端口，在GDB中可以通过target remote localhost:1234连接
```
