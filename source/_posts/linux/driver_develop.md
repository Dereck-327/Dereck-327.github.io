---
title: 驱动开发
tags: [driver]
categories: [linux]
description: driver develop
date: 2025-05-12 10:59:41
top_img: /image/不良人李星云.jpg
cover: /image/不良人李星云.jpg
---


# 常用函数

### kmalloc 和 dev_kzalloc 分配内存

kmalloc : 需要手动清理， 收到释放 ; 但是 dev_kzalloc可以自动管理内存
devm系列的函数可以自动释放资源
kzalloc : 分配需要初始化为零的内存块

### module_platform_driver 和 platform_driver_register区别

module_platform_driver 和 platform_driver_register 是 Linux 内核中用于注册平台驱动的两种不同方式。这两种方式都用于注册一个平台设备驱动程序，但在使用上有一些细微的区别。
module_platform_driver : 不需要显示地调用注册函数(module_init module_exit)
platform_driver_register : 需要显示地调用注册函数

### 设计器初始化

允许在初始化结构体时显式指定某个字段的值。

```c
.field_name = value,
```

.field_name: 结构体的字段名。
value: 要赋给该字段的值。

# dma

## 一般框架

一个典型的 DMA 驱动包含以下模块：

1. 模块初始化与卸载
注册字符设备、创建设备节点。
初始化 DMA 控制器硬件。

2. 设备探测与资源分配
获取设备树（Device Tree）中的硬件信息（如寄存器地址、中断号）。
映射寄存器内存空间（ioremap）。

3. DMA 缓冲区管理
分配 DMA 可用的物理连续内存（dma_alloc_coherent）。

4. 文件操作接口
实现 open、release、ioctl、read、write 等文件操作函数。

5. DMA 传输控制
配置 DMA 传输源地址、目标地址、传输长度。
启动/停止 DMA 传输。

6. 中断处理
注册中断处理函数，处理 DMA 传输完成或错误中断。

7. 同步与并发控制
使用自旋锁（spinlock）或互斥锁（mutex）保护共享数据。

## 驱动匹配流程

1. 内核启动时解析设备树，遍历所有节点。
2. 当发现节点包含 compatible = "xlnx,dmaraw_chrdev" 时，搜索匹配的驱动。
3. 若驱动注册了相同的 compatible 值，则调用其 probe 函数。

# SysMonPSU

#
