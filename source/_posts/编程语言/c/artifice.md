---
title: C artifice
date: 2025-05-12 15:04:47
tags: [c]
categories: [编程语言]
top_img:
cover:
---


# artifice

## 结构体字面量赋值

```c
struct hal_support_s
{
  uint16_t data0;
  uint16_t data1;
};

const struct hal_support_s hal_support =
{
  .data0 = 1,
  .data2 = 2
};

```

## HAL(Hardware Abstraction Layer，硬件抽象层)

```txt
┌──────────────────────────────┐
│         应用/业务层           │
│  (如主循环、业务逻辑、CLI等)   │
└─────────────▲────────────────┘
              │
              │ 调用
              │
┌─────────────┴────────────────┐
│         HAL 支持层           │
│   hal_support_s 函数指针表   │
│ ┌─────────────────────────┐ │
│ │ .cli_register           │ │
│ │ .logging_log            │ │
│ │ .statistics_reg_var     │ │
│ │ .strtok_lock/unlock     │ │
│ └─────────────────────────┘ │
└─────────────▲────────────────┘
              │
              │ 间接调用
              │
┌─────────────┴────────────────┐
│         驱动/硬件层           │
│ (如UART、SPI、GPIO、Timer等)  │
└──────────────────────────────┘
```
