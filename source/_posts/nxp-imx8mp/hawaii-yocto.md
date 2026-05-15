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
bitbake package-index
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
# 允许跨越不同的硬件时钟（JBOD模式）
boundary_clock_jbod     1
time_stamping           hardware
# 如果不确定下游设备模式，建议先尝试 E2E
delay_mechanism         P2P
network_transport       L2
# 除非特定行业需求，transportSpecific 通常设为 0
transportSpecific       1
domainNumber            0
verbose                 1
# 降低优先级数值（如128），确保在没有外部时钟时才选自己，或者保持254让外部时钟胜出
priority1               254
priority2               254

# 上游接口
[eth0]

# 下游接口
[eth1]

```

```bash
ptp4l -2 -f linux_ptp_bc.cfg -m 

phc2sys -s eth0 -c swp1 -w -m

phc2sys -s eth0 -c swp1 -O 0 -m

# 同步到系统时钟 (-c 不加参数默认就是 CLOCK_REALTIME)
phc2sys -s eth0 -O 0 -m 

# 同步到 swp1
phc2sys -s eth0 -c swp1 -O 0 -m

phc2sys -s /dev/ptp2 -c /dev/ptp1 -O 0 -m
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

# i2c-tools 

## i2cdetect

```bash
i2cdetect -y -r 3 # 扫描总线上的设备 
# -y: 自动确认，不询问
# -r: 使用 SMBus "read byte" 命令探测。这对大多数设备最安全
# 1: 代表总线编号 /dev/i2c-1。

i2cget -y 3 0x18 0x00
# 读取总线 3，地址 0x18，寄存器 0x00 的值

```

## profile

```profile
# /etc/profile: system-wide .profile file for the Bourne shell (sh(1))
# and Bourne compatible shells (bash(1), ksh(1), ash(1), ...).

PATH="/usr/local/bin:/usr/bin:/bin"
[ "$TERM" ] || TERM="xterm-256color"	# Basic terminal capab. For screen etc.

# Add /sbin & co to $PATH for the root user
[ "$HOME" != "/root" ] || PATH=$PATH:/usr/local/sbin:/usr/sbin:/sbin

# Set the prompt for bash and ash (no other shells known to be in use here)
[ -z "$PS1" ] || PS1='\[\e[1;35m\]\u@\h\[\e[0m\]:\[\e[1;36m\]\w\[\e[0m\]\$'

# Use the EDITOR not being set as a trigger to call resize later on
FIRSTTIMESETUP=0
if [ -z "$EDITOR" ] ; then
	FIRSTTIMESETUP=1
fi

if [ -d /etc/profile.d ]; then
	for i in /etc/profile.d/*.sh; do
		if [ -f $i -a -r $i ]; then
			. $i
		fi
	done
	unset i
fi

if [ -t 0 -a $# -eq 0 ]; then
	if [ ! -x /usr/bin/resize ] ; then
		if [ -n "$BASH_VERSION" ] ; then
# Optimized resize funciton for bash
resize() {
	local x y
	IFS='[;' read -t 2 -p $(printf '\e7\e[r\e[999;999H\e[6n\e8') -sd R _ y x _
	[ -n "$y" ] && \
	echo -e "COLUMNS=$x;\nLINES=$y;\nexport COLUMNS LINES;" && \
	stty cols $x rows $y
}
		else
# Portable resize function for ash/bash/dash/ksh
# with subshell to avoid local variables
resize() {
	(o=$(stty -g)
	stty -echo raw min 0 time 2
	printf '\0337\033[r\033[999;999H\033[6n\0338'
	if echo R | read -d R x 2> /dev/null; then
		IFS='[;R' read -t 2 -d R -r z y x _
	else
		IFS='[;R' read -r _ y x _
	fi
	stty "$o"
	[ -z "$y" ] && y=${z##*[}&&x=${y##*;}&&y=${y%%;*}
	[ -n "$y" ] && \
	echo "COLUMNS=$x;"&&echo "LINES=$y;"&&echo "export COLUMNS LINES;"&& \
	stty cols $x rows $y)
}
		fi
	fi
	# only do this for /dev/tty[A-z] which are typically
	# serial ports
	if [ $FIRSTTIMESETUP -eq 1 -a ${SHLVL:-1} -eq 1 ] ; then
		case $(tty 2>/dev/null) in
			/dev/tty[A-z]*) resize >/dev/null;;
		esac
	fi
fi

if [ -z "$EDITOR" ]; then
	EDITOR="vi"			# needed for packages like cron, git-commit
fi

export PATH PS1 OPIEDIR QPEDIR QTDIR EDITOR TERM


alias ls='ls --color=auto'
alias grep='grep --color=auto'
alias fgrep='fgrep --color=auto'
alias egrep='egrep --color=auto'

export LS_COLORS=$LS_COLORS:'di=01;36:'

export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8
umask 022

```

# sja1105 spi配置

```conf
# /etc/sja1105/sja1105.conf
[spi_setup]
        staging_area = /lib/firmware/sja1105.bin
        device       = /dev/spidev1.0
        bits         = 8
        speed        = 1000000
        delay        = 0
        cs_change    = 0
        mode         = SPI_CPHA
        dry_run      = false
        auto_flush   = false

[general]
        screen_width     = 120
        entries_per_line = 10
        verbose          = false
        debug            = false
```

```bash
scp hjk@192.168.1.111:/mnt/e/10.NXP/nxp-office/SJA1105/SJA1105Q-EVB-CONFIGURATION-TOOLS/sja1105x/tools/firmware_generation/sja1105p_cfg.bin /lib/firmware/sja1105.bin
sja1105-tool config upload
sja1105-tool config save bug.xml
sja1105-tool config show
sja1105-tool status general   

```

```bash
ip -s link show dev eth1
```


[global]
network_transport    L2
delay_mechanism      P2P
priority1            254
priority2            254
clientOnly           1
domainNumber         0
verbose              1
time_stamping        hardware
transportSpecific    1

[eth0]

# system log 开启journal 启动log保存

/etc/systemd/journald.conf

```conf

#  This file is part of systemd.
#
#  systemd is free software; you can redistribute it and/or modify it under the
#  terms of the GNU Lesser General Public License as published by the Free
#  Software Foundation; either version 2.1 of the License, or (at your option)
#  any later version.
#
# Entries in this file show the compile time defaults. Local configuration
# should be created by either modifying this file (or a copy of it placed in
# /etc/ if the original file is shipped in /usr/), or by creating "drop-ins" in
# the /etc/systemd/journald.conf.d/ directory. The latter is generally
# recommended. Defaults can be restored by simply deleting the main
# configuration file and all drop-ins located in /etc/.
#
# Use 'systemd-analyze cat-config systemd/journald.conf' to display the full config.
#
# See journald.conf(5) for details.

[Journal]
Storage=persistent
#Compress=yes
#Seal=yes
#SplitMode=uid
#SyncIntervalSec=5m
#RateLimitIntervalSec=30s
#RateLimitBurst=10000
SystemMaxUse=50M
#SystemKeepFree=
#SystemMaxFileSize=
#SystemMaxFiles=100
#RuntimeMaxUse=
#RuntimeKeepFree=
#RuntimeMaxFileSize=
#RuntimeMaxFiles=100
#MaxRetentionSec=0
#MaxFileSec=1month
#ForwardToSyslog=no
#ForwardToKMsg=no
#ForwardToConsole=no
#ForwardToWall=yes
#TTYPath=/dev/console
#MaxLevelStore=debug
#MaxLevelSyslog=debug
#MaxLevelKMsg=notice
#MaxLevelConsole=info
#MaxLevelWall=emerg
#MaxLevelSocket=debug
#LineMax=48K
#ReadKMsg=yes
#Audit=yes

```

```bash
# 创建一个真正掉电不丢失的目录
mkdir -p /root/journal
# 设置权限
chown root:systemd-journal /root/journal
chmod 2755 /root/journal

vim /etc/fstab
# 末尾添加
# /root/journal /var/log/journal none bind 0 0

#  尝试立即挂载
mkdir -p /var/log/journal
mount -a

# 刷新日志到新位置
journalctl --flush

#  检查当前日志是否已经写入新目录（应该能看到一串长长的十六进制目录）
ls /root/journal

# 重启系统
reboot

journalctl --list-boots
```

# git 保存账户密码

```bash
git config --global credential.helper store
```
