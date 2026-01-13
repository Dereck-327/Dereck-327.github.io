---
title: NXP OS System        
data: 2025-12-23 10:44:27
tags: [os]             
categories: [linux]              
description: os system  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# standalone 编译各模块

## uboot

### 拉取uboot仓库

```bash
# clone 自己的仓库后，将官方的仓库subtree下来 注意可能需要代理
git remote add uboot-imx https://github.com/nxp-imx/uboot-imx.git
git subtree add --prefix=uboot-imx uboot-imx lf_v2025.04 --squash # --squash 是将remote仓库的所有提交commint成一个
# 拉取atf信任固件
git remote add imx-atf https://github.com/nxp-imx/imx-atf 
git subtree add --prefix=imx-atf imx-atf lf_v2.12 --squash

# 下载ddr training bin
mkdir firmware-imx
wget -P ./firmware-imx https://www.nxp.com/lgfiles/NMG/MAD/YOCTO/firmware-imx-8.29-8741a3b.bin
chmod a+x ./firmware-imx/firmware-imx-8.29-8741a3b.bin
./firmware-imx/firmware-imx-8.29-8741a3b.bin

# 拉取imx-mkimage
git remote add imx-mkimage https://github.com/nxp-imx/imx-mkimage
git subtree add --prefix=imx-mkimage imx-mkimage lf-6.12.34_2.1.0 --squash
```

### 编译各个仓库

```bash
source /opt/fsl-imx-xwayland/6.12-walnascar/environment-setup-armv8a-poky-linux
export ARCH=arm64
export AS="$CC"
export LD="$CC"

# 编译imx uboot
cd uboot-imx
make distclean
make imx8mp_evk_defconfig
make
make dtbs
# 编译 ARM Trusted Firmware (ATF)
cd imx-atf/
make PLAT=imx8mp bl31

```

### 添加打包脚本 `build_flash_bin.sh`

```sh
#!/usr/bin/env bash
set -euo pipefail

# 获取脚本和工程根目录
script_path=$(readlink -f "$0")
CDIR=$(dirname "$script_path")
TOPDIR=$(readlink -f "$CDIR/")

DEST="$TOPDIR/imx-mkimage/iMX8M"
mkdir -p "$DEST"

echo "目标目录: $DEST"
# exit 0

# 简单复制单个文件（存在则复制，否则打印警告）
copy_file() {
    local src="$1" dst="$2"
    if [ -e "$src" ]; then
        cp -av "$src" "$dst"
    else
        echo "Warning: not found: $src"
    fi
}

# 复制并匹配通配符（使用 nullglob 避免字面匹配）
copy_glob() {
    local pattern="$1"
    shopt -s nullglob
    local files=( $pattern )
    if [ ${#files[@]} -eq 0 ]; then
        echo "Warning: no files match: $pattern"
    else
        cp -av "${files[@]}" "$DEST/"
    fi
    shopt -u nullglob
}

# 实际复制操作（路径相对于工程根）
copy_file "$TOPDIR/uboot-imx/spl/u-boot-spl.bin" "$DEST/"
copy_file "$TOPDIR/uboot-imx/u-boot-nodtb.bin" "$DEST/"
copy_file "$TOPDIR/uboot-imx/dts/upstream/src/arm64/freescale/imx8mp-evk.dtb" "$DEST/"
# copy_file "$TOPDIR/uboot-imx/arch/arm/dts/imx8mp-evk.dtb" "$DEST/"
copy_file "$TOPDIR/imx-atf/build/imx8mp/release/bl31.bin" "$DEST/"

copy_glob "$TOPDIR/firmware-imx/firmware-imx-8.29-8741a3b/firmware/ddr/synopsys/lpddr4_pmu_train_*"

# 复制并重命名 mkimage 为 mkimage_uboot
if [ -e "$TOPDIR/uboot-imx/tools/mkimage" ]; then
    cp -av "$TOPDIR/uboot-imx/tools/mkimage" "$DEST/mkimage_uboot"
else
    echo "Warning: not found: $TOPDIR/uboot-imx/tools/mkimage"
fi

#%%
# 在 DEST 的上一级执行 make SOC=iMX8MP flash_evk
PARENT_DIR="$(dirname "$DEST")"
if [ -f "$PARENT_DIR/Makefile" ] || [ -f "$PARENT_DIR/makefile" ]; then
    echo "切换到 $PARENT_DIR 并执行: make SOC=iMX8MP flash_evk"
    ( cd "$PARENT_DIR" && make SOC=iMX8MP flash_evk )
else
    echo "Warning: Makefile not found in $PARENT_DIR, 跳过 make"
fi

echo "完成."
```

## kernel

### 拉取nxp官方仓库

```bash
# clone 自己的仓库后，将官方的仓库subtree下来 注意可能需要代理
git remote add linux-imx https://github.com/nxp-imx/linux-imx
git subtree add --prefix=linux-imx linux-imx lf-6.12.y --squash # --squash 是将remote仓库的所有提交commint成一个
```

## rootfs

### 打包设备上的文件系统

```bash
mount /dev/mmcblk2p2 ./tmp
cd tmp

# 记录文件所有者的 UID/GID（数字），而不是用户名
tar --numeric-owner -czvf ../rootfs_ubuntu_24_04.tar.gz .  # 压缩 
tar -tvf ../rootfs_ubuntu_24_04.tar.gz | head -n 20 # 查看

```

# yocto 编译

## 检查磁盘空间，至少需要200GB空间

```bash
wsl.exe --system -d Ubuntu-22.04 df -h /mnt/wslg/distro
# Ubuntu-22.04为采用wsl.exe -l查看到的安装版本
```

代理设置

```bash
export https_proxy=http://127.0.0.1:7897 http_proxy=http://127.0.0.1:7897 all_proxy=socks5://127.0.0.1:7897
# 不同代理设置方式不一样，用代理下载更快
```

1. 安装必要的库

```bash
sudo apt-get install build-essential chrpath cpio debianutils diffstat file gawk gcc git iputils-ping libacl1 liblz4-tool locales python3 python3-git python3-jinja2 python3-pexpect python3-pip python3-subunit socat texinfo unzip wget xz-utils zstd
# 这里面可能仍然缺少一些库，后续编译出错的时候自行Google安装即可
```

2. Setting up the Repo utility

```bash
mkdir ~/bin (this step may not be needed if the bin folder already exists)
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod a+x ~/bin/repo
export PATH=~/bin:$PATH
```

3. Yocto Project Setup

```bash
mkdir imx-yocto-bsp
cd imx-yocto-bsp
repo init -u https://github.com/nxp-imx/imx-manifest -b imx-linux-walnascar -m imx-6.12.34-2.1.0.xml
repo sync
```

4. Image Build

```bash
DISTRO=fsl-imx-xwayland MACHINE=imx8mpevk source ./imx-setup-release.sh -b hawaii_ii
# 激活环境设置设备参数和编译路径，设备参数的选项还没搞懂
DISTRO=fsl-imx-xwayland MACHINE=imx8mpevk bitbake core-image-base -c populate_sdk
# 编译kernel镜像和编译工具链
DISTRO=fsl-imx-xwayland MACHINE=imx8mp-evk bitbake -c deploy imx-boot
# 编译U-boot镜像
```

# 操作系统修改

## uboot更改

### otg挂载的ic2

- /home/hjk/workspace/04.HAWAII_II/imx-sys/u-boot/uboot-imx/board/freescale/imx8mp_evk/imx8mp_evk.c
  
```c
/* Port2 is the power supply, port 1 does not support power */
struct tcpc_port_config port1_config = {
  .i2c_bus = 0, /*i2c1 官方挂在了i2c2 我们挂在了ic21 */
  .addr = 0x50,
  .port_type = TYPEC_PORT_UFP,
  .max_snk_mv = 20000,
  .max_snk_ma = 3000,
  .max_snk_mw = 45000,
  .op_snk_mv = 15000,
  .switch_setup_func = &pd_switch_snk_enable,
  .disable_pd = true,
};

```

### uboot环境变量修改

- /home/hjk/workspace/04.HAWAII_II/imx-sys/u-boot/uboot-imx/board/freescale/imx8mp_evk/imx8mp_evk.env

```env
// 从otg启动的uboot是在ram中运行的读取的是默认环境变量
// 设置fastboot dev为emmc
fastboot_dev=mmc2
// 修改mmcroot
mmcroot=/dev/mmcblk2p2 rootwait rw
```

```uboot
setenv bootcmd 'run prepare_mcore; run sr_ir_v2_cmd; bootflow scan -lb; run bsp_bootcmd'
saveenv
```

### uboot 白板分区

evk gpt分区表

```bash
# 使用usb挂载到电脑
ums 0 mmc 2
```

```txt
idx 0, ptn 0 name='gpt'         start=0       len=2048
idx 1, ptn 0 name=''            start=0       len=0
idx 2, ptn 0 name='all'         start=0       len=61071360
idx 3, ptn 0 name='bootloader'  start=0       len=8192
idx 4, ptn 1 name='mmcsdc1'     start=16384   len=524288
idx 5, ptn 2 name='mmcsdc2'     start=540672  len=21024114
```

- 使用fatwrite: fatwrite mmc 2:2 ${loadaddr} Image ${filesize}
  
```sh
            echo_pink "[0] 格式化并创建分区~"
            if test -f $UBOOT_DIR ; then
                echo "$UBOOT_DIR SIZE: $(ls -lh $UBOOT_DIR | awk '{print $5}')"
                echo ""
                echo "FB: ucmd setenv mmcdev 2" >> $UUU_TMP_DIR
                echo "FB: ucmd mmc dev \${mmcdev}" >> $UUU_TMP_DIR
                # 擦除前32GB，防止残留分区表
                echo "FB: ucmd mmc erase 0 0x3C00000" >> $UUU_TMP_DIR
                # 分区名与fastboot分区表同步
                echo "FB: ucmd setenv partitions 'name=bootloader,start=1M,size=4M;name=mmcsdc1,size=500M,type=data;name=mmcsdc2,size=28G'" >> $UUU_TMP_DIR
                echo "FB: ucmd gpt write mmc \${mmcdev} \$partitions" >> $UUU_TMP_DIR

                echo "FB: ucmd echo Partitioning and formatting done." >> $UUU_TMP_DIR
                echo "FB: done" >> $UUU_TMP_DIR
                $UUU_DIR $UUU_TMP_DIR
            else
                echo_red "$UBOOT_DIR 不存在"
            fi
            ;;

```

### 添加版本号

/home/hjk/workspace/04.HAWAII_II/hawaii-sys/uboot/uboot-imx/common/board_f.c

```bash

```

## kernel修改


# 进系统后修改

## 更改hostname

```bash
hostnamectl set-hostname hawaii-gen2
su - root
```

## 设置字符宽度和刷新文件系统

```bash
stty cols 260
# ext4 文件系统去填充整个分区
sudo resize2fs /dev/mmcblk2p2
# 设置root密码 # q
passwd root

```

## 添加root用户

```bash
adduser hawaii
usermod -aG sudo hawaii
```

## 设置静态ip

```bash
cp /etc/netplan/01-netcfg.yaml /etc/netplan/01-netcfg.yaml.bak
```

```yaml
# network:
#   version: 2
#   ethernets:
#     # 使用通配符匹配所有以太网接口，自动启用 DHCP
#     # 适用于 eth0, ens18, enp1s0 等各种命名
#     all-eth:
#       match:
#         name: "e*"
#       dhcp4: true
#       optional: true
    # end0:
    #   dhcp4: no
    #   addresses:
    #     - 192.168.2.100/24  
    #   routes:
    #     - to: default
    #       via: 192.168.2.1 
    #   nameservers:
    #     addresses: [8.8.8.8, 114.114.114.114]
network:
  version: 2
  renderer: networkd
  ethernets:
    end0:
      dhcp4: true
      optional: true

    swp1: {}
    swp2: {}
    swp3: {}
    swp4: {}
    end1: {}

  bridges:
    br0:
      interfaces: [swp1, swp2, swp3, swp4]
      dhcp4: no
      addresses:
        - 172.24.172.10/24
      parameters:
        stp: false
        forward-delay: 0
      nameservers:
        addresses: [8.8.8.8, 114.114.114.114]
```

```bash
sudo bash -c 'cat << EOF > /etc/netplan/01-netcfg.yaml
network:
  version: 2
  renderer: networkd
  ethernets:
    end0:
      dhcp4: no
      addresses:
        - 192.168.2.100/24  
      routes:
        - to: default
          via: 192.168.2.1 
      nameservers:
        addresses: [8.8.8.8, 114.114.114.114]

    swp1: {}
    swp2: {}
    swp3: {}
    swp4: {}
    end1: {}

  bridges:
    br0:
      interfaces: [swp1, swp2, swp3, swp4]
      dhcp4: no
      addresses:
        - 172.24.172.100/24
      parameters:
        stp: false
        forward-delay: 0
      nameservers:
        addresses: [8.8.8.8, 114.114.114.114]
EOF'
```

## 拷贝固件

```bash
# 拷贝sdma等
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/01.imx_sys/tq_imx8mp_sdk/debian-12/debian-bookworm-arm64-minimal/lib/firmware/imx /lib/firmware/imx
# 拷贝 optee
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/04.HAWAII_II/imx-sys/nxp-rootfs/lib/firmware/regulatory.db /lib/firmware/
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/04.HAWAII_II/imx-sys/nxp-rootfs/lib/firmware/regulatory.db.p7s /lib/firmware/
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/04.HAWAII_II/imx-sys/nxp-rootfs/lib/firmware/tee-header_v2.bin /lib/firmware/
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/04.HAWAII_II/imx-sys/nxp-rootfs/lib/firmware/tee-pageable_v2.bin /lib/firmware/
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/04.HAWAII_II/imx-sys/nxp-rootfs/lib/firmware/tee-pager_v2.bin /lib/firmware/
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/04.HAWAII_II/imx-sys/nxp-rootfs/lib/firmware/tee-raw.bin /lib/firmware/
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/04.HAWAII_II/imx-sys/nxp-rootfs/lib/firmware/tee.bin /lib/firmware/
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/04.HAWAII_II/imx-sys/nxp-rootfs/lib/firmware/tee.elf /lib/firmware/
# 拷贝m7程序
sudo scp -r hjk@192.168.1.111:/mnt/d/WorkProgram/18.HAWAII_II/mcu_prj/hawaii_m7/armgcc/release/hawaii_mcu.elf /lib/firmware
sudo scp -r hjk@192.168.1.111:/mnt/d/WorkProgram/18.HAWAII_II/mcu_prj/hawaii_m7/armgcc/release/hawaii_mcu.bin /lib/firmware

# 拷贝rpmsg驱动
sudo scp -r hjk@192.168.1.111:/home/hjk/workspace/04.HAWAII_II/imx-sys/linux-imx-lf-6.12.34-2.1.0/drivers/rpmsg/hawaii_rpmsg.ko .
# 拷贝sja1105p switch配置文件
sudo scp -r hjk@192.168.1.111:/mnt/e/10.NXP/nxp-office/SJA1105/SJA1105Q-EVB-CONFIGURATION-TOOLS/sja1105x/tools/firmware_generation/sja1105p_cfg.bin /lib/firmware
```

## 提升内核打印等级

```bash
# 查看当前级别
cat /proc/sys/kernel/printk
# 输出示例: 4    4    1    7

# 提升到最高级别 (打印所有信息)
echo "8 4 1 7" > /proc/sys/kernel/printk
```

## 修改欢迎横幅

```bash
# 删除或去除执行权限所有 update-motd.d 下的脚本：
sudo chmod -x /etc/update-motd.d/*
# 或者直接删除不需要的脚本：
sudo rm /etc/update-motd.d/*

vim /etc/update-motd.d/99-custom
```

```custom

```

## root允许远程登录

```bash
# 自动查找并将 #PermitRootLogin 或 PermitRootLogin 替换为 PermitRootLogin yes
sed -i 's/^#*PermitRootLogin.*/PermitRootLogin yes/' /etc/ssh/sshd_config

# 验证是否修改成功
grep "PermitRootLogin" /etc/ssh/sshd_config

systemctl restart ssh
```

## xterm串口设置

```bash
apt install xterm
resize
```

# sja1105p 设置

## switch 物理主接口

在 DSA (Distributed Switch Architecture) 架构中，end1 只是一个数据通道（Conduit）。当你连接了 SJA1105 交换机后，你不应该直接在 end1 上配置 IP 地址。  
流量路径错误：如果把 IP 设在 end1 上，协议栈会尝试直接从 end1 发出包。但由于 end1 现在接的是 SJA1105 的 CPU 端口，它期望收到带有 DSA Tag（标签） 的数据包，而 end1 直接发出的普通包会被交换机丢弃。  
🚀

设置好netplan之后，直接设置网桥属性 设置端口隔离即可  
创建`isolate_ports.sh` 并执行

```sh
#!/bin/bash
ethtool -K end1 rx-vlan-filter off
# 1. 启用网桥 VLAN 过滤（这是下发硬件表的总开关）
echo "Enabling VLAN filtering on br0..."
ip link set dev br0 type bridge vlan_filtering 1

# 2. 清理所有端口默认的 VLAN 1 (防止广播域重叠)
echo "Clearing default VLAN 1 from ports..."
bridge vlan del dev br0 vid 1 self
for i in {1..4}; do
    bridge vlan del dev swp$i vlan 1
done

# 3. 为每个端口分配独立的硬件 VLAN (PVID)
# 这样进入 swp1 的包会被打上 11，swp2 的打上 12，以此类推
echo "Assigning isolated VIDs to ports..."
bridge vlan add dev swp1 vid 11 pvid untagged
bridge vlan add dev swp2 vid 12 pvid untagged
bridge vlan add dev swp3 vid 13 pvid untagged
bridge vlan add dev swp4 vid 14 pvid untagged

# 4. 关键：让 CPU 端口 (br0) 能够接收所有这些隔离的 VLAN
# 否则 CPU 就收不到来自 swp1-4 的数据包了
echo "Configuring CPU port (br0) to accept all VIDs..."
bridge vlan add dev br0 vid 11 untagged self
bridge vlan add dev br0 vid 12 untagged self
bridge vlan add dev br0 vid 13 untagged self
bridge vlan add dev br0 vid 14 untagged self

bridge vlan add dev br0 vid 10 pvid untagged self

bridge vlan add dev swp1 vid 10
bridge vlan add dev swp2 vid 10
bridge vlan add dev swp3 vid 10
bridge vlan add dev swp4 vid 10


echo "Isolation configuration complete."
```

禁用 CPU 网卡的 VLAN 接收过滤  
主控网卡 end1 其实不需要做精细的 VLAN 过滤，过滤应该交给交换机芯片  

```bash
# 禁用 VLAN 收取过滤
ethtool -K end1 rx-vlan-filter off
```

🚀 后面的操作可以不用了，不用看

```bash
# 清除主接口 end1 的 IP
ip addr flush dev end1
# 激活所有物理端口
ip link set swp1 up
ip link set swp2 up
ip link set swp3 up
ip link set swp4 up

#####  端口隔离 #######
# 1. 创建网桥设备
sudo ip link add name br0 type bridge

# 2. 启动网桥（这一步非常重要，否则网桥无法工作）
sudo ip link set br0 up
# 3. 将所有端口加入网桥
ip link set swp1 master br0
ip link set swp2 master br0
ip link set swp3 master br0
ip link set swp4 master br0
# 在桥接接口上设置 IP： 不要 给 end1 或 swp1-4 设置 IP，而是给 br0 设置 IP
ifconfig end1 0.0.0.0
ifconfig br0 172.16.2.100 netmask 255.255.255.0
```

```bash
# 查看系统中所有的网桥设备
ip link show type bridge
# 查看哪些接口插在哪些网桥上
bridge link show
# 查看网桥学习到的 MAC 地址表（非常有用！）
bridge fdb show dev br0
# 激活网桥（创建后默认是 DOWN 的）
ip link set br0 up
# 删除网桥（删除前最好先卸载从设备）
ip link del br0
```

## switch 端口测试

### 内部通路测试：自环测试 (Loopback)  

要测试 end1 (CPU) 与 SJA1105 内部端口是否通畅，最简单的方法是在 swp 端口上做回环。  
物理回环： 用一根网线短接 swp1 和 swp2。  
软件测试： 给这两个端口配置同一个网段的不同 IP，尝试互 Ping。  

```bash
ip addr add 10.0.0.1/24 dev swp1
ip addr add 10.0.0.2/24 dev swp2
ip link set swp1 up
ip link set swp2 up
ping -I swp1 10.0.0.2
```

如果能 Ping 通，说明：CPU -> end1 -> SJA1105 -> swp1 -> 网线 -> swp2 -> SJA1105 -> end1 -> CPU 这条路径全线贯通  

### 使用 tcpdump 抓包观察 (验证 CPU Port 转发)

在 CPU 端启动监听：  

```bash
tcpdump -i end1 -n -e
```

注意：在 DSA 架构下，end1 上的包通常带有特殊的 Tag（SJA1105 专用头部），普通 tcpdump 可能识别为未知协议，但只要有流量计数增加就说明通路正常  

# linuxptp 边界时钟设置

```bash
# 运行边界时钟
ptp4l -2 -f linux_ptp_bc.cfg -m && phc2sys -a -r -m
# 需要关闭网桥
ip link set br0 down
```

```conf
[global]
boundary_clock_jbod     1
# 硬件时间戳
time_stamping           hardware

delay_mechanism         E2E

network_transport       L2
verbose                 1

priority1               254
priority2               254

[end0]

[swp1]
[swp2]
[swp3]
[swp4]
```


# arm app 运行需要的库

## libgpiod

```bash
#将/home/hjk/workspace/04.HAWAII_II/arm_app/lib/libgpiod.so.3 拷贝到 /usr/lib
scp hjk@10.46.10.110:/home/hjk/workspace/04.HAWAII_II/arm_app/lib/libgpiod.so.3 /usr/lib/
```

# hawaii-gen2 开机服务

## soc复位

```bash
# 1. 重新加载 systemd 管理器配置
sudo systemctl daemon-reload

# 2. 设置开机自动启动
sudo systemctl enable indie_soc_en.service

# 3. 立即手动启动测试（用于验证配置是否正确）
sudo systemctl start indie_soc_en.service
```

/etc/systemd/system/indie_soc_en.service

```service
[Unit]
Description=Run Indie Soc after Boot
After=multi-user.target

[Service]
Type=oneshot
WorkingDirectory=/root
Environment="LD_LIBRARY_PATH=/usr/local/lib:/usr/lib"
ExecStart=/root/unit_test --gtest_filter=UtilsTest.soc_rest
StandardOutput=journal+console
StandardError=journal+console

[Install]
WantedBy=multi-user.target
```

```sh
#bash
gpioset gpiochip0 0=0
gpioset gpiochip0 5=1
gpioset gpiochip0 9=0
gpioset gpiochip0 9=1
```

