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

## gun 编译指令

### `__attribute__`

他是编译器的扩展，而不是c/c++的标准，所以在msvc上不可用。
作用: 向编译器传递“提示”或“要求”，告诉编译器：“请以某种特殊的方式处理这个函数/变量”。  
语法：`__attribute__` 的语法是使用双括号 (( ))，这有助于减少它与宏发生冲突的可能性。

#### 控制函数行为

- `__attribute__((weak))` - 弱符号  
  作用：将一个函数或变量声明为“弱符号”。如果链接器找不到同名的“强符号”（没有 weak 声明的符号），它不会报错，而是使用这个弱符号。如果找到了强符号，则弱符号会被忽略。  
  应用场景：定义库函数的默认实现，允许用户在自己的代码中覆盖（重写）该函数。

- `__attribute__((constructor)) / __attribute__((destructor))` - 构造与析构函数  
  作用：constructor 修饰的函数会在 main() 函数之前自动执行。destructor 修饰的函数会在 main() 函数结束之后或 exit() 被调用时自动执行。  
  应用场景：自动初始化库、注册清理函数。

- `__attribute__((noreturn))` - 无返回值  
  作用：告诉编译器这个函数不会返回给调用者（例如，函数内部会无限循环或直接终止程序）。这可以避免编译器生成不必要的返回代码，并消除关于“未返回值”的警告。  
  应用场景：exit(), abort(), panic(), 自定义错误处理函数。

- `__attribute__((always_inline)) / __attribute__((noinline))` - 内联控制  
  作用：强制编译器内联一个函数（always_inline）或禁止内联一个函数（noinline），覆盖编译器的优化决策。  
  应用场景：对性能有极致要求的微小函数（强制内联），或者为了调试方便不希望函数被内联。

#### 控制变量在内存中的布局

- `__attribute__((packed))` - 紧缩结构体  
  作用：告诉编译器取消结构体或联合体的内存对齐，所有成员紧挨着排列，不留空隙。这可以节省内存，但可能导致性能下降（在某些架构上访问未对齐的内存地址会引发硬件异常或需要多条指令）。  
  应用场景：处理网络数据包、硬件寄存器映射、与外部设备通信的协议，这些场景下数据的字节布局是严格定义的。

- `__attribute__((aligned(n)))` - 对齐控制  
  作用：指定变量或结构体必须以 n 字节的对齐方式分配在内存中。  
  应用场景：DMA 操作、缓存行对齐（避免伪共享）、特定硬件指令（如 SIMD）要求的数据对齐。

- `__attribute__((section("section_name")))` - 指定段  
  作用：指示编译器将某个函数或变量放置到自定义的段（Section） 中，而不是默认的 .text 或 .data 段。  
  应用场景：在链接脚本中精确控制代码和数据在内存中的位置，这在嵌入式开发中至关重要。例如，将关键代码放入快速 RAM 中，或将初始化数据放入 Flash 的特定区域。

  ```c
  // 将一个变量放入 .my_custom_section 段
  int my_var __attribute__((section(".my_custom_section"))) = 0xDEADBEEF;

  // 将一个函数放入 .ITCM 段（假设这是片上紧耦合内存）
  __attribute__((section(".ITCM"))) void critical_speed_function() {
      // ...
  }
  ```

#### 辅助编译器诊断

- `__attribute__((deprecated("message")))` - 弃用警告  
  作用：标记一个函数、变量或类型为已弃用。当其他代码使用它时，编译器会发出一个包含指定消息的警告。  
  应用场景：标记旧的 API，引导用户使用新的替代方案。

  ```c
  __attribute__((deprecated("Use new_function() instead.")))
  void old_function();
  ```

- `__attribute__((unused))` - 未使用警告消除  
  作用：告诉编译器“这个变量/函数我可能故意不使用，不要发警告”。

  ```c
  int some_callback(__attribute__((unused)) int event_id) {
    return 0;
  }
  ```

- `__attribute__((warn_unused_result))` - 检查返回值  
  作用：如果调用了一个返回值的函数，但其返回值没有被使用，编译器会产生一个警告。  
  应用场景：用于那些返回值非常重要、必须检查的函数（如内存分配、锁操作）。

  ```c
  __attribute__((warn_unused_result)) int allocate_memory(size_t size);
  // 如果调用 allocate_memory(100); 而不检查返回值，编译器会警告。
  ```
  
