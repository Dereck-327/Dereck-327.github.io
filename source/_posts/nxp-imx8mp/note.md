---
title: ARM-NXP        
data: 2025-10-25 11:06:20
tags: [NXP IMX8MP]             
categories: [note]             
description: nxp arm linux note
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# 开发笔记

## yocto常用指令

```bash
#%% 编译和清理
bitbake virtual/kernel 
-c cleanall # 会删除下载的文件，慎用。
-c clean   # 不会删除已下载的文件
bitbake u-boot-imx -c unpack -f # 强制重新解压 （有时候.git没有了就重新解压）

#%% 编译和部署
bitbake core-image-base  # 系统镜像
bitbake u-boot-imx # 编译uboot（u-boot-imx 是配方名）
bitbake -c deploy u-boot-imx #部署 uboot(但是好像没有重新生成)
bitbake linux-imx  # 编译内核

#%% 终端配置指定配方
bitbake-layers show-recipes | grep linux  # 找出配方名
# 以 linux-imx 为例打开 devshell（会把你放到 $S，内核源目录）
bitbake -c devshell linux-imx
```

## uboot 指令

```bash
mmc list # 查看存储设备
fatls mmc 2 # 查看mmc 设备2 fat格式
fatls mmc 2:1 # 查看mmc 设备2 第一个分区
ext4ls mmc 2:2 # 查看mmc 设备2的第二个分区 （已知是ext4格式）
ums 0 mmc 2:2 # 将mmc设备2的第二个分区暴露给pc

```

## gpt分区

FB: flash gpt gpt_partition_table.img

## 在uboot中对emmc分区 

```bash
mmc dev 2 # select emmc device
gpt write 0 # earse gpt

```