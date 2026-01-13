---
title: ARM-NXP        
data: 2025-11-1 11:06:20
tags: [NXP IMX8MP]             
categories: [note]             
description: linux ptp
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---


# linuxptp v4.4

## BC 边界时钟

```bash
./ptp4l -f ./boundary_clock.conf -i eth0 -i eth1 -m
```

对应的配置文件如下

```conf
[global]
# 指定这是一个边界时钟
#clock_servant          1
boundary_clock_jbod     1

# 日志记录到标准输出，方便调试
logging_level           6
verbose                 1

# 网络传输模式 (通常是 2，代表 L2/Ethernet)
network_transport       L2

# 时间戳模式 (硬件时间戳)
time_stamping           hardware

# 使用哪个 PTP profile，默认即可
#ptp_dst_mac            01:1B:19:00:00:00
#p2p_dst_mac            01:80:C2:00:00:0E

# 接口列表
# 指定 eth0 和 eth1
[eth0]
[eth1]
```
