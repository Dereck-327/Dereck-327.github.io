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

## 任务优先级设置

```c
#define TASK_PRIORITY_PAYLOAD     (tskIDLE_PRIORITY + 4)
#define TASK_PRIORITY_HEARTBEAT   (tskIDLE_PRIORITY + 1)
/* ... */
int userTaskStack = TASK_PRIORITY_PAYLOAD;
TaskHandle_t userTask = xTaskCreateStatic(
                            &user_task_entry,       // task proc
                            "tcpip",                // task name
                            LOCAL_STACK_SIZE,       // stack depth
                            NULL,                   // task parameters
                            params->task_priority,  // priority
                            userTaskStack,
                            &user_task_control);
(void) userTask;  // 消除未使用变量警告的常用写法。
```

tskIDLE_PRIORITY 是 FreeRTOS 的空闲任务优先级，值为 0。  
TASK_PRIORITY_PAYLOAD 设为 4，比空闲任务高 4 级，表示 payload 任务优先级较高，能更快被调度执行。  
TASK_PRIORITY_HEARTBEAT 设为 1，比空闲任务高 1 级，表示 heartbeat 任务优先级较低，但仍高于空闲任务。

## 栈块大小

一个 stack block（栈块）以 StackType_t 类型为单位分配。  
FreeRTOS 的栈以 StackType_t 类型为单位分配，而在 ARM Cortex-M 等平台上，StackType_t 通常定义为 uint32_t，即 4 字节。



