---
title: RTOS System        
data: 2025-06-27 14:06:20
tags: [system]             
categories: [RTOS]             
description: RTOS system  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# RTOS 实时操作系统

## MOSI/MISO loopback

MOSI（Master Out Slave In）和 MISO（Master In Slave Out）是 SPI 总线的两条数据线。  
Loopback 指的是将 MOSI 和 MISO 物理上短接（或在软件中模拟），这样主机发送的数据会直接回到主机的接收端。  
在没有真实 SPI 从设备的情况下，可以用 loopback 测试 SPI 主机的收发流程、FIFO、回调等功能是否正常。  





