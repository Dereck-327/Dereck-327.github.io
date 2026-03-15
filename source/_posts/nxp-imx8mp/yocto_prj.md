---
title: NXP OS System        
data: 2026-01-19 10:44:27
tags: [os]             
categories: [yocto]              
description: yocto system  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# yocto 编译nxp系统

## 检查 Yocto 编译出来的 .wic 镜像内容

### 查看分区信息

```bash
# 查看分区信息
fdisk -l core-image-*.wic
```

### 挂载 .wic 镜像分区

```bash
unzstd ./tmp/deploy/images/imx8mpevk/core-image-base-*.wic.zst
sudo losetup -Pf --show ./tmp/deploy/images/imx8mpevk/core-image-base-*.wic
sudo mkdir ./wicroot
sudo mount /dev/loop0p2 ./wicroot
ls ./wicroot/
ls ./wicroot/usr/bin/

# 卸载并释放 loop 设备
sudo umount ./wicroot
sudo losetup -d /dev/loop0
```

### 扩展rootfs分区边界

```bash
fdisk /dev/mmcblk2
# 输入 p查看当前分区，记下第2分区的 Start（起始扇区）数值
# 输入 d，然后输入 2 删除第2分区。
# 输入 n 新建分区，输入 p，编号输入 2
# 起始扇区（First sector)：输入刚才记下的那个数字
# 结束扇区（Lastsector）：直接按回车（默认使用全部剩余空间）。
# 如果提示 Do you want to remove the signature?，输入 N。
# 输入 w 保存退出。

# partprobe /dev/mmcblk2
resize2fs /dev/mmcblk2p2
```

```sh
# 自动扩展 /dev/mmcblk2 的第2分区到剩余空间，并扩展文件系统
# 注意：请确保已备份数据，且第2分区为ext4文件系统

#!/bin/bash
DEV=/dev/mmcblk2
PART=${DEV}p2

# 获取第2分区起始扇区
START=$(fdisk -l $DEV | awk '/^'"${DEV}p2"'/ {print $2}')
echo "第2分区起始扇区: $START"

# 自动重建分区表（无交互）
echo -e "d\n2\nn\np\n2\n$START\n\nN\nw\n" | fdisk $DEV

# 刷新分区表
partprobe $DEV

# 扩展文件系统
resize2fs $PART

echo "分区扩展完成。"
```

### 在yocto项目中运行简易的apt源服务器

```bash
# cd build/tmp/deploy/deb
python3 -m http.server 8080
```

```bash
cat <<EOF > /etc/apt/sources.list
deb [trusted=yes] http://10.46.10.110:8080/armv8a ./
deb [trusted=yes] http://10.46.10.110:8080/all ./
deb [trusted=yes] http://10.46.10.110:8080/armv8a-mx8mp ./
deb [trusted=yes] http://10.46.10.110:8080/imx8mpevk ./
EOF
```

### netplan 不使用网桥

```bash
network:
  version: 2
  renderer: networkd
  ethernets:
    eth0:
      dhcp4: true
      optional: true
      addresses:
        - 192.168.1.10/24

    eth1: {}

    swp1:
      addresses: [172.24.172.101/24]
      optional: true
      routes:
        - to: 172.24.172.11/32
          via: 0.0.0.0

    swp2:
      addresses: [172.24.172.102/24]
      optional: true
      routes:
        - to: 172.24.172.12/32
          via: 0.0.0.0

    swp3:
      addresses: [172.24.172.103/24]
      optional: true
      routes:
        - to: 172.24.172.13/32
          via: 0.0.0.0

    swp4:
      addresses: [172.24.172.104/24]
      optional: true
      routes:
        - to: 172.24.172.14/32
          via: 0.0.0.0
```

```bash
ip route add 172.24.172.11 dev swp1
ip route add 172.24.172.12 dev swp2
ip route add 172.24.172.13 dev swp3
ip route add 172.24.172.14 dev swp4
```

### ptp配置

#### .conf

```conf
[global]
boundary_clock_jbod  1
network_transport    L2
delay_mechanism      P2P

[eth0]
# 默认使用 ptp0 (fec ptp)，作为 Slave 端口同步外部时间

[swp1]
# 强制关联 SJA1105 的硬件时钟 (ptp1)
phc_index            1

[swp2]
phc_index            1

[swp3]
phc_index            1

[swp4]
phc_index            1

```

```bash
phc2sys -s eth0 -c swp1 -O 0 -m && ptp4l -2 -f ptp.cfg -m
```

```bash
# -w: 等待 ptp4l 同步后再开始
phc2sys -s eth0 -c swp1 -w -m

tcpdump -i swp1 ether proto 0x88f7 -n -e
```

## core dump

ulimit -c unlimited
sysctl -w kernel.core_pattern=core


## 透传

```bash
ip link add name br0 type bridge
ip link set br0 up
ip link set eth0 master br0
ip link set swp1 master br0
ip link set swp2 master br0
ip link set swp3 master br0
ip link set swp4 master br0
ip addr delete 192.168.1.10/24 dev eth0
ip addr addr 192.168.1.10/24 dev br0
```

## 打开 静态ip ssh连接服务

```bash
vi /etc/ssh/sshd_config
# 打开下面两个选项
## UseDNS no
## GSSAPIAuthentication no
```

