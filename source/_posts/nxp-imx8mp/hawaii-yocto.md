---
title: Hawaii Yocto System        
data: 2026-03-31 10:44:27
tags: [os]             
categories: [yocto]              
description: yocto system  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# yocto 编译nxp系统

 - 使用google的repo工具来管理整个项目的仓库代码

## uboot

建好仓库后拉取imx官方的uboot仓库代码并修改

```bash
git remote add uboot-imx https://github.com/nxp-imx/uboot-imx.git
git fetch uboot-imx lf_v2025.04
git merge --squash --allow-unrelated-histories uboot-imx/lf_v2025.04
git commit -m "Squash merge uboot-imx/lf_v2025.04"
```

### 编译

```bash
make distclean
make imx8mp_evk_defconfig
make
make dtbs
```

## kernel

同样fork imx的仓库过来修改

```bash
git remote add linux-imx https://github.com/nxp-imx/linux-imx.git
git fetch linux-imx lf-6.12.y
git merge --squash --allow-unrelated-histories linux-imx/lf-6.12.y
git commit -m "squash merge linux-imx/lf-6.12.y"
```

### 编译

```bash
make distclean
make imx_v8_defconfig
make -j$(nproc)
```
## meta层代码

```bash
git remote add meta-imx https://github.com/nxp-imx/meta-imx
git fetch meta-imx walnascar-6.12.34-2.1.0
git merge --squash --allow-unrelated-histories meta-imx/walnascar-6.12.34-2.1.0
git commit -m "Squash merge meta-imx/walnascar-6.12.34-2.1.0"
```

## application 

## repo 管理

```bash
git remote add imx-manifest https://github.com/nxp-imx/imx-manifest 
git subtree add --prefix=imx-manifest imx-manifest imx-linux-walnascar --squash
# 拉取最新代码
git subtree pull --prefix=imx-manifest imx-manifest imx-linux-walnascar --squash
```

将uboot kernel 和 meta层代码仓库修改为自己fork下来的仓库地址，并根据需要添加自己的application


# 进系统后的修改

## 扩展rootfs分区边界

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

```bash
ulimit -c unlimited
sysctl -w kernel.core_pattern=core
```

