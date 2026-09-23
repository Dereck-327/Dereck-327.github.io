# lightic_app FreeRTOS V11.2 移植笔记

> 适用工程：BCM8915x Bare Metal / `lightic_app`
>
> 处理器：MCU0 Cortex-M7 DCLS、MCU1 Cortex-M7
>
> 内核版本：FreeRTOS Kernel V11.2.0
>
> 编译器端口：`portable/GCC/ARM_CM7/r0p1`
>
> 文档日期：2026-08-23

## 0. 先读这一节

这篇笔记有两个目的：

1. 解释把一个普通裸机 Cortex-M 工程移植到 FreeRTOS 的一般过程。
2. 结合 `lightic_app` 的真实代码，解释 BCM8915x 上的启动、异常汇编、中断优先级、双 MCU 关系、链接脚本和 SRAM 分配。

本文面向第一次接触 RTOS、Cortex-M 异常和链接脚本的读者。术语第一次出现时会解释，已经熟悉这些内容的读者可以直接跳到对应章节。

### 0.1 当前结论和验证边界

当前工程已经完成以下静态工作：

- FreeRTOS V11.2.0 内核、`heap_4` 和 Cortex-M7 端口已经加入构建。
- SVC、PendSV 和 SysTick 已经在 `exceptions.S` 的向量表中直接指向 FreeRTOS 处理函数。
- MCU0 和 MCU1 均能生成 ELF 和 raw binary。
- 已用 ELF、map、反汇编和向量表内容检查入口地址、段地址和 Thumb 位。
- 已加入 assert、malloc 失败、任务栈溢出和 CPU fault 的停机报告路径。

调度器已经在真实板卡上跑起来（首任务运行、日志可见）。但"能跑起来"和"验证完成"是两件事：
§19 的十个阶段里，tick 频率的墙上时间实测、两任务抢占、堆/栈高水位、fault 注入、
外设 IRQ 逐个恢复、DMA/cache 和双 MCU IPC 都还没有留下可复核的记录。
**上板走通哪一步，就把该阶段的实测数据补进 §19，否则下一个人仍然不知道边界在哪。**

尤其是 MCU1 的 `.bss` 清零、DTCM 数据装载、TCM ECC 初始化、MPU/cache 初始化以及
MCU0 释放 MCU1 的流程，必须结合实际 Boot ROM、PVT bootloader 或调试器行为确认。
其中 TCM ECC 和 VTOR 两项手册已有明确结论，见 §13.6。

### 0.2 最重要的四个认识

1. MCU0 和 MCU1 是两个独立的 Cortex-M7。通常应各运行一个独立 FreeRTOS 实例，它们不共享调度器、tick、TCB、任务栈或 `heap_4`。
2. MCU0 的 DCLS 是“双核锁步”安全结构。两个物理执行通道执行相同指令并比较结果，对软件只表现为一个处理器，不是两个可分别调度任务的 CPU，也不是 FreeRTOS SMP。
3. `exceptions.S` 负责“异常从哪里进入”；真正保存和恢复任务上下文的代码在 FreeRTOS 的 `port.c` 中。
4. 链接成功只表示地址能够排布，不表示启动加载器真的复制了初始化数据、清零了 BSS、初始化了 TCM ECC 或处理好了 D-cache。

### 0.3 阅读路线

全文较长，可以按目的阅读：

| 目的 | 建议章节 |
|---|---|
| 第一次理解 RTOS 和 Cortex-M | 第 1 到 3 章 |
| 理解当前工程如何编译和启动 | 第 4 到 5 章 |
| 逐行理解 `exceptions.S` | 第 6 章 |
| 理解首任务和上下文切换汇编 | 第 7 到 8 章 |
| 配置外设中断优先级 | 第 9 到 10 章 |
| 修改链接脚本和 SRAM 分配 | 第 11 到 15 章 |
| 把裸机业务拆成任务 | 第 16 到 17 章 |
| 构建、上板和故障排查 | 第 18 到 23 章 |
| 扩大 MCU0 可用 SRAM | §14.6 |
| 引入 printf / malloc 之前 | 第 25 章 |

## 1. 建立一个最小心智模型

### 1.1 裸机程序和 RTOS 程序的区别

典型裸机程序如下：

```c
int main(void)
{
    board_init();

    for (;;) {
        poll_uart();
        sample_adc();
        update_state();
    }
}
```

CPU 只有一条显式控制流。哪个函数执行太久，后面的工作就被推迟。

FreeRTOS 程序把工作拆成任务：

```text
ADC 任务  ----等待采样事件----> 处理数据 ----再次阻塞----
日志任务 ----等待日志数据----> 发送串口 ----再次阻塞----
空闲任务 --------------------------------------> 运行
```

调度器根据任务状态和优先级选择下一项工作。任务在没有事情可做时应该进入阻塞态，例如等待队列、通知、信号量或延时，而不是一直查询。

### 1.2 任务、调度器和 tick

- 任务：一个通常不会返回的 C 函数，以及它自己的栈和任务控制块 TCB。
- 调度器：FreeRTOS 内核中负责选择下一个运行任务的逻辑。
- tick：周期性时钟节拍。当前工程用 SysTick 每 1 ms 产生一次 tick。
- 抢占：更高优先级任务变为就绪时，可以抢占当前任务。
- 时间片：同优先级任务同时就绪时，按 tick 轮换。
- 阻塞：任务主动等待事件或时间，暂时不参与 CPU 竞争。
- 上下文：任务暂停后将来继续执行所需的寄存器和栈状态。

当前 `FreeRTOSConfig.h` 配置：

| 配置 | 当前值 | 含义 |
|---|---:|---|
| `configUSE_PREEMPTION` | 1 | 使用抢占式调度 |
| `configUSE_TIME_SLICING` | 1 | 同优先级任务使用时间片 |
| `configTICK_RATE_HZ` | 1000 | 1 ms 一个 tick |
| `configMAX_PRIORITIES` | 5 | 可用任务优先级为 0 到 4 |
| `configMINIMAL_STACK_SIZE` | 128 | 128 个 `StackType_t`，在本平台是 512 B |
| `configUSE_TIMERS` | 1 | 启用软件定时器 |

### 1.3 Cortex-M 的 Thread mode 和 Handler mode

Cortex-M 有两类执行模式：

- Thread mode：复位后的普通程序和 FreeRTOS 任务运行在这里。
- Handler mode：异常和中断处理函数运行在这里。

这里的“异常”是统称，包括 fault、SVC、PendSV、SysTick 和外设 IRQ。外设中断只是异常的一类。

### 1.4 MSP 和 PSP

Cortex-M 有两个栈指针：

- MSP，Main Stack Pointer，主栈指针。
- PSP，Process Stack Pointer，进程栈指针。

本工程中的典型使用方式：

| 阶段 | 使用的栈 |
|---|---|
| 复位后、启动代码和调度器启动前的 C 代码 | MSP |
| FreeRTOS 任务 | 每个任务自己的 PSP |
| 所有异常和中断处理 | MSP |

因此要同时预算两类内存：

- 链接脚本中的主栈，供启动代码和中断嵌套使用。
- 每个任务自己的栈，静态提供或从 `heap_4` 中动态分配。

一个常见误解是“启动调度器后 main 的栈会变成任务栈”。不会。FreeRTOS 启动首任务后，任务使用 PSP；MSP 仍保留给异常处理。

## 2. BCM8915x 上的两颗 Cortex-M7

### 2.1 MCU0

根据 `doc/Technical_ReferenceManual.txt`：

- 600 MHz Cortex-M7 DCLS。
- 16 KB I-cache、16 KB D-cache。
- 32 KB DTCM 物理空间。
- MCU0 的 ITCM 端口连接 Boot ROM。
- 带 MPU、NVIC 和 FPv5 单精度 FPU。

DCLS 可以理解为两套执行硬件同时做同一道题并比较答案。它提高故障检测能力，但 FreeRTOS 只看到一个 Cortex-M7，所以使用单核端口。

### 2.2 MCU1

- 600 MHz Cortex-M7，也支持 300 MHz 慢速模式。
- 16 KB I-cache、16 KB D-cache。
- 32 KB DTCM。
- 16 KB ITCM RAM。
- 带 MPU、NVIC 和 FPv5 单精度 FPU。

MCU1 通常不是自己从外部镜像独立启动。手册描述的典型流程是 MCU0 将 MCU1 启动代码装入系统 SRAM，设置 MCU1 的替代向量表地址 `VTOR_ALTVEC`，然后释放 MCU1 reset。实际产品启动链必须与 bootloader 实现一起核对。

### 2.3 公共 SRAM

芯片有 2 MB 全局 SRAM，分成四个 512 KB bank：

| Bank | 起始地址 | 大小 |
|---|---:|---:|
| SRAM bank 0 | `0x01000000` | 512 KB |
| SRAM bank 1 | `0x01080000` | 512 KB |
| SRAM bank 2 | `0x01100000` | 512 KB |
| SRAM bank 3 | `0x01180000` | 512 KB |

全局 SRAM 适合放代码、普通数据、任务栈、RTOS 堆和较大的缓冲区。手册建议通过 MPU 将其设置为 cacheable，以取得合理性能。

但“设置为 cacheable”会带来两个必须处理的问题：

- DMA 不会自动理解 CPU D-cache 中尚未写回的数据。
- MCU0 和 MCU1 的 cache 不会因为另一个 MCU 写了 SRAM 就自动一致。

所以 DMA buffer 和双 MCU 共享区必须另行设计 cache 维护或 MPU 属性，不能只把变量放进共享地址就算完成。

### 2.4 TCM 是什么

TCM 是 Tightly Coupled Memory，紧耦合存储器。它直接连接 CPU，访问确定、通常单周期、不经过普通 cache。

适合放：

- MSP 主栈。
- 对时延抖动非常敏感的少量数据。
- 经测试确实需要确定性的关键 ISR 代码或数据。

不适合默认放：

- 24 KB 的通用 FreeRTOS 堆。
- 大型日志缓存。
- 大量普通任务栈。
- 跨核或 DMA 共享数据。

TCM 容量很小，而且带 ECC。冷启动时不能假设 ECC 校验位已经有效。将代码或数据映射到 TCM 前，启动代码或上级加载器要完成 ECC 初始化，并在需要时完成代码/数据复制。

### 2.5 设备头给出的架构参数

CMSIS 设备头（`arm_prog_common/BCM8915x_CM7.h`）中关键定义如下：

| 宏 | 值 | 影响 |
|---|---:|---|
| `__CM7_REV` | `0x0000` | 处理器 revision 标识为 r0p0 |
| `__NVIC_PRIO_BITS` | 3 | 只有 3 个有效中断优先级位 |
| `__MPU_PRESENT` | 1 | 有 MPU |
| `__VTOR_PRESENT` | 1 | 向量表基址可重定位 |
| `__FPU_PRESENT` | 1 | 有 FPU |
| `__FPU_DP` | 0 | 无双精度 FPU |
| `__ICACHE_PRESENT` | 1 | 有 I-cache |
| `__DCACHE_PRESENT` | 1 | 有 D-cache |
| `__DTCM_PRESENT` | 1 | 有 DTCM |

其中 FPU 三项不是写死的，而是由 `BCM8915x_CM7_NO_FPU` /
`_SP_FPU` / `_DP_FPU` 三个宏选择，默认走 `_SP_FPU`（单精度）。
也就是说这张表反映的是"当前编译配置下的值"，不是纯硬件事实；
如果有人在命令行上定义了 `BCM8915x_CM7_DP_FPU`，头文件会声称有双精度 FPU，
而编译选项 `+nofp.dp` 说没有 —— 两边不一致时以硬件和 `-mcpu` 为准。
`__NVIC_PRIO_BITS = 3` 必须与 `FreeRTOSConfig.h` 的 `configPRIO_BITS` 保持一致，
这条已列入 §22 检查清单。

## 3. FreeRTOS 一般移植流程

下面是一套适用于大多数 Cortex-M 裸机工程的顺序。不要一开始就同时启用所有外设中断、cache、DMA 和几十个任务，否则故障很难定位。

### 第 1 步：确认 CPU 和 ABI

至少确认：

- CPU 架构和 revision，例如 Cortex-M7。
- 编译器，例如 GCC。
- 是否有 FPU，是单精度还是双精度。
- 浮点 ABI 是 `soft`、`softfp` 还是 `hard`。
- 中断优先级位数。
- CPU 时钟和 SysTick 时钟来源。
- 字节序和栈对齐要求。

本工程编译选项是：

```make
FPU_FLAGS = -mcpu=cortex-m7+nofp.dp -mfloat-abi=softfp
MCU0_CFLAGS = $(FPU_FLAGS) -mthumb -mthumb-interwork
```

含义：

- `cortex-m7+nofp.dp`：目标是 Cortex-M7，但不使用硬件双精度，因为芯片只有单精度 FPU。
- `softfp`：编译器可以生成硬件浮点指令，但函数参数传递仍采用 base ABI。
- `-mthumb`：生成 Cortex-M 所需的 Thumb 指令。

这里使用 `softfp` 是为了与现有 `liblidar.a` 的 ABI 保持兼容。只有在所有应用、驱动库和第三方库都用一致的 `hard` ABI 重编译后，才能整体改为 `hard`。ABI 混用可能在链接时暴露，也可能表现为运行时函数参数错误。

### 第 2 步：选择正确的 portable 端口

当前选择：

```text
lightic_app/freeRTOS/portable/GCC/ARM_CM7/r0p1/
    port.c
    portmacro.h
```

虽然设备头将 CM7 revision 标为 r0p0，FreeRTOS 目录中的 `ReadMe.txt` 明确说明 `r0p1` 端口可用于全部 Cortex-M7 revision，并包含 Cortex-M7 erratum 837070 的规避，所以这个选择是合理的。

选择端口时不要只看目录名“差不多”。错误的 CPU、编译器或 FPU 端口会直接影响异常返回、栈布局和临界区。

### 第 3 步：加入最小内核源文件

当前构建加入：

```text
tasks.c
list.c
queue.c
timers.c
stream_buffer.c
portable/MemMang/heap_4.c
portable/GCC/ARM_CM7/r0p1/port.c
```

只加入实际使用且配置已启用的模块。比如当前没有启用 co-routine，因此不需要 `croutine.c`。

### 第 4 步：编写 FreeRTOSConfig.h

至少确定：

- CPU 和 tick 频率。
- 最大任务优先级。
- 最小栈深度。
- 动态和静态分配策略。
- 堆大小。
- 中断优先级边界。
- 软件定时器设置。
- assert 和错误 hook。
- 要包含的可选 API。

配置应该来自板级事实和应用预算，不能从别的工程原样复制。

### 第 5 步：接管三个系统异常

Cortex-M FreeRTOS 端口至少需要：

| 异常 | FreeRTOS 函数 | 用途 |
|---|---|---|
| SVCall，槽 11 | `vPortSVCHandler` | 启动第一个任务 |
| PendSV，槽 14 | `xPortPendSVHandler` | 保存旧任务、选择并恢复新任务 |
| SysTick，槽 15 | `xPortSysTickHandler` | 产生 RTOS tick，必要时请求切换 |

可以在 C 头文件中用宏把传统 handler 名映射到这些函数，也可以像本工程一样直接把函数地址写入汇编向量表。不要同时存在两个互相冲突的路由方式。

### 第 6 步：建立堆、主栈和任务栈策略

需要先回答：

- `heap_4` 放在哪里？
- 主栈放在哪里、多大？
- 静态任务栈放在哪里？
- 动态任务栈从哪个堆分配？
- DMA 和共享缓存如何隔离？
- NOLOAD 段是否真的由启动代码初始化？

后文会结合两个链接脚本详细说明。

### 第 7 步：补齐 C runtime 初始化

标准 C 启动至少要保证：

1. 带非零初值的 `.data` 从镜像装载地址复制到运行地址。
2. `.bss` 清零。
3. 栈满足 8 字节对齐。
4. 必要时初始化 TCM ECC。
5. 配置 MPU、cache、FPU 和向量表。

有的 bootloader 或调试器会替应用完成一部分，但应用不能靠猜。用 ELF 调试加载成功，不代表 raw `.bin` 启动也正确。

### 第 8 步：先创建最小任务

第一次上板只保留：

- 一个能递增计数或打印短日志的任务。
- Idle task。
- 暂时不启用外设 IRQ 和 DMA。

确认 SVC 能启动首任务、SysTick 在走、PendSV 能切换两个任务后，再逐项恢复外设。

### 第 9 步：迁移中断

每个 ISR 都要审核：

- 优先级是否合法。
- 是否调用了普通任务 API。
- 是否改用 `...FromISR` API。
- 是否提供 `xHigherPriorityTaskWoken`。
- 退出前是否调用 `portYIELD_FROM_ISR()`。
- 外设 pending 和 NVIC pending 是否正确清除。

### 第 10 步：迁移裸机循环

把不同周期和事件来源的工作拆成任务。先识别“什么时候应该睡眠”，再决定任务优先级，而不是每个裸机函数都机械创建一个任务。

### 第 11 步：加入诊断保护

至少启用：

- `configASSERT`。
- `configCHECK_FOR_STACK_OVERFLOW = 2`。
- `vApplicationStackOverflowHook`。
- `configUSE_MALLOC_FAILED_HOOK = 1`。
- `vApplicationMallocFailedHook`。
- HardFault、MemManage、BusFault 和 UsageFault 报告。

### 第 12 步：分阶段验证

按“启动 -> 首任务 -> 两任务 -> 动态内存 -> 外设 IRQ -> DMA -> 双 MCU 共享”的顺序逐步增加复杂度。每一步都保留一个可观察现象，例如 UART 日志、GPIO 翻转、内存计数或调试器断点。

## 4. lightic_app 的构建组成

### 4.1 关键文件职责

| 文件 | 职责 |
|---|---|
| `lightic_app/Makefile` | 编译选项、内核源、双 MCU 目标、链接和 binary 导出 |
| `lightic_app/FreeRTOSConfig.h` | FreeRTOS 编译期配置 |
| `lightic_app/startup.S` | 复位入口、VTOR、MSP 和 fault 开关 |
| `lightic_app/exceptions.S` | 向量表、fault/未处理中断入口 |
| `lightic_app/mcu0.ld` | MCU0 的应用装载和内存布局 |
| `lightic_app/mcu1.ld` | MCU1 MEMORY 定义 |
| `lightic_app/firmware_common.ld` | MCU1 的具体 section 布局 |
| `lightic_app/src/os/lic_os.c` | 堆数组、任务启动、hook 和 fault C 报告 |
| `lightic_app/src/log/lic_log.c` | RTOS 日志任务、stream buffer 和轮询 panic 路径 |
| `lightic_app/src/hsadc/lic_hsadc.c` | HSADC 采集、FFT 和事件上报 |
| `lightic_app/src/lic_system.c` | 板级/时钟初始化封装 |
| `lightic_app/src/config.c` | 驱动配置表 |
| `lightic_app/main.c` | UART 日志初始化并启动 OS |

### 4.2 两个构建目标

`Makefile` 分别生成：

```text
bin/mcu0/lightic_mcu0.elf
bin/mcu0/lightic_mcu0.bin
bin/mcu1/lightic_mcu1.elf
bin/mcu1/lightic_mcu1.bin
```

MCU0 默认 `APP_LOAD_ADDR = 0x01040000`。MCU1 链接从 `0x01000000` 开始保留 header，并将 reset 放在 `0x01000100`、向量表放在 `0x01000400`。

两者不是同一个镜像在两个核上重复运行，而是分别链接的固件。

### 4.3 raw binary 导出需要特别注意

当前两个目标都使用：

```make
arm-none-eabi-objcopy -O binary --only-section=.text input.elf output.bin
```

这表示 raw `.bin` 只包含名为 `.text` 的输出段。`NOLOAD` 段本来就不会进入镜像；其他名字的 PROGBITS 段也会被 `--only-section=.text` 排除。

这个事实对 MCU1 非常重要：

- ELF 调试器可能按 program header 加载多个 segment。
- raw `.bin` 只带 `.text`。
- MCU1 DTCM 中出现的初始化数据不一定进入 raw `.bin`。
- `startup.S` 当前没有通用 `.data` copy 和 `.bss` zero。

因此必须检查真正烧写或加载链路，而不能只观察 ELF 构建成功。

## 5. 从复位到 main：startup.S 逐段说明

### 5.1 汇编文件开头

```asm
.syntax unified
.arch armv7-m
.cpu cortex-m7
.code 16
.text
```

- `.syntax unified`：使用统一 ARM/Thumb 汇编语法。
- `.arch armv7-m`：目标指令架构是 ARMv7-M。
- `.cpu cortex-m7`：进一步指定 Cortex-M7。
- `.code 16`：后续代码采用 Thumb 指令编码。Cortex-M 只执行 Thumb。
- `.text`：选择默认代码 section；文件后面还会显式切到 `.reset_func`。

这些是 GNU assembler 指令，不是 CPU 真正执行的指令。

### 5.2 外部符号

```asm
.extern BCM8915X_Main
.extern __main_stack__
.extern main_stack_size
```

- `BCM8915X_Main` 定义在 `main.c`。
- `__main_stack__` 和 `main_stack_size` 由链接脚本定义。
- `.extern` 告诉汇编器符号来自其他目标文件或链接脚本。

这里有一个容易误解的点：

```asm
LDR r0, =main_stack_size
```

`main_stack_size` 是链接器绝对符号，`LDR =symbol` 得到的是它的数值，例如 `0x800`，不是读取某个全局变量的内容。

### 5.3 设置 VTOR

```asm
LDR r0, =CORTEX_MX_VECTOR_TBL
LDR r1, =0xE000ED08
STR r0, [r1]
```

`0xE000ED08` 是 Cortex-M System Control Block 的 VTOR，Vector Table Offset Register。

三条指令的意思：

1. 将向量表地址装入 `r0`。
2. 将 VTOR 寄存器地址装入 `r1`。
3. 把向量表地址写到 VTOR。

CPU 之后查找异常入口时，会以这个表为准。地址必须满足处理器要求的对齐。本工程向量表大小和链接布局都是 `0x400` 对齐。

### 5.4 设置 MSP

```asm
LDR r0, =__main_stack__
MSR MSP, r0
```

`MSR` 将通用寄存器写入特殊寄存器。这里把链接脚本给出的主栈顶写入 MSP。

Cortex-M 的栈通常向低地址增长：

```text
高地址  __main_stack__       <- 初始 MSP
          可用主栈空间
低地址  主栈底
```

`__main_stack__` 应当是栈顶，不是栈底。

### 5.5 当前栈涂色代码实际上不可达

当前代码在设置 MSP 后立即：

```asm
b STACK_PAINT_EXIT
```

所以后面的 `0xA5A5A5A5` 主栈填充代码不会执行。当前不能依赖 A5 图案统计 MSP 主栈高水位。

如果未来启用这段代码，要理解：

- `PUSH` 会先递减 MSP 再写入数据。
- 填完整个栈后必须把 MSP 恢复为 `__main_stack__`。
- 必须保证不会写出链接脚本预留范围。
- 启用中断前完成涂色，否则中断使用 MSP 会破坏过程。

### 5.6 开启除零和可配置 fault

```asm
LDR r0, =CORTEX_CCR
LDR r1, [r0]
ORR r1, r1, #0x10
STR r1, [r0]

LDR r0, =CORTEX_SHCSR
LDR r1, [r0]
ORR r1, r1, #0x70000
STR r1, [r0]
```

- CCR 的 bit 4 是 `DIV_0_TRP`，开启后整数除零触发 UsageFault。
- SHCSR 的 bits 16、17、18 分别启用 MemManage、BusFault、UsageFault。

两处都是 read-modify-write，这一点是必须的，不是风格问题。Cortex-M7 的 CCR
bit 16/17/18 分别是 `DC`、`IC`、`BP`（见 `core_cm7.h` 的 `SCB_CCR_DC_Pos` 等）。
早期版本直接 `STR` 一个 `0x10` 常量进 CCR，会把 Boot ROM 或 bootloader 可能已经
使能的 D-cache、I-cache 和分支预测一起关掉，同时清掉 `UNALIGN_TRP` 和
`BFHFNMIGN`。这不是"以后可能出问题"，而是当次复位就生效的副作用：
一个只想打开除零陷阱的操作顺手关掉了两级 cache，性能变化还很难归因。
SHCSR 同理，盲写会清掉已有的 fault 使能和 pending 状态位。

结论：任何对 CCR、SHCSR、CPACR、FPCCR 这类"一个寄存器里塞了多组无关控制位"的
系统寄存器的写入，都应该先读后改再写。

### 5.7 进入 C 主函数

```asm
BLX BCM8915X_Main

LOOP:
    b LOOP
```

`BLX` 跳到 C 函数并设置返回地址。`BCM8915X_Main()` 初始化日志，然后调用 `Lic_OsStart()` 启动调度器。调度器正常启动后不应返回；如果返回，启动汇编最后会停在死循环。

### 5.8 当前 startup.S 没做什么

这比它做了什么更重要。当前没有看到：

- 从加载地址复制 `.data` 到运行地址。
- 将 `.bss` 清零。
- 初始化 TCM ECC。
- 建立完整 MPU region。
- 启用和维护 I-cache/D-cache。
- 针对 MCU1 ITCM 的复制。

这些工作可能由 Boot ROM、bootloader、ROM test 环境或调试器完成，也可能没有完成。移植人员必须对实际启动链逐项确认。

一个标准初始化骨架通常类似：

```c
copy_words(&_sidata, &_sdata, &_edata);
zero_words(&_sbss, &_ebss);
board_tcm_ecc_init();
board_mpu_init();
SCB_InvalidateICache();
SCB_EnableICache();
SCB_CleanInvalidateDCache();
SCB_EnableDCache();
```

上述只是顺序示意，具体 cache/MPU/TCM 操作必须按芯片手册和 CMSIS API 实现，不能直接把伪代码粘进产品。

## 6. exceptions.S 和向量表详细说明

### 6.1 什么是向量表

向量表是一个 32 位地址数组。CPU 复位或进入异常时，根据异常号取对应槽位。

前 16 个槽是 Cortex-M 固定系统异常：

| 槽号 | 名称 | 本工程入口 |
|---:|---|---|
| 0 | 初始 MSP | `__main_stack__` |
| 1 | Reset | `BCM_OS_RESET_HANDLER` |
| 2 | NMI | `BCM_OS_NMI_ISR_LOOP` |
| 3 | HardFault | `BCM_OS_HARD_FAULT_ISR` |
| 4 | MemManage | `BCM_OS_MPU_FAULT_ISR` |
| 5 | BusFault | `BCM_OS_BUS_FAULT_ISR` |
| 6 | UsageFault | `BCM_OS_USAGE_FAULT_ISR` |
| 7 到 10 | 保留 | `reserved_handler` |
| 11 | SVC | `vPortSVCHandler` |
| 12 | DebugMonitor | `BCM_OS_DEBUG_MONITOR_ISR` |
| 13 | 保留 | `reserved_handler` |
| 14 | PendSV | `xPortPendSVHandler` |
| 15 | SysTick | `xPortSysTickHandler` |

从槽 16 开始是外设 IRQ。当前 `exceptions.S` 共保留 240 个外部 IRQ 槽，加上 16 个系统槽，一共 256 槽：

```text
256 * 4 B = 1024 B = 0x400
```

这也是 MCU0 reset 固定在向量表后 `APP_LOAD_ADDR + 0x400` 的原因。

### 6.2 VECTOR_FN 和 VECTOR_SP

```asm
#define VECTOR_FN(x) .word x + 1
#define VECTOR_SP(x) .word x
```

`.word` 在当前位置放一个 32 位值。

Cortex-M 函数地址最低位 bit 0 用于表示 Thumb 状态。向量中的函数入口必须是奇数地址，也就是符号地址加 1。

但是槽 0 不是函数，它是初始 MSP 数据。因此：

```asm
VECTOR_SP(__main_stack__)          /* 正确：栈地址原值 */
VECTOR_FN(BCM_OS_RESET_HANDLER)    /* 正确：函数地址带 Thumb bit */
```

绝不能把初始 MSP 也写成 `__main_stack__ + 1`。FreeRTOS 的 `prvPortStartFirstTask()` 会从 `VTOR[0]` 重新读取初始 MSP；奇数 MSP 不满足栈对齐要求，可能在启动首任务附近触发 fault。原始裸机向量宏容易把所有槽都统一加 1，本工程已经将栈槽单独修正为 `VECTOR_SP`。

### 6.3 常用汇编声明

- `.section .vector_tbl`：切换到名为 `.vector_tbl` 的输入段，链接脚本负责放置它。
- `.global name`：导出符号，使链接器和其他目标文件可见。
- `.extern name`：声明符号定义在别处。
- `.thumb_func`：把紧随其后的符号标记为 Thumb 函数，帮助链接和调试工具正确解释。
- 标签后面的冒号：定义当前位置的符号，例如 `irq_handler:`。

### 6.4 为什么 FreeRTOS 必须接管 SVC、PendSV、SysTick

`exceptions.S` 中的关键三行：

```asm
VECTOR_FN(vPortSVCHandler)
...
VECTOR_FN(xPortPendSVHandler)
VECTOR_FN(xPortSysTickHandler)
```

它们形成以下关系：

```text
向量槽 11 -> vPortSVCHandler     -> 启动首任务
向量槽 14 -> xPortPendSVHandler  -> 任务上下文切换
向量槽 15 -> xPortSysTickHandler -> RTOS tick
```

当前 `configCHECK_HANDLER_INSTALLATION = 1`。FreeRTOS 在启动时会检查 VTOR 指向的向量表中，SVC 和 PendSV 是否安装为期望函数。这样可以尽早发现“编译进了 port.c，但向量仍指向旧裸机 handler”的错误。

`BCM_OS_SYSTICK_ISR` 的旧死循环标签仍在汇编文件中，但向量槽 15 已经不指向它，所以正常情况下不会进入。

### 6.5 fault 入口宏逐条解释

当前宏：

```asm
.macro FAULT_ENTRY id
    TST     lr, #4
    ITE     EQ
    MRSEQ   r0, MSP
    MRSNE   r0, PSP
    MOV     r1, #\id
    B       Lic_OsFaultReport
.endm
```

异常进入时，CPU 已自动把一部分寄存器压入某个栈。此时 `lr` 不再是普通函数返回地址，而是特殊值 `EXC_RETURN`。它编码了异常返回方式、使用哪个栈以及是否有扩展浮点帧。

逐条看：

1. `TST lr, #4`：将 `lr` 与 4 做按位测试，只更新条件标志，不保存结果。`EXC_RETURN` bit 2 为 0 表示异常前使用 MSP，为 1 表示异常前使用 PSP。
2. `ITE EQ`：Thumb 的 If-Then-Else 条件块。下一条按 EQ 执行，再下一条按 NE 执行。
3. `MRSEQ r0, MSP`：bit 2 为 0 时，把 MSP 复制到 `r0`。
4. `MRSNE r0, PSP`：bit 2 为 1 时，把 PSP 复制到 `r0`。
5. `MOV r1, #id`：把 fault 异常号放到 `r1`。
6. `B Lic_OsFaultReport`：直接跳到 C 报告函数，不设置返回地址。报告函数最后停机，不需要回来。

按 ARM AAPCS C 调用约定，前四个参数放在 `r0-r3`。所以这段汇编恰好构造：

```c
Lic_OsFaultReport(frame_pointer, exception_number);
```

### 6.6 硬件自动压栈内容

没有浮点扩展帧时，可以用下面的基本帧逻辑布局理解：

```text
frame[0]  r0
frame[1]  r1
frame[2]  r2
frame[3]  r3
frame[4]  r12
frame[5]  lr
frame[6]  pc       <- 发生异常时要执行或附近的指令地址
frame[7]  xPSR
```

`Lic_OsFaultReport()` 读取 stacked PC、LR、xPSR 和 R0，并读取：

- CFSR，Configurable Fault Status Register。
- HFSR，HardFault Status Register。
- MMFAR，MemManage Fault Address Register。
- BFAR，BusFault Address Register。

PC 可以交给 `addr2line` 映射回源码。但若 CFSR 报 `IMPRECISERR`，总线错误是异步上报的，stacked PC 可能不是实际发起访问的那条指令。

这里还有一个当前实现的诊断限制，必须特别说明。`FAULT_ENTRY` 只检查了 `EXC_RETURN` bit 2 来选择 MSP/PSP，没有检查 bit 4：

- bit 4 为 1：只有基本异常帧，`SP` 直接指向上表中的 R0，当前解析正确。
- bit 4 为 0：存在浮点扩展帧，`SP` 先指向 `s0-s15`、FPSCR 和保留字，基本帧在其后 `18 * 4 = 0x48` 字节处。

因此，如果 fault 发生时硬件已经为浮点上下文建立扩展帧，当前 `Lic_OsFaultReport()` 仍把原始 SP 当作基本帧，打印的 R0/LR/PC/xPSR 可能错位。CFSR/HFSR 仍是直接读取系统寄存器，不受这个索引问题影响。

若要让报告路径同时支持基本帧和扩展帧，汇编入口可在选出 MSP/PSP 后根据 bit 4 调整基本帧指针，思路如下：

```asm
    TST     lr, #4
    ITE     EQ
    MRSEQ   r0, MSP
    MRSNE   r0, PSP

    TST     lr, #0x10
    IT      EQ
    ADDEQ   r0, r0, #0x48   /* 扩展帧存在时，跳到基本帧 r0 */
```

更完整的实现还应把原始 `EXC_RETURN` 传给 C 层，记录是否有扩展帧，并评估 lazy stacking 状态。修改 fault 汇编后要分别用“未使用 FPU 的任务”和“已使用 FPU 的任务”做故障注入验证。

### 6.7 irq_handler 和 IPSR

未单独注册的外部 IRQ 默认进入：

```asm
MRS r1, IPSR
B   Lic_OsFaultReport
```

IPSR 是当前异常号。Cortex-M 的外部 IRQ 从异常号 16 开始，因此：

```text
外部 IRQn = IPSR - 16
```

例如日志曾观察到：

```text
unexpected interrupt (exc 57)
```

则：

```text
57 - 16 = IRQ 41 = UART1
```

这就是为什么 catch-all handler 打印 IPSR 比静默死循环更有用。

### 6.8 当前向量表实测值

以下数值来自当前工作区已有 ELF 的静态检查，不代表上板运行结果：

| 项目 | MCU0 | MCU1 |
|---|---:|---:|
| 向量表基址 | `0x01040000` | `0x01000400` |
| 初始 MSP | `0x20004800` | `0x20002000` |
| Reset 向量值 | `0x01040401` | `0x01000101` |
| SVC 向量值 | `0x01042E05` | `0x01003131` |
| PendSV 向量值 | `0x01042E4D` | `0x01003179` |
| SysTick 向量值 | `0x01042F8D` | `0x010032B9` |

函数向量最低位为 1 是正确的 Thumb 标记。初始 MSP 是偶数且按 8 字节对齐。

三个 handler 的绝对地址会随任何代码改动漂移，抄这张表没有意义；有意义的是
每次改完用 §18.7 的命令重新读一遍，确认**槽 0 是偶数**、**槽 11/14/15 指向
FreeRTOS 而不是旧的裸机死循环**。

## 7. FreeRTOS 如何启动第一个任务

### 7.1 总体调用链

```text
BCM8915X_Main()
  -> Lic_LogInit()
  -> Lic_OsStart()
       -> 创建日志任务和 HSADC 任务
       -> vTaskStartScheduler()
            -> xPortStartScheduler()
                 -> 检查 SVC/PendSV 向量
                 -> 设置 PendSV/SysTick 为最低优先级
                 -> 配置 SysTick
                 -> 启用 FPU 上下文支持和 lazy stacking
                 -> prvPortStartFirstTask()
                      -> 从 VTOR[0] 恢复 MSP
                      -> svc 0
                           -> vPortSVCHandler()
                                -> 从 pxCurrentTCB 恢复首任务上下文
                                -> 使用 PSP 异常返回到首任务
```

### 7.2 为什么用 SVC 启动

SVC 是 Supervisor Call。执行 `svc 0` 会同步进入 Handler mode。FreeRTOS 借此使用和普通异常返回相同的硬件机制，构造“恢复一个任务”的过程。

任务创建时，内核已经在任务栈上预先摆好一份仿真的寄存器现场，包括任务入口 PC、参数 R0、xPSR 和初始 EXC_RETURN。`vPortSVCHandler` 恢复这份现场后执行异常返回，CPU 就像“从一次中断回到该任务”一样开始运行。

真实 `vPortSVCHandler` 中几条关键指令是：

```asm
ldr   r3, =pxCurrentTCB
ldr   r1, [r3]
ldr   r0, [r1]
ldmia r0!, {r4-r11, r14}
msr   psp, r0
isb
mov   r0, #0
msr   basepri, r0
bx    r14
```

逐步含义：

1. 前三条先找到当前 TCB，再取 TCB 第一个成员中的任务栈顶。
2. `ldmia` 恢复内核创建任务时预置的 `r4-r11` 和 `EXC_RETURN`。
3. 把更新后的栈地址写到 PSP。
4. 清 BASEPRI，解除内核临界区屏蔽。
5. `bx r14` 不是普通函数返回，而是按 EXC_RETURN 完成异常返回；CPU 随后自动恢复基本帧并进入任务入口。

`vPortSVCHandler` 和 `xPortPendSVHandler` 都被声明为 `naked` 函数。这会禁止编译器自动生成普通 C 函数的入栈/出栈序言。上下文汇编必须完全掌控栈，若编译器擅自插入 `push`，任务帧布局就会被破坏。

### 7.3 TCB 为什么通常把栈顶放在第一个成员

`pxCurrentTCB` 指向当前任务的 TCB。Cortex-M 端口汇编首先从 TCB 取任务栈顶，再保存或恢复寄存器。端口依赖 TCB 的栈指针布局，这是内核和 portable 层之间的重要契约，不应由应用修改。

## 8. SysTick、PendSV 和上下文切换

### 8.1 SysTick 重装值

当前：

```c
configCPU_CLOCK_HZ = 600000000
configTICK_RATE_HZ = 1000
```

SysTick 用的是 CPU 时钟，这一点不是假设：`port.c` 里 `configSYSTICK_CLOCK_HZ`
未定义时默认等于 `configCPU_CLOCK_HZ`，且此时 `portNVIC_SYSTICK_CLK_BIT_CONFIG`
就是 `portNVIC_SYSTICK_CLK_BIT`（CLKSOURCE = 内核时钟）。所以：

```text
reload = 600000000 / 1000 - 1
       = 599999
```

SysTick LOAD 是 24 位，最大 `0xFFFFFF = 16777215`，所以 599999 可容纳。

真正要确认的是硬件一侧：如果 MCU1 运行在手册所说的 300 MHz 慢速模式而配置仍写
600 MHz，tick 会慢一倍，所有基于 tick 的超时也会错一倍。这一条只能靠 §19 阶段 4
的墙上时间实测排除，静态检查看不出来。

### 8.2 一次 tick 中断发生什么

`xPortSysTickHandler()` 的逻辑可概括为：

1. 用 BASEPRI 屏蔽会调用内核的较低优先级中断。
2. 调用 `xTaskIncrementTick()` 更新内核 tick 和延时列表。
3. 如果更高优先级任务因超时而就绪，或者需要同优先级时间片，设置 PendSV pending。
4. 恢复 BASEPRI 并退出。

SysTick 不直接在自己的 handler 中完成整个任务切换。它只请求 PendSV，这样所有上下文切换集中在最低优先级异常中完成。

### 8.3 为什么 PendSV 应为最低优先级

PendSV 是“可挂起的系统服务”。将它设为最低优先级有两个好处：

- 高优先级外设 ISR 可以先完整执行。
- 最后一个 ISR 退出后，再统一切换任务，避免在中断嵌套中反复保存任务上下文。

当前 PendSV 和 SysTick 的逻辑优先级都是 7，即寄存器值 `0xE0`。

### 8.4 谁保存哪些寄存器

异常进入时硬件自动保存：

```text
r0-r3, r12, lr, pc, xPSR
```

PendSV 软件保存：

```text
r4-r11, EXC_RETURN
```

如果任务使用过 FPU，端口还按 EXC_RETURN 状态保存 `s16-s31`。`s0-s15` 和 FPSCR 属于 Cortex-M 浮点扩展异常帧，由硬件异常入栈和 lazy stacking 机制处理。

合起来，任务下次恢复时拥有完整可继续执行的上下文。

### 8.5 PendSV 的核心过程

用伪汇编理解：

```asm
mrs   r0, psp              /* r0 = 当前任务栈顶 */
isb
stmdb r0!, {r4-r11, r14}   /* 软件寄存器压栈，实际端口还处理 FPU */
str   r0, [pxCurrentTCB]   /* 保存旧任务的新栈顶 */

bl    vTaskSwitchContext   /* pxCurrentTCB 改为新任务 */

ldr   r0, [pxCurrentTCB]   /* 取新任务栈顶 */
ldmia r0!, {r4-r11, r14}   /* 恢复软件寄存器 */
msr   psp, r0
bx    r14                  /* 按 EXC_RETURN 退出异常 */
```

指令含义：

- `MRS r0, PSP`：读取任务栈指针。
- `STMDB r0!, {...}`：先递减地址再连续存储，用于向下增长的压栈。
- `LDMIA r0!, {...}`：从当前地址连续读取后递增，用于出栈。
- `BL`：调用 C 调度函数并保留返回地址。
- `MSR PSP, r0`：切换到新任务的栈。
- `BX r14`：这里的 r14 是 EXC_RETURN，CPU 根据它恢复硬件异常帧并回到 Thread mode。

真实 `port.c` 还包含 BASEPRI、erratum 837070 和浮点路径，不能用上面的教学伪代码替换真实实现。

对照本工程真实 `xPortPendSVHandler`，可以再细分为以下步骤：

1. `mrs r0, psp` 取得旧任务栈顶；异常 handler 自身仍使用 MSP。
2. `tst r14, #0x10` 检查 EXC_RETURN bit 4。bit 4 为 0 说明任务有浮点上下文，`vstmdbeq r0!, {s16-s31}` 保存高浮点寄存器。
3. `stmdb r0!, {r4-r11, r14}` 保存被调用者保存寄存器和 EXC_RETURN。
4. `str r0, [r2]` 把新的 PSP 栈顶写回旧任务 TCB 的第一个成员。
5. `stmdb sp!, {r0, r3}` 暂时把两个工作寄存器压到 handler 使用的 MSP 上，以便跨 C 函数调用保留。
6. 把 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 写入 BASEPRI，在内核选择任务期间屏蔽允许调用内核的那组 IRQ。
7. 调用 `vTaskSwitchContext()`，它把 `pxCurrentTCB` 改为最高优先级就绪任务。
8. 清 BASEPRI，恢复 MSP 上的工作寄存器，从新 TCB 取栈顶。
9. `ldmia` 恢复 `r4-r11` 和 EXC_RETURN；若 bit 4 为 0，再用 `vldmiaeq` 恢复 `s16-s31`。
10. 写 PSP，执行 `bx r14`。CPU 自动恢复剩余硬件帧并回到新任务。

真实端口在写 BASEPRI 前后还执行：

```asm
cpsid i
msr   basepri, r0
dsb
isb
cpsie i
```

`cpsid i`/`cpsie i` 短暂设置/清除 PRIMASK，配合 `dsb` 和 `isb`，是 Cortex-M7 erratum 837070 的规避路径之一。`dsb` 保证此前显式内存访问完成，`isb` 刷新指令执行流水线对控制状态变化的观察。不要为了“减少几条指令”删除这组代码。

汇编末尾的 `.ltorg` 要求汇编器在合适位置生成 literal pool。像 `ldr r3, =pxCurrentTCB` 这样的伪指令可能通过 PC 相对方式从 literal pool 取完整地址，`.ltorg` 让这些常量保持在可寻址范围内。

### 8.6 完整切换时序

```text
当前任务运行
   |
   | SysTick 或 ISR 使更高优先级任务就绪
   v
设置 PendSV pending
   |
   | 等所有更高优先级异常退出
   v
PendSV 保存旧任务 PSP 上下文
   |
   v
vTaskSwitchContext() 选择新任务
   |
   v
PendSV 从新任务 PSP 恢复上下文
   |
   v
异常返回，新任务继续运行
```

### 8.7 FPU 和 lazy stacking

Cortex-M7 FPU 上下文很大。如果每次异常都无条件保存所有浮点寄存器，开销很高。lazy stacking 会等到异常处理代码真的需要浮点现场时再完成部分压栈。

应用层要注意：

- ISR 中尽量避免浮点运算。
- 编译选项和所有库 ABI 必须一致。
- 任务栈预算要考虑使用浮点时更大的最坏异常帧。
- fault 分析时要结合 EXC_RETURN 判断是否存在扩展浮点帧，但基本帧中的 R0、LR、PC、xPSR 逻辑位置仍由架构规则决定。

## 9. 中断优先级：最容易出错的部分

### 9.1 数值越小，硬件优先级越高

BCM8915x 只实现 3 个 NVIC 优先级位，因此 CMSIS 逻辑优先级是 0 到 7。它们存放在 8 位寄存器的高 3 位：

| CMSIS 逻辑值 | 寄存器值 | FreeRTOS 含义 |
|---:|---:|---|
| 0 | `0x00` | 最高，不能调用 FreeRTOS API |
| 1 | `0x20` | 很高，不能调用 FreeRTOS API |
| 2 | `0x40` | 可调用 FromISR API 的最高边界 |
| 3 | `0x60` | 可调用 FromISR API |
| 4 | `0x80` | 可调用 FromISR API |
| 5 | `0xA0` | 可调用 FromISR API |
| 6 | `0xC0` | 可调用 FromISR API |
| 7 | `0xE0` | 最低，PendSV/SysTick 使用 |

“逻辑值更大”表示“硬件优先级更低”，这是新手最容易反过来的地方。

### 9.2 当前边界

```c
#define configPRIO_BITS                              3
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 2
#define configKERNEL_INTERRUPT_PRIORITY               0xE0
#define configMAX_SYSCALL_INTERRUPT_PRIORITY          0x40
```

规则：

- 逻辑优先级 0 或 1 的 ISR 绝不能调用任何 FreeRTOS API。
- 逻辑优先级 2 到 7 的 ISR 可以调用名字以 `FromISR` 结尾的 API。
- ISR 不能调用普通的 `xQueueSend()`、`xSemaphoreTake()`、`vTaskDelay()` 等任务 API。
- NVIC 复位默认优先级通常是 0。新启用的 IRQ 如果不显式设置优先级，恰好处于禁止调用 FreeRTOS API 的最高优先级。

这里的规则主要约束应用外设 ISR。FreeRTOS 自己拥有的 SVC 是一个特殊系统异常，端口会故意把它设为逻辑优先级 0，并在其中恢复首任务；这不表示应用可以把普通外设 IRQ 设为 0 后调用内核 API。

### 9.3 BASEPRI 是什么

FreeRTOS 在 Cortex-M 上使用 BASEPRI 建立内核临界区。设置 `BASEPRI = 0x40` 后，优先级数值等于或大于 `0x40` 的异常会被暂时阻挡，而 `0x00`、`0x20` 这些更高优先级异常仍可响应。

所以高优先级 ISR 可以获得较低延迟，但它不能访问正在被内核临界区保护的数据结构，也就不能调用 FreeRTOS API。

### 9.4 正确的 ISR 通知范式

```c
void ADC_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    adc_clear_interrupt();
    vTaskNotifyGiveFromISR(xAdcTask, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

`xHigherPriorityTaskWoken` 的意思是：这次 ISR 操作是否唤醒了一个比当前任务优先级更高的任务。`portYIELD_FROM_ISR()` 会在需要时请求 PendSV，使 ISR 退出后尽快运行该任务。

### 9.5 启用一个外设 IRQ 的推荐顺序

1. 先关闭外设内部的 interrupt enable。
2. 安装或确认向量入口。
3. 清除外设状态寄存器中的 pending 条件。
4. 清除 NVIC pending。
5. 使用 CMSIS `NVIC_SetPriority(IRQn, logical_priority)` 设置优先级。
6. 使用 `NVIC_EnableIRQ(IRQn)` 启用 NVIC。
7. 最后开启外设内部 interrupt enable。

这样可以避免旧 pending 状态在 handler 和优先级准备好之前立即触发。

### 9.6 当前工程的 bring-up 防护

`Lic_OsQuiesceInterrupts()` 在启动调度器前禁用并清除所有外部 IRQ，并把外部 IRQ 优先级写成最低的 `0xE0`。它用于 bring-up 阶段避免裸机驱动遗留 IRQ 突然进入统一 catch-all handler。

日志使用 UART 驱动的轮询路径。UART 驱动初始化可能开启外设中断，而本工程没有把 UART IRQ 接到完整 RTOS handler，因此日志代码同时关闭 UART 外设 interrupt enable 和对应 NVIC line。UART0/1 分别是外部 IRQ 40/41。

这只是调通阶段的保护，不是最终中断架构。将来真正使用某个外设 IRQ 时，应按上一节顺序重新注册、设优先级、清 pending 并启用。

## 10. FreeRTOSConfig.h 当前配置解读

### 10.1 调度相关

- `configMAX_PRIORITIES = 5`：任务优先级是 0 到 4。数值越大，任务优先级越高，这与 NVIC “数值越小越高”相反。
- `configIDLE_SHOULD_YIELD = 1`：空闲优先级的其他任务就绪时，Idle task 主动让出 CPU。
- `configUSE_PORT_OPTIMISED_TASK_SELECTION = 1`：利用 CM7 的 CLZ 指令快速寻找最高就绪优先级。
- `configTICK_TYPE_WIDTH_IN_BITS = TICK_TYPE_WIDTH_32_BITS`：tick 是 32 位。

在 1000 Hz 下，32 位 tick 大约每 49.7 天回绕。FreeRTOS 延时 API 按无符号回绕规则处理，不要自己用错误的有符号比较判断超时。

### 10.2 内存相关

```c
configSUPPORT_DYNAMIC_ALLOCATION = 1
configSUPPORT_STATIC_ALLOCATION  = 1
configKERNEL_PROVIDED_STATIC_MEMORY = 1
configAPPLICATION_ALLOCATED_HEAP = 1
configTOTAL_HEAP_SIZE = 24 * 1024
```

含义：

- 同时允许动态任务和静态任务。
- Idle task 和 timer task 的静态内存由 V11.2 内核提供，不要求应用实现两个 get-memory 回调。
- 应用定义 `ucHeap[]`，`heap_4.c` 通过 extern 使用它。

### 10.3 功能开关

当前启用 mutex、counting semaphore、stream buffer、task notification 和 software timer。Timer service task：

- 优先级为 `configMAX_PRIORITIES - 1 = 4`。
- 队列长度 8。
- 栈深度 256 个 `StackType_t`，即 1024 B。

软件定时器回调运行在同一个 timer task 中，因此回调不能长时间阻塞。否则会推迟其他定时器命令和回调。

### 10.4 诊断开关

- `configCHECK_FOR_STACK_OVERFLOW = 2`：在任务切换时检查栈边界图案。它能发现很多溢出，但不是内存保护；一次严重越界可能先破坏其他内存。
- `configUSE_MALLOC_FAILED_HOOK = 1`：动态分配失败调用 hook。
- `configASSERT`：失败进入 `Lic_OsAssertFailed()` 并记录文件、行号。
- `configCHECK_HANDLER_INSTALLATION = 1`：启动时检查 SVC/PendSV 路由。

## 11. SRAM、TCM、堆和栈规划

### 11.1 先区分四类“内存”

初学者经常把它们都叫“堆栈”，实际完全不同：

| 名称 | 谁管理 | 用来做什么 |
|---|---|---|
| MSP 主栈 | 链接脚本 + CPU | 启动代码和中断嵌套 |
| 任务栈 | FreeRTOS/应用 | 每个任务的局部变量、调用链和上下文 |
| `heap_4` 堆 | FreeRTOS | 动态 TCB、动态任务栈、队列等内核对象 |
| C library heap | libc / `_sbrk` | `malloc()` 等，当前不应与 `heap_4` 混为一谈 |

FreeRTOS 的 `pvPortMalloc()` 使用 `heap_4`，不等于 C 库的 `malloc()`。两套堆同时存在时必须分别规划，当前工程主要使用 FreeRTOS 堆。

### 11.2 总预算公式

```text
总 SRAM =
    可加载代码和只读常量
  + 初始化数据
  + BSS
  + MSP 主栈
  + 静态任务栈和静态 TCB
  + heap_4 内存池
  + 从 heap_4 分配的动态任务栈/TCB/队列/定时器
  + DMA buffer
  + 驱动 scratch
  + 双 MCU 共享区
  + 对齐空洞
  + 安全余量
```

链接器只会计算静态 section。`heap_4` 内部未来会分配什么，需要用运行时统计和任务设计表补充。

### 11.3 栈深度单位不是字节

FreeRTOS 任务创建 API 的 stack depth 单位是 `StackType_t` 数量。当前 CM7 上：

```text
sizeof(StackType_t) = 4 B
```

所以：

| 配置或任务 | 深度 | 字节 |
|---|---:|---:|
| `configMINIMAL_STACK_SIZE` | 128 | 512 B |
| 日志任务 `2 * 128` | 256 | 1024 B |
| HSADC 任务 `3 * 128` | 384 | 1536 B |
| Timer task `2 * 128` | 256 | 1024 B |
| Idle task | 128 | 512 B |

不要把 `xTaskCreate(..., 512, ...)` 理解成 512 B；在本平台它是 2048 B。

### 11.4 heap_4 的特点

当前使用 `portable/MemMang/heap_4.c`：

- 支持分配和释放。
- 会合并相邻空闲块，降低碎片。
- 不是实时常数时间算法。
- 一般管理一个连续的 `ucHeap[]`。
- 分配按 `portBYTE_ALIGNMENT = 8` 对齐。

`heap_4` 不会自动跨 MCU，也不会自动跨两个不连续内存区。如果要使用多个不连续 region，应评估 `heap_5`，但当前没有这种需要。

### 11.5 当前 ucHeap 和 .freertos_heap

`lic_os.c` 定义：

```c
uint8_t ucHeap[configTOTAL_HEAP_SIZE]
    __attribute__((section(".freertos_heap")));
```

`configTOTAL_HEAP_SIZE` 是 24 KB。但是当前链接输出段 `.freertos_heap` 实际不只 24 KB，因为日志模块也将静态对象放在同一输入 section：

- `ucHeap`：24 KB。
- `g_taskStack`：1024 B。
- `g_streamStorage`：1025 B。
- 对齐填充：7 B。

所以当前 `.freertos_heap` 输出段：

```text
0x6808 = 26632 B
```

需要区分：

- “FreeRTOS 动态堆池大小”是 24 KB。
- “链接器中名为 `.freertos_heap` 的总输出段”是 26632 B。

段被标成 `NOLOAD` 只表示不进入镜像 payload，不表示运行时不占 SRAM。

### 11.6 静态和动态任务

当前日志任务使用 `xTaskCreateStatic()`，它的 TCB 和栈由应用提供。HSADC 任务使用 `xTaskCreate()`，TCB 和 1536 B 栈从 `heap_4` 动态分配。

静态分配的优点：

- 链接时就能看到主要内存占用。
- 创建不会因堆不足失败。
- 适合长期存在的关键任务。

动态分配的优点：

- 代码简洁。
- 对生命周期变化的对象灵活。

产品中可以混用，但必须检查所有创建函数的返回值，并在运行时记录最小剩余堆和栈高水位。

## 12. MCU0 链接脚本详解

### 12.1 MEMORY 区域

`mcu0.ld`：

| 区域 | 地址 | 长度 | 当前用途 |
|---|---:|---:|---|
| `DTCM_PRIV` | `0x20000000` | 16 KB | 预留，当前脚本未放通用对象 |
| `DTCM_BSS` | `0x20004000` | 16 KB | MSP 主栈 |
| `SRAM` | `0x01040000` | 256 KB | 应用代码、数据和 RTOS 堆 |
| `SRAM_IPC_SCR` | `0x011E0000` | 16 KB | IPC scratch 预留 |
| `FLASH` | `0x08000000` | 16 MB | 仅声明，当前无 section |
| `ARM_DUMMY` | `0x10000000` | 16 KB | 见下方警告 |

芯片物理 DTCM 是连续 32 KB。脚本只是把它拆成两个 16 KB 工程窗口，不能据此说 MCU0 物理 DTCM 只有 16 KB。

`ARM_DUMMY` 这个名字有误导性，它不是"废纸篓"。按 TRM 内存映射，`0x10000000`
是 Local Peripherals (AXI) 窗口，即真实的外设寄存器空间。早期脚本把
`.ARM.exidx` / `.ARM.extab` 放进这里，链接结果就是多出一个指向外设空间的
LOAD segment（8 B）—— `objcopy --only-section=.text` 恰好把它滤掉了，
所以一直没出事，但任何按 program header 加载的调试器都会往外设写 8 个字节。
现在两个脚本都改成 `/DISCARD/` 掉这两个段：镜像是 `-nostdlib`、
无异常处理，没有任何东西需要 unwind 表。

`SRAM` 那个 256 KB 不是硬件限制，见 §14.6。

### 12.2 .text 不是只有代码

MCU0 的 `.text` 输出段包含：

```ld
KEEP(*(.vector_tbl*))
KEEP(*(.reset_func*))
*(.text*)
*(.rodata*)
*(.bss*)
*(COMMON)
*(.data*)
*(.SRAM*)
```

也就是说 MCU0 将代码、常量、普通 BSS 输入段、DATA 输入段和 `.SRAM` 数据合并为一个可加载 payload。`__bss_start__` 这个名字容易让人以为这部分运行时一定由 startup 清零，实际上它已被放进 PROGBITS `.text` payload，内容由生成的镜像携带。

这种布局不是标准 C runtime 布局，但与“bootloader 一次复制完整 application payload”契约相配。修改时必须同时理解 loader。

### 12.3 向量表和 reset 固定关系

脚本断言：

```ld
ASSERT(__text_start__ == APP_LOAD_ADDR,
       "MCU0 payload must start at APP_LOAD_ADDR");
ASSERT(BCM_OS_RESET_HANDLER == (APP_LOAD_ADDR + 0x400),
       "MCU0 reset must follow the 0x400-byte vector table");
```

当前默认：

```text
0x01040000  0x400 B 向量表
0x01040400  Reset Handler
```

如果向量表条目数量变化，第二个断言会立即让链接失败，防止 bootloader 仍跳固定偏移却跳到错误代码。

### 12.4 主栈

```ld
main_stack_size = 0x800;

.bss (NOLOAD) :
{
    . = ALIGN(8);
    . = . + main_stack_size;
    __main_stack__ = .;
} > DTCM_BSS
```

结果：

```text
主栈底  0x20004000
主栈顶  0x20004800
大小    2048 B
```

2 KB 是否足够取决于：

- 最大中断嵌套深度。
- 每个 ISR 的局部变量和调用链。
- 是否在 ISR 中使用浮点。
- fault 处理和日志函数在 MSP 上的栈消耗。

不能只看启动代码几乎不用栈就判定足够。

### 12.5 当前 MCU0 实际布局

以下来自当前已有 ELF/map：

| 地址 | 内容 |
|---:|---|
| `0x01040000` | 向量表 |
| `0x01040400` | Reset Handler |
| `0x01044C80` | `__text_end__` / `__bss_start__`（payload 内的数据部分起点） |
| `0x01045658` | `ucHeap` 开始 |
| `0x0104B658` | 静态日志任务栈开始 |
| `0x0104BA58` | 日志 stream storage 开始 |
| `0x0104BE60` | `.freertos_heap` 结束（也是 `end` / `_end`） |
| `0x20004000` | MSP 主栈底 |
| `0x20004800` | 初始 MSP |

当前 section 大小：

| section | 大小 |
|---|---:|
| `.text` | 22104 B |
| `.freertos_heap` | 26632 B，NOBITS |
| 主栈 `.bss` | 2048 B，NOBITS |
| raw `.bin` | 22104 B |

这些数值会随代码变化。每次正式发布都应从该版本 ELF/map 重新生成，而不是长期抄文档。

注意 `.text` 22104 B 里包含了 `__bss_start__` 之后的整个数据区（`0x01044C80`
到 `0x01045658`，约 2.5 KB）。这是 §12.2 那种"把 BSS/DATA 并进可加载 payload"
布局的直接代价：BSS 越大，FS 镜像越大，而且里面绝大部分是零。
若哪天镜像大小成为约束，第一件事就是把真正的 `.bss` 拆成 NOLOAD 段并在
startup 里清零，而不是去压缩代码。

## 13. MCU1 链接脚本详解

### 13.1 MEMORY 区域

`mcu1.ld` 定义：

| 区域 | 地址 | 长度 |
|---|---:|---:|
| `DTCM` | `0x20000000` | 32 KB |
| `SRAM` | `0x01000000` | 2 MB - 32 KB |
| `ROM_DRV_SCR` | `0x011F8000` | 32 KB |

SRAM 少掉的最后 32 KB 由 `ROM_DRV_SCR` 单独命名，避免普通 section 与 ROM driver scratch 重叠。

### 13.2 header、reset 和向量表

`firmware_common.ld` 首先保留：

```text
0x01000000 .. 0x010000FF  0x100 B header
0x01000100                 Reset Handler
0x01000400                 向量表
```

脚本先放 `.reset_func`，然后将位置推进到 SRAM 输出段内偏移 768。因为 `.text` 自身从 `0x01000100` 开始，所以向量表落在绝对地址 `0x01000400`。

这与 MCU0 的顺序相反：

- MCU0：向量表在前，reset 在 `+0x400`。
- MCU1：reset 在 `0x01000100`，向量表在 `0x01000400`。

不要给两个目标强行套用同一个入口假设。

### 13.3 MCU1 的 SRAM BSS 和 RTOS 堆

`.bss` 放在全局 SRAM，包含输入 BSS 后又额外推进 `bss_size = 0x4000`。脚本原注释写“8KB”而 `0x4000` 实际是 16 KB，注释已改正；这类"注释和数值不一致"的地方一律以数值和 map 为准。

随后 `.freertos_heap (NOLOAD)` 也放在全局 SRAM。这一选择合理，因为 DTCM 只有 32 KB，且已经被主栈、数据、patch 和 debug log 划分。

### 13.4 MCU1 的 DTCM

当前 DTCM 规划：

```text
0x20000000 .. 0x20001FFF  8 KB MSP 主栈
0x20002000 .. 0x20003FFF  8 KB .data 预留
0x20004000 ..            少量 .SRAM / 孤儿初始化数据
0x20007200 .. 0x200079FF  2 KB patch 区
0x20007A00 .. 0x20007FFF  1536 B debug log 区
0x20008000                DTCM 末端
```

`main_stack_size = 0x2000`，所以初始 MSP 为 `0x20002000`。

注意 `.data` 中的：

```ld
. = . + data_size;
```

只是保留地址空间，不等于把任何初始化数据复制进去。

### 13.5 当前 MCU1 实际布局

| 地址 | 内容 |
|---:|---|
| `0x01000000` | header reservation |
| `0x01000100` | Reset Handler |
| `0x01000400` | 向量表 |
| `0x01005008` | `.bss` 开始 |
| `0x01009998` | `ucHeap` 开始 |
| `0x0100F998` | 静态日志任务栈 |
| `0x0100FD98` | 日志 stream storage |
| `0x010101A0` | `.freertos_heap` 结束（也是 `end` / `_end`） |
| `0x011F8000` | ROM driver scratch |
| `0x20000000` | MSP 主栈底 |
| `0x20002000` | 初始 MSP |
| `0x20002000` | 8 KB data reservation 起点 |
| `0x20007200` | patch 区 |
| `0x20007A00` | debug log 区 |
| `0x20008000` | DTCM 末端 |

当前 section 静态统计：

| section | 大小/属性 |
|---|---|
| `.text_hdr` | 256 B，NOBITS |
| `.text` | 20224 B（含 `.data*` / `.SRAM*`） |
| `.bss` | 18832 B，NOBITS |
| `.freertos_heap` | 26632 B，NOBITS |
| `.rom_drv_scr` | 32768 B，NOBITS |
| `.privileged.data` | 8192 B，NOBITS |
| `.data` | 8192 B，NOBITS（纯地址预留） |
| raw `.bin` | 20224 B，只导出 `.text` |

与早期版本相比，DTCM 里已经没有 PROGBITS 段了 —— 那些孤儿 `.data` / `.SRAM`
已并入 `.text` payload，原因见 §13.6。`readelf -l` 现在只剩 3 个 LOAD segment，
`.ARM.exidx` 也已丢弃（无异常处理需求，留着只会在 `.text` 和 `.bss` 之间
多出一个 segment）。

上表 `0x20004000` 一行已经删除：DTCM 中不再有初始化数据。

### 13.6 MCU1 当前最需要确认的启动风险

MCU1 的 `.bss` 是 NOBITS，DTCM 的主栈/数据预留也是 NOBITS。当前 raw binary 只导出 `.text`，而 `startup.S` 不做 `.bss` 清零或 `.data` 复制。

**已经修掉的一处：** 之前 `firmware_common.ld` 的 `.text` 只收 `.text*` 和 `.rodata*`，
于是 `*(.data*)` 和 `*(.SRAM*)` 变成孤儿段被链接器扔到 DTCM（`0x20004000` 一带）
并保持 PROGBITS。raw `.bin` 不带它们，startup 也不复制，初值就这么丢了。
其中最要命的是 port.c 的 `uxCriticalNesting`：它的初值是 `0xaaaaaaaa`
（一个故意选的非法值，用来抓"调度器还没启动就用临界区"），上电时却是 DTCM 的随机内容。
`xTaskCreate()` 在 `xPortStartScheduler()` 把它置 0 之前就已经进出临界区，
`vPortExitCritical()` 递减一个随机值永远到不了 0，于是再也不重新开中断。
现在 `.data*` / `.SRAM*` 已经并入 MCU0 那样的 `.text` payload
（`readelf -S` 确认 DTCM 侧不再有 PROGBITS 段，`uxCriticalNesting` 落在
`0x01004f60`），两个脚本也都加了 `KEEP(*(.init_array*))`，避免以后有构造函数被
`--gc-sections` 静默丢掉。

**手册已有明确结论的两条**（不必再列为未知）：

1. **TCM ECC**。MCU_0 一侧，reset sequencer 在放开 `mcu_0_cpuwait` 之前就已经把
   DTCM、系统 SRAM、HSM-Lite、LSP 初始化并做完 ECC 检查，之后 MCU_0 才从
   `0x0000_0004` 指向的地址开始取指（TRM 9.3）。所以 **MCU0 应用不需要自己做
   TCM ECC 初始化**。MCU_1 一侧，`ITCM_init_en1` / `DTCM_init_en1` 及对应的
   `*_init_chk_en1` 字段手册明确写了"只在 MCU 0 可用，MCU_1 没有"，
   也就是**必须由 MCU0 触发**，MCU1 自己的 startup 无从下手。
2. **MCU1 入口契约**。TRM 10.2 说得很具体：MCU_1 复位后默认 VTOR 为 0，冷启动时
   ITCM 被清空所以不能从那里跑；MCU_0 要把启动代码装入系统 SRAM、把地址写进
   `VTOR_ALTVEC`，然后放开 MCU_1 reset；MCU_1 取的第一条指令来自
   **`VTOR_ALTVEC + 0x4` 处存放的值**，不是 `VTOR_ALTVEC` 本身。
   当前布局（向量表 `0x01000400`，槽 1 = `0x01000101`）与这个契约一致。
   手册还要求装载的代码**先把整个默认地址空间设成 XN，再逐个开放可执行窗口**，
   见 §15.5。

**仍需与 bootloader 一起确认的**，实际只剩加载方式：

1. MCU0/bootloader 加载 MCU1 时，是按 ELF segment 加载，还是只复制 raw `.bin`？
   这决定了 `.bss` 由谁清零。
2. 释放 reset 前，MPU 是否允许 MCU1 从系统 SRAM 执行（与上面 XN 要求相关）。

如果答案是“只复制 `.text`，其他都不管”，就应建立标准链接符号并在 startup 中实现初始化，例如：

```ld
_sidata = LOADADDR(.data);
_sdata = ADDR(.data);
_edata = ADDR(.data) + SIZEOF(.data);
_sbss = ADDR(.bss);
_ebss = ADDR(.bss) + SIZEOF(.bss);
```

然后在 TCM ECC 已初始化之后复制 data、清零 bss。也可以让 bootloader 明确按镜像描述加载多个段，但应用和 loader 必须形成清晰契约。

注意：一旦引入 newlib（见 §25），`.data` 装载就从“目前碰巧没人依赖”变成必需，
因为 newlib 的 `_impure_data` 带非零初值。

## 14. 如何为这块板重新设计 SRAM 分配

### 14.1 第一步：画出物理资源和保留区

至少列出：

```text
全局 SRAM 0x01000000 .. 0x011FFFFF
MCU0 DTCM 0x20000000 .. 0x20007FFF
MCU1 DTCM 0x20000000 .. 0x20007FFF   由各自 CPU 地址空间观察
MCU1 ITCM 16 KB
Boot/ROM driver 保留窗口
MCU0/MCU1 固件装载窗口
IPC mailbox 和共享缓冲区
DMA 描述符与数据 buffer
```

两个 MCU 都把自己的 DTCM 映射为 `0x20000000` 并不表示它们共享同一块物理 DTCM。不要把 DTCM 地址拿来做跨核指针。

### 14.2 第二步：区分私有区和共享区

每个区域必须标明 owner：

| 区域类型 | 例子 | 规则 |
|---|---|---|
| MCU0 私有 | MCU0 代码、TCB、任务栈、heap | MCU1 不写 |
| MCU1 私有 | MCU1 代码、TCB、任务栈、heap | MCU0 不写 |
| Boot/ROM 私有 | image header、ROM scratch | 应用不占用 |
| 外设/DMA | 描述符、采样 buffer | 遵守 DMA cache 规则 |
| 跨核共享 | mailbox、ring buffer | 明确所有权和同步协议 |

不要只看两个 section 当前没有重叠。还要检查 bootloader、ROM driver、DMA 和另一个固件的绝对地址窗口。

### 14.3 第三步：决定每类对象放在哪里

推荐基线：

- 代码和只读常量：全局 SRAM，可执行、cacheable。
- 普通 BSS/data：各 MCU 私有全局 SRAM，cacheable。
- MSP：各自 DTCM，容量按最大 ISR 嵌套评估。
- 普通任务栈：全局 SRAM，通过静态 section 或 `heap_4` 分配。
- `heap_4`：各自私有、连续、8 字节对齐的全局 SRAM。
- DMA buffer：专用 section，明确 cache line 对齐和 cache 维护策略。
- 双核共享：专用 section，不能混入普通 cacheable BSS。
- 小型关键数据：经过测量后才考虑 DTCM。

### 14.4 第四步：给链接器加边界断言

链接脚本应尽可能在构建期失败，而不是上板后随机覆盖。前两条现在已经加进
`mcu0.ld` 和 `firmware_common.ld`：

```ld
ASSERT(__freertos_heap_end__ <= (ORIGIN(SRAM) + LENGTH(SRAM)),
       "MCU0 SRAM window overflow: payload + FreeRTOS heap exceed LENGTH(SRAM)");
ASSERT((__main_stack__ & 7) == 0,
       "MCU0 initial MSP must be 8-byte aligned");
```

第一条不是多余的。`--print-memory-usage` 只是打印百分比，越界时链接器给的是
`region SRAM overflowed` 这种泛化信息，而 NOLOAD 段的越界在某些布局下
根本不报 —— 加了这条 ASSERT 之后，构建期会先打印出带工程语义的那句话。
（已验证：把 `LENGTH(SRAM)` 临时改成 32K，链接立即失败并先打印
"MCU0 SRAM window overflow"。）

还值得加但尚未加的：

```ld
ASSERT(__shared_end__ <= SHARED_END, "shared SRAM overflow");
```

如果有多个固定地址窗口，还应断言它们不重叠。链接器只知道当前 ELF 内的 section；属于另一个镜像的边界要通过统一 memory map 文件或显式常量同步 —— 这正是 §14.6 那张表要解决的问题。

### 14.5 第五步：留下余量

不要把 SRAM 规划到刚好 100%。至少为以下变化留余量：

- 编译器版本变化导致代码和栈使用变化。
- 新增队列和任务。
- 中断嵌套最坏情况。
- FPU 扩展异常帧。
- cache line 对齐。
- 日志和 fault 路径。
- DMA 一次突发需要的额外 buffer。

“链接还剩 10 KB”不代表运行时堆一定有 10 KB，因为动态对象和峰值同时存在关系要另外算。

### 14.6 给 MCU0 分配超过 256 KB：MPU 不是那个开关

先纠正一个前提：**MPU 不分配内存，也不会"跳过"它来获得更多空间。**
MPU 是一个访问权限和内存属性的过滤器 —— 对最多 16 个地址区间标注
可读/可写/可执行、cacheable/device、shareable。它不做地址翻译（那是 MMU 的事，
Cortex-M 没有），不能把不存在的内存变出来，也不能把一块 SRAM 映射到两个地址。
MCU0 现在"只有 256 KB"跟 MPU 毫无关系，MPU 当前甚至没有被使能。

真正限制它的是三层，按从软到硬排列：

**第一层：链接脚本里的一个常数。** `mcu0.ld` 写着

```ld
SRAM (xw) : ORIGIN = APP_LOAD_ADDR, LENGTH = 256K
```

这个 256K 是从 PVT SDK 的 `corelib_demo/demo.ld` 和
`init/bcm8915x/chip/common/firmware_offset1.ld` 抄来的约定，不是芯片参数，
也不是 bootloader 要求的。改大这个数字链接器立刻就能用 —— 前提是先回答第二层。

**第二层：谁还住在那片地址里。** 这是真正的约束，而且链接器看不见。

权威答案在 `doc/PVT_BCM8915X_BL_2026.26.0_M7_BL_Manual.pdf` 的
**Table 2.3.1 Memory Layout**（可用 `bcm8915x-docs` skill 检索：
`docsearch.py -d bl -C 40 '0x01040000'`）。这是 bootloader 自己声明的地图，
优先级高于任何 `.ld` 文件：

| 区域 | 地址范围 | 权限 | 用途 |
|---|---|---|---|
| `DTCM PRV` | `0x20000000`–`0x20003FFF` | Privileged RW | 栈和驱动 BSS |
| `DTCM` | `0x20004000`–`0x200079FF` | RW | 组件 BSS |
| `DBG LOGS` | `0x20007A00`–`0x20007FFF` | RW | 调试日志 |
| **`SRAM BL Text`** | `0x01000000`–`0x0103FFFF` | **R + Execute** | **MCU0 bootloader 自身代码** |
| **`SRAM Free`** | `0x01040000`–`0x011CFFFF` | RW | **MCU1 / Q8 应用镜像空间** |
| `SRAM IPC` | `0x011D0000`–`0x011D7FFF` | RW | IPC 消息内存（PCIe EP 模式下暴露） |
| `SRAM User Scratch` | `0x011EC000`–`0x011FBFFF` | RW | 用户 scratch |
| `SRAM Driver Scratch` | `0x011FC000`–`0x011FDFFF` | Privileged RW | 驱动 scratch |
| `ROM DRV Scratch` | `0x011FE000`–`0x011FFFFF` | Privileged RW | ROM entry point API 使用 |
| `DRAM` | `0x80000000`–`0xDFFFFFFF` | RW | 外部 DDR |

从这张表能直接读出三件事：

1. **`APP_LOAD_ADDR = 0x01040000` 不是随便选的** —— 它正好是 bootloader
   自身代码段的结束位置。`0x01040000` 以下是 `SRAM BL Text`，
   应用绝对不能往下扩。所以扩容只能往高地址走。
2. **真正的可用窗口是 `0x01040000`–`0x011CFFFF`，正好 1600 KB**，
   而当前脚本只声明了 256 KB —— 上方还有 1344 KB 从未使用。
3. 这块空间的正式用途是 "MCU1 / Q8 应用镜像"。也就是说 MCU0 应用扩容时，
   必须和 MCU1 镜像、Q8 镜像一起分配，不能假设整片都归自己。

**几处工程脚本与手册不一致，一律以手册为准：**

| 项 | 本工程 / PVT 脚本 | BL 手册 Table 2.3.1 | 结论 |
|---|---|---|---|
| IPC scratch | `mcu0.ld`：`SRAM_IPC_SCR` 在 `0x011E0000`，16 KB | IPC 在 `0x011D0000`–`0x011D7FFF`（32 KB） | **`mcu0.ld` 放错了**：真正的 IPC 区在 `0x011D0000`。`0x011E0000`–`0x011E3FFF` 落在 `0x011D8000`–`0x011EBFFF` 这段 **80 KB 手册未列出的空隙**里（IPC 结束于 `0x011D7FFF`，User Scratch 从 `0x011EC000` 才开始）—— 未列出不等于可用。目前是**潜伏**问题而非活跃故障：`--print-memory-usage` 显示 `SRAM_IPC_SCR: 0 B`，没有任何 section 落进去。要么改成 `0x011D0000`，要么直接删掉这个区域声明。 |
| IPC scratch | PVT `firmware_offset1.ld`：`0x011D0000`，32 KB | `0x011D0000`–`0x011D7FFF`（32 KB） | PVT 脚本与手册一致。 |
| User scratch | PVT：`SRAM_USR_SCR` `0x011D8000`，144 KB | `0x011EC000`–`0x011FBFFF`（64 KB） | 两者不一致，且 PVT 的范围会盖住手册的 driver scratch。以手册为准。 |
| ROM/driver scratch | `mcu1.ld`：`ROM_DRV_SCR` `0x011F8000`，32 KB | driver scratch `0x011FC000`–`0x011FDFFF`（8 KB）+ ROM DRV scratch `0x011FE000`–`0x011FFFFF`（8 KB） | `mcu1.ld` 的起点偏低且尺寸偏大，会侵入 user scratch 区。 |
| MCU1 SRAM 窗口 | `mcu1.ld`：`ORIGIN = 0x01000000, LENGTH = 2M - 32K` | bootloader text 占 `0x01000000`–`0x0103FFFF` | **`mcu1.ld` 从 bootloader 代码段起就开始声明**，整片覆盖了 MCU0 窗口和 bootloader 自身。目前两个核不同时跑在同一区域所以没炸，但这是 §15 强调的重叠问题的最坏例子。 |

这些差异目前都没有造成实际故障（实测 `SRAM_IPC_SCR` 占用 0 B，
两核固件也没有并行跑同一区域），但它们是"下一个人照着脚本推断内存布局"时
会踩的坑。修之前应先与 bootloader 团队确认板上实际运行的 bootloader 版本
是否就是 `PVT_BCM8915X_BL_2026.26.0`。

**第三层：镜像和 bootloader 契约。** `APP_LOAD_ADDR` 和
`APP_LOAD_ADDR + 0x400` 这两个地址被 `img_signer.py` 的 `-l` / `-e` 参数和
`mcu0.ld` 的两条 ASSERT 同时锁定。扩大 `LENGTH(SRAM)` 不影响这两个值，
但如果改的是 `ORIGIN`，签名脚本的 `-l` 必须跟着改。

所以扩容的正确顺序是：

1. 确认板上 bootloader 版本与 Table 2.3.1 相符（上面那张表来自
   `PVT_BCM8915X_BL_2026.26.0`；换版本要重新检索一遍手册）。
2. 与 MCU1 / Q8 镜像的负责人划分 `0x01040000`–`0x011CFFFF` 这 1600 KB，
   因为手册把它整体标为 "Space for MCU1/Q8 App images"，不是 MCU0 独占。
3. 只改 `mcu0.ld` 的 `LENGTH`，比如 `256K` → `512K`。**上限是
   `0x011CFFFF - 0x01040000 + 1 = 0x190000`，即 1600 KB**，
   再往上就撞 `0x011D0000` 的 IPC 区。
4. 依赖新加的
   `ASSERT(__freertos_heap_end__ <= ORIGIN(SRAM) + LENGTH(SRAM))`
   在构建期兜住越界（已验证：把 LENGTH 改成 32K 时链接立即失败并打印
   "MCU0 SRAM window overflow"）。
5. 把两个核的 map 合并，检查新窗口与 MCU1、Q8、IPC、scratch 区不重叠。
   顺手修掉上表列出的几处脚本错误，否则合并出来的地图本身就是错的。
6. 如果扩容目的是放大 `heap_4`，同时改 `configTOTAL_HEAP_SIZE`；
   `ucHeap[]` 的大小由它决定，`mcu0.ld` 里的 `freertos_heap_size`
   其实是个没被引用的残留符号（段大小来自 `ucHeap` 本身）。

**MPU 该在什么时候进场。** 扩容之后，MPU 用来给这些区间加正确的属性，
这跟"能用多少内存"是两个独立问题：

- 代码区 → RO + executable + cacheable
- 私有数据/堆/栈 → RW + XN + cacheable
- DMA buffer 与跨核共享区 → non-cacheable，或保持 cacheable 但强制维护（§15.4）
- 外设 → device memory + XN

注意 TRM 对 MCU1 有一条硬要求（§13.6）：装载的代码必须先把默认地址空间
整体设成 XN，再逐个开放可执行窗口。这是安全要求，不是性能优化。
另外普通 `ARM_CM7/r0p1` 端口不是 FreeRTOS 的 MPU 隔离端口
（那是 `ARM_CM7_MPU`），配置板级 MPU 属性不等于给每个任务做权限隔离。

DTCM 想扩就没这么容易了：物理上就是 32 KB，`mcu0.ld` 把它切成两个 16 KB
窗口纯属工程约定，但 32 KB 这个总量是硬的。要更多确定性内存只能是
"把东西挪出 DTCM"，不是"把 DTCM 变大"。

## 15. 双 MCU、共享 SRAM、cache 和 DMA

### 15.1 两套 FreeRTOS 完全独立

MCU0 和 MCU1 各自有：

- `pxCurrentTCB`。
- ready/delay list。
- SysTick 和 tick count。
- `ucHeap[]`。
- Idle task 和 timer task。
- 自己的中断控制器视图和任务栈。

一个 MCU 的 FreeRTOS mutex 只能协调该 MCU 上的任务。它不能阻止另一个 MCU 或 DMA 同时访问内存。

### 15.2 volatile 不等于同步

`volatile` 只要求编译器按规则真的访问对象，不能提供：

- 原子性。
- 两个 MCU 之间的 cache 一致性。
- 内存访问顺序。
- 互斥。
- DMA 可见性。

跨 MCU 通信应使用硬件 mutex、IPC doorbell/mailbox、带内存屏障的无锁协议，或芯片提供的通信驱动。

### 15.3 cache line 所有权

共享结构最好按 cache line 对齐，并避免两个 MCU 写同一 cache line 中的不同字段。否则即使逻辑字段互不相干，clean 整条 cache line 时也可能覆盖对方数据。

一种简单协议：

```text
生产者拥有 data buffer
  -> 写数据
  -> D-cache clean
  -> DMB/DSB
  -> 写 ready/doorbell

消费者收到通知
  -> DMB
  -> D-cache invalidate
  -> 读数据
  -> 归还所有权
```

具体 clean/invalidate 的方向、范围和屏障必须结合 Cortex-M7 cache API、cache line 大小和总线行为确认。

### 15.4 DMA 的典型规则

CPU 向 DMA 提交 TX buffer：

1. CPU 写 buffer。
2. clean 对应 D-cache range，使数据写回 SRAM。
3. 执行需要的内存屏障。
4. 启动 DMA。

DMA 写 RX buffer 后 CPU 读取：

1. 等 DMA 完成。
2. invalidate 对应 D-cache range，丢弃 CPU 旧副本。
3. CPU 读取 SRAM 中的新数据。

不要对未按 cache line 对齐且与其他可写对象共用 cache line 的区域直接 invalidate，否则可能丢失旁边对象的脏数据。通常应对 DMA buffer 做 cache-line 对齐和尺寸向上取整。

### 15.5 MPU 策略

手册建议 MCU1 启动时先用 MPU 将默认空间设为 execute-never，再开放确实需要执行的窗口。可建立类似原则：

- 代码区：read-only、executable、cacheable。
- 私有数据区：read-write、execute-never、cacheable。
- 外设区：device memory、execute-never。
- DMA/共享区：根据一致性方案设置 non-cacheable，或保持 cacheable 但强制维护。
- TCM：按架构属性和芯片要求配置。

普通 `ARM_CM7/r0p1` 端口不是 FreeRTOS MPU 隔离端口。配置板级 MPU 属性不等于给每个任务建立独立访问权限。

## 16. 从裸机业务迁移成任务

### 16.1 不要把 while(1) 直接复制多份

错误方式：

```c
void AdcTask(void *arg)
{
    for (;;) {
        if (adc_ready()) {
            process_adc();
        }
    }
}
```

如果 `adc_ready()` 大多数时间为 false，这个任务仍持续占 CPU。

更好的方式是让 ISR 通知任务：

```c
void AdcTask(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        process_adc();
    }
}
```

任务没有采样时完全阻塞。ADC ISR 使用 `vTaskNotifyGiveFromISR()` 唤醒它。

### 16.2 忙等和 vTaskDelay

- 忙等：CPU 在循环里反复检查时间，期间同优先级和低优先级任务可能得不到运行机会。
- `vTaskDelay(n)`：当前任务阻塞至少 n 个 tick，CPU 可运行其他任务。
- `vTaskDelayUntil()`：以固定基准产生周期，适合周期任务，长期漂移小于反复 `vTaskDelay()`。

`vTaskDelay(1)` 表示延时一个 tick，不保证精确 1 ms 的执行周期。调用发生在 tick 边界前后会影响实际墙上时间，而且任务恢复后还要等调度。

### 16.3 选通知、队列、信号量还是 stream buffer

| 需求 | 建议工具 |
|---|---|
| 单个 ISR 告诉单个任务“发生了 N 次” | task notification |
| 传递固定结构体消息 | queue |
| 表示资源可用数量 | counting semaphore |
| 保护同 MCU 任务间共享资源 | mutex |
| 传递连续字节流且单写单读 | stream buffer |
| 传递离散可变长消息 | message buffer |

任务通知通常最轻量，但只能绑定任务且语义有限。队列会复制数据。传大 buffer 时可在队列中传指针，但必须清楚 buffer 所有权。

### 16.4 mutex 不能在 ISR 中使用

Mutex 有优先级继承，设计用于任务上下文。ISR 不允许阻塞，也没有“当前任务优先级”语义。ISR 与任务共享设备状态时，应缩短临界区，或让 ISR 只采集状态并通过通知/队列交给任务处理。

### 16.5 优先级分配

先根据时限和阻塞关系分配，不要用“模块重要程度”命名：

- 硬实时、被 ISR 唤醒且处理必须很快的任务：较高。
- timer service task：当前为最高任务优先级 4，回调必须短。
- 普通数据处理：中间。
- 日志和后台维护：较低。
- Idle：0。

持续就绪的高优先级任务会饿死所有低优先级任务。高优先级任务必须阻塞或主动等待事件。

### 16.6 初始化放在哪里

有两种常见方式：

1. 调度器前初始化：简单，但此时不能依赖任务阻塞、队列消费或中断唤醒。
2. init task 内初始化：调度器已经运行，可以使用延时和 RTOS 同步，完成后删除或转为普通任务。

当前 `BCM8915X_Main()` 先初始化轮询 UART 日志，具体硬件初始化放进 HSADC 任务。这样调度器故障前有日志，外设流程又能使用 RTOS。

## 17. 当前 OS 和日志封装值得注意的点

### 17.1 Lic_OsStart

`Lic_OsStart()` 的责任是：

- 让外部 IRQ 进入安静、可预测状态。
- 初始化日志 RTOS 对象。
- 创建业务任务。
- 调用 `vTaskStartScheduler()`。
- 若调度器意外返回，进入 fault 停机路径。

每一次创建都应检查返回值。调度器最常见的启动失败原因之一就是 Idle/timer task 所需内存无法分配。

### 17.2 日志的两条路径

调度器前或 panic 时：

```text
调用者 -> 格式化 -> 轮询 UART
```

调度器运行后：

```text
调用者 -> stream buffer -> 日志任务 -> 轮询推进 UART
```

日志任务静态创建，stream buffer 也使用静态 storage。普通任务日志采用零等待发送，buffer 满时丢弃，而不是阻塞业务任务。

ISR 日志使用：

```c
xStreamBufferSendFromISR(..., &woken);
portYIELD_FROM_ISR(woken);
```

这是正确的 FromISR 模式，但它给调用方附加了一条约束：**调用 `Lic_LogFromIsr()` 的
ISR，其逻辑优先级必须在 2 到 7 之间**（寄存器值 ≥ `0x40`，即
`configMAX_SYSCALL_INTERRUPT_PRIORITY`）。优先级 0 或 1 的 ISR 调用它会命中
端口的 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()`。§9.2 讲的是通则，
这里是它落到具体函数上的后果 —— 而 NVIC 复位默认优先级恰好是 0，
所以"新接一个 IRQ、顺手打条日志"是很容易踩的组合。

另外 ISR 中仍应少打日志，格式化、缓存压力和串口带宽都会影响实时性。
`Lic_LogFromIsr()` 只接受已经成形的字符串，不做格式化，就是这个原因。

### 17.3 panic 日志

fault 后调度器不可信，日志任务不会再被调度。因此 `Lic_LogPanicFlush()` 切换到轮询路径，先排空 stream buffer，再打印 fault 信息并屏蔽中断停机。

这里有一个反直觉但必须遵守的细节：排空时用的是 **`xStreamBufferReceiveFromISR()`**，
即使调用者大多在 thread mode。原因是任务上下文的 `xStreamBufferReceive()` 结尾会走
`sbRECEIVE_COMPLETED` → `vTaskSuspendAll()` / `xTaskResumeAll()`，而
`xTaskResumeAll()` 里的 `taskENTER_CRITICAL()` 会命中 `vPortEnterCritical()` 的

```c
configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 );
```

在 Handler mode（也就是从 fault handler 过来时）这个断言必然失败。失败后进入
`Lic_OsAssertFailed()` → `Lic_OsPark()` → 又调 `Lic_LogPanicFlush()`，
而缓冲区里还有数据，于是再次 assert —— 一路递归吃掉 MCU0 那 2 KB 的 MSP，
最后升级成 HardFault 再走同一条路。**表现恰好是"fault 之后 UART 彻底安静"，
正是这条诊断路径存在的意义所被抹掉。** 触发条件也不苛刻：调度器已经在跑、
stream buffer 里有没发完的日志，也就是正常运行期的每一次 fault。

`FromISR` 变体不做这个通知，因此没有临界区，也就没有断言。它在这里安全的理由
和它在 ISR 里安全的理由相同：日志任务已经死了，不存在第二个接收方，
也没有发送方在等空间。

这使“调度器死了以后仍能看到原因”真正成立。但如果 UART 时钟、引脚或硬件本身已损坏，仍需配合 `Lic_OsFaultInfo` 内存记录和调试器。

## 18. 构建和静态检查方法

下面的命令都应在仓库根目录执行。工具链路径以工程实际配置为准。

### 18.1 构建

```bash
make -C lightic_app mcu0
make -C lightic_app mcu1
```

如果需要签名镜像，使用工程已有 `mcu0-image`、`mcu1-image` 目标，并确认签名工具所理解的装载地址和入口契约。

### 18.2 看 section 大小和属性

```bash
arm-none-eabi-size -A lightic_app/bin/mcu0/lightic_mcu0.elf
arm-none-eabi-size -A lightic_app/bin/mcu1/lightic_mcu1.elf

arm-none-eabi-readelf -S lightic_app/bin/mcu0/lightic_mcu0.elf
arm-none-eabi-readelf -S lightic_app/bin/mcu1/lightic_mcu1.elf
```

`readelf -S` 中要区分：

- PROGBITS：ELF 中实际有内容。
- NOBITS：运行时占地址，但 ELF 文件中不存逐字节内容。
- W/A/X flags：可写、可分配、可执行。

### 18.3 看 program header 和装载关系

```bash
arm-none-eabi-readelf -l lightic_app/bin/mcu0/lightic_mcu0.elf
arm-none-eabi-readelf -l lightic_app/bin/mcu1/lightic_mcu1.elf
```

重点看 VMA、LMA、FileSiz 和 MemSiz：

- VMA：运行地址。
- LMA：镜像装载地址。
- FileSiz：文件里实际带多少字节。
- MemSiz：运行时占多少字节。

若 MemSiz 大于 FileSiz，差额通常需要启动时清零。

### 18.4 看符号和地址

```bash
arm-none-eabi-nm -n lightic_app/bin/mcu0/lightic_mcu0.elf
arm-none-eabi-nm -n lightic_app/bin/mcu1/lightic_mcu1.elf
```

重点搜索：

```text
CORTEX_MX_VECTOR_TBL
BCM_OS_RESET_HANDLER
__main_stack__
ucHeap
__freertos_heap_start__
__freertos_heap_end__
vPortSVCHandler
xPortPendSVHandler
xPortSysTickHandler
```

### 18.5 反汇编

```bash
arm-none-eabi-objdump -d -S lightic_app/bin/mcu0/lightic_mcu0.elf
arm-none-eabi-objdump -d -S lightic_app/bin/mcu1/lightic_mcu1.elf
```

检查：

- reset 是否在预期地址。
- `MSR MSP` 和 VTOR 写入是否存在。
- `svc 0` 是否进入正确 handler。
- PendSV 是否使用 PSP、BASEPRI 和 `pxCurrentTCB`。
- FPU 条件保存路径是否存在。

### 18.6 从 fault PC 找源码

```bash
arm-none-eabi-addr2line -e lightic_app/bin/mcu0/lightic_mcu0.elf -f -C 0x01041234
```

必须使用与板上镜像完全对应的 ELF。优化会让一条指令对应多行内联源码，`-f -C` 可以同时显示函数名并解码 C++ 名称。

### 18.7 直接检查向量表

```bash
arm-none-eabi-objdump -s -j .text lightic_app/bin/mcu0/lightic_mcu0.elf
arm-none-eabi-objdump -s -j .text lightic_app/bin/mcu1/lightic_mcu1.elf
```

对照小端字节序解析前 16 个 32 位槽。至少确认：

- 槽 0 是对齐的 MSP，不带 bit 0。
- 函数槽最低位为 1。
- SVC/PendSV/SysTick 指向 FreeRTOS port。
- VTOR 运行值与所检查表的地址相同。

### 18.8 map 文件比 size 更重要

map 能回答：

- 哪个对象把 section 撑大了。
- 某个孤儿 `.data.xxx` 最终被放在哪里。
- 地址间的对齐空洞来自哪里。
- 静态栈和缓存是否落在预期区域。

每个发布镜像应保留 ELF 和 map，不能只保留 stripped `.bin`。

## 19. 推荐上板验证顺序

### 阶段 1：只验证 reset

目标：

- CPU 到达 `BCM_OS_RESET_HANDLER`。
- VTOR 是正确地址。
- MSP 在有效 DTCM 范围且 8 字节对齐。
- 除零和可配置 fault 已开启。

观察方式：调试器断点、GPIO 或最早期轮询 UART。

### 阶段 2：进入 C 和早期日志

目标：

- `BCM8915X_Main()` 可执行。
- 全局初始化值正确。
- BSS 确实为 0。
- UART 轮询日志可用。

建议显式检查一个非零 `.data` 变量和一个 `.bss` 变量，不要只看“恰好为零”的 SRAM。

### 阶段 3：启动首任务

在 `vTaskStartScheduler()` 前后设置日志或断点：

- 看到“starting scheduler”后，首先应该进入 SVC。
- 看到 `log task running, scheduler alive`，说明首任务已经运行。

注意，这条日志能证明 SVC 和任务恢复工作；要证明 SysTick/PendSV，还要继续观察 tick 变化和任务切换。

### 阶段 4：验证 tick

- 周期读取 `xTaskGetTickCount()`。
- 1 秒墙上时间应约增加 1000。
- 验证 MCU1 300/600 MHz 模式。
- 验证长时间回绕逻辑不使用错误有符号比较。

### 阶段 5：验证两个任务切换

创建两个同优先级任务，各自递增独立计数并 `vTaskDelay()`。确认两个计数都增长。然后用任务通知让高优先级任务被唤醒，确认它能抢占低优先级任务。

### 阶段 6：验证内存

- 动态创建和删除测试对象。
- 查看 `xPortGetFreeHeapSize()`。
- 查看 `xPortGetMinimumEverFreeHeapSize()`。
- 查看每个任务 `uxTaskGetStackHighWaterMark()`。
- 对静态任务和动态任务分别记录最差值。

栈 high-water mark 单位仍是 `StackType_t`，换算为字节要乘 4。

### 阶段 7：主动触发诊断

在测试固件中分别触发：

- `configASSERT(0)`。
- 超过堆容量的分配。
- 可控的任务栈溢出。
- 整数除零 UsageFault。
- 非法地址 BusFault/MemManage。

确认 UART 输出和 `Lic_OsFaultInfo` 记录都能定位原因。测试后删除或用专用宏隔离故障注入。

### 阶段 8：逐个恢复外设 IRQ

每次只启用一个 IRQ：

- 注册 handler。
- 设合法优先级。
- 清双层 pending。
- ISR 只调用 FromISR API。
- 验证 `portYIELD_FROM_ISR`。
- 统计 ISR 最长耗时。

### 阶段 9：DMA 和 cache

用已知字节图案验证不同方向和不同长度，包括：

- 未对齐长度。
- 跨 cache line。
- 连续多次 DMA。
- CPU 修改后立即 DMA。
- DMA 完成后 CPU 立即读取。

### 阶段 10：双 MCU 共享

最后再启用 MCU0/MCU1 IPC。用序号、长度和 CRC 检查共享消息；长时间压测生产者快于消费者、消费者重启、一个 MCU reset 等边界场景。

## 20. 常见故障和排查表

| 现象 | 首要怀疑 | 检查方法 |
|---|---|---|
| reset 前后立即 HardFault | 初始 MSP、向量基址、执行权限 | 读 VTOR、MSP、槽 0/1、CFSR |
| “starting scheduler”后静默 | SVC 向量错误或首任务栈错误 | 断在 `vPortSVCHandler`，检查 `VTOR[11]` |
| 首任务运行但 tick 不变 | SysTick 时钟/向量/使能错误 | 读 SysTick CTRL/LOAD/VAL 和槽 15 |
| tick 走但任务不切换 | PendSV 路由或优先级错误 | 读 ICSR PENDSVSET、槽 14、SHPR |
| MSP 是奇数 | 槽 0 错用了函数宏 `+1` | 向量表槽 0 必须用 `VECTOR_SP` |
| 一启用 IRQ 就 assert | ISR 默认优先级 0 却调用 FromISR | 显式 `NVIC_SetPriority` 为 2 到 7 |
| 一启中断就 unexpected IRQ | 外设/NVIC 旧 pending 或向量未注册 | 先关源，清外设和 NVIC pending |
| `unexpected interrupt (exc 57)` | UART1 外部 IRQ 41 | `57 - 16 = 41`，检查 UART1 |
| 全局变量随机 | BSS 未清零或 data 未复制 | 对比 ELF segment、raw bin 和 startup |
| MCU1 ELF 调试正常、签名 bin 不正常 | debugger 加载多段，bin 只有 `.text` | 检查 objcopy 和真实 loader |
| 创建任务失败 | heap 不足或碎片 | 检查返回值和最小剩余堆 |
| 运行一段时间莫名 fault | 任务栈或 MSP 栈溢出 | hook、high-water、map、栈图案 |
| DMA 数据偶尔旧 | D-cache 未 clean/invalidate | 检查 ownership、对齐和维护范围 |
| 双 MCU 偶发覆盖 | SRAM 区间重叠或无跨核协议 | 合并两套 map，检查共享所有权 |
| 低优先级任务不运行 | 高优先级任务一直 ready | 查看任务状态，让高优先级任务阻塞 |
| 软件定时器普遍延迟 | callback 阻塞或 timer queue 满 | 缩短 callback，检查发送返回值 |
| fault PC 看似无关 | IMPRECISERR 异步总线错误 | 看 CFSR，缩小写缓冲/外设访问范围 |

## 21. 进一步的故障寄存器解释

### 21.1 CFSR

CFSR 合并了三个状态区：

- bits 0 到 7：MemManage（MMFSR）。
- bits 8 到 15：BusFault（BFSR）。
- bits 16 到 31：UsageFault（UFSR）。

**这个打包方式是一个现成的陷阱，本工程踩过。** 手册在描述 UFSR 时通常用
UFSR 内部的偏移（`DIVBYZERO` 是 UFSR 的 bit 9），而代码里要用的是 CFSR 里的
绝对位置（bit 9 + 16 = bit 25）。早期 `Lic_OsFaultReport()` 的六个 UsageFault
掩码整体差了 8 位，后果不是"读不出来"而是**读成别的原因**：

| 实际发生 | CFSR 值 | 当时会打印成 |
|---|---:|---|
| 整数除零 | `0x02000000` | `INVSTATE: bad EPSR/Thumb state` |
| 未对齐访问 | `0x01000000` | `UNDEFINSTR` |

这比不打印更坏 —— §19 阶段 7 让你注入除零来验证诊断路径，验证会"通过"，
只是结论是错的，而你会带着一个错的解码器去查真正的故障。
现在代码里的掩码用 `_Static_assert` 逐条钉在 CMSIS 的
`SCB_CFSR_*_Msk` 上（已验证：故意改错一位，编译立即失败），
MemManage / BusFault / HFSR 那几位当时是对的。

常见位（下表用 CFSR 中的绝对位置）：

| 位名 | 含义 |
|---|---|
| `IACCVIOL` | 从不可执行/不允许区域取指 |
| `DACCVIOL` | 数据访问违反 MPU |
| `PRECISERR` | 精确总线错误，stacked PC 通常有意义 |
| `IMPRECISERR` | 非精确异步总线错误，PC 可能已向后执行 |
| `UNDEFINSTR` | 未定义指令 |
| `INVSTATE` | 无效执行状态，常见于错误函数地址 Thumb 位 |
| `UNALIGNED` | 未对齐访问陷阱 |
| `DIVBYZERO` | 除零陷阱 |

`MMARVALID` 和 `BFARVALID` 置位时，MMFAR/BFAR 地址才可信。

### 21.2 HFSR

如果 `FORCED` 置位，表示一个可配置 fault 未启用或在处理过程中再次出错，升级为 HardFault。此时真正原因通常仍在 CFSR 中。

如果 fault handler 自己调用复杂日志再次 fault，现场会被二次异常覆盖。产品诊断路径应尽量短、避免动态分配并可退化为只写固定内存。

### 21.3 从地址回到源码

流程：

```text
UART/内存中取 stacked PC
  -> 确认使用完全匹配的 ELF
  -> addr2line 映射
  -> objdump 查看前后指令
  -> CFSR 判断 PC 是否可信
  -> 检查访问地址、寄存器和链接 map
```

函数向量值带 Thumb bit，但反汇编/`addr2line` 查询通常可使用清除 bit 0 后的实际指令地址。

## 22. 移植完成检查清单

### CPU 和工具链

- [ ] CPU/架构、revision 和编译器端口匹配。
- [ ] 单精度/双精度能力确认。
- [ ] 所有对象和库使用兼容浮点 ABI。
- [ ] `__NVIC_PRIO_BITS` 与 `configPRIO_BITS` 一致。
- [ ] CPU/SysTick 实际时钟与配置一致。

### 启动和链接

- [ ] 复位入口地址符合 bootloader 契约。
- [ ] VTOR 指向真实向量表。
- [ ] 槽 0 是对齐 MSP，不带 Thumb bit。
- [ ] 函数向量带 Thumb bit。
- [ ] `.data` 装载和 `.bss` 清零责任明确。
- [x] 没有初始化数据落在 raw bin 之外（DTCM 里不再有 PROGBITS 孤儿段，§13.6）。
- [x] TCM ECC 初始化责任明确：MCU0 由 reset sequencer 完成，MCU1 必须由 MCU0 触发（§13.6）。
- [ ] MPU/cache 初始化责任明确。
- [ ] ELF、raw bin、签名镜像的内容关系已核对。
- [x] 链接脚本有越界和固定入口 ASSERT。
- [x] 系统寄存器（CCR/SHCSR）写入是 read-modify-write，未清掉 cache/BP 使能（§5.6）。
- [x] 无 LOAD segment 指向外设地址空间（`.ARM.exidx` 已丢弃，§12.1）。

### FreeRTOS

- [ ] SVC -> `vPortSVCHandler`。
- [ ] PendSV -> `xPortPendSVHandler`。
- [ ] SysTick -> `xPortSysTickHandler`。
- [ ] PendSV/SysTick 为最低 NVIC 优先级。
- [ ] `configMAX_SYSCALL_INTERRUPT_PRIORITY` 非 0 且已正确移位。
- [ ] Idle/timer task 内存策略明确。
- [ ] 所有创建 API 检查返回值。
- [ ] assert、malloc hook、stack hook 和 CPU fault handler 可观察。
- [x] fault 报告的 CFSR 掩码与 CMSIS 一致（`_Static_assert` 保证，§21.1）。
- [x] panic 排空路径不调用会进临界区的内核 API（§17.3）。
- [ ] fault 注入实测：除零打印 `DIVBYZERO`、未对齐打印 `UNALIGNED`（验证解码正确，不只是"有输出"）。
- [ ] 引入 stdio/malloc 前已读过 §25 并确认 `end` 归属。

### 任务和中断

- [ ] 高优先级任务不会永远保持 ready。
- [ ] 任务栈单位按 `StackType_t` 换算。
- [ ] 每个任务测过 high-water mark。
- [ ] 每个 IRQ 都显式设置优先级。
- [ ] ISR 只使用 FromISR API。
- [ ] `xHigherPriorityTaskWoken` 和 `portYIELD_FROM_ISR` 正确。
- [ ] ISR 不使用 mutex、不阻塞、不做长日志。

### 内存、DMA 和双 MCU

- [ ] MCU0/MCU1 私有区不重叠。
- [ ] Boot/ROM scratch 不被应用占用。
- [ ] `heap_4` 连续且 8 字节对齐。
- [ ] MSP 按最坏 ISR 嵌套预算。
- [ ] DMA buffer cache line 对齐。
- [ ] clean/invalidate 的方向、范围和时机已压测。
- [ ] 共享 SRAM 有所有权协议和内存屏障。
- [ ] 没有用普通 FreeRTOS mutex 保护跨 MCU 数据。

### 上板验证

- [ ] reset 和早期 UART 已验证。
- [ ] 首任务已验证。
- [ ] 1000 Hz tick 已用真实时间验证。
- [ ] 两任务切换和抢占已验证。
- [ ] heap 和 stack 监控已验证。
- [ ] fault 注入已验证。
- [ ] 外设 IRQ 逐个恢复验证。
- [ ] DMA/cache 已验证。
- [ ] 双 MCU 长时间 IPC 压测已验证。

## 23. 针对 lightic_app 的后续工作优先级

按风险排序建议。已经做掉的列在前面，便于对照：

**本轮已修（代码 + 脚本）**

- MCU1 的 `.data*` / `.SRAM*` 孤儿段并入 payload，消除 `uxCriticalNesting`
  初值丢失导致的"临界区再也不开中断"（§13.6）。
- `Lic_OsFaultReport()` 六个 UsageFault 掩码修正并加 `_Static_assert`（§21.1）。
- `Lic_LogPanicFlush()` 改用 `xStreamBufferReceiveFromISR()`，消除 fault 后
  assert 递归吃穿 MSP 的静默死（§17.3）。
- `startup.S` 对 CCR/SHCSR 改为 read-modify-write，不再关掉 cache 和分支预测（§5.6）。
- `end` / `_end` 从 `__image_end__` 移到 `__freertos_heap_end__`，
  否则第一次 `malloc()` 就与 `ucHeap` 同址（§25.2）。
- 两个脚本加 SRAM 越界与 MSP 对齐 ASSERT，丢弃指向外设空间的 `.ARM.exidx`。

**仍待办**

1. 明确 MCU1 真实加载链：raw `.bin` 只有 `.text` 时，谁负责 `.bss` 清零。
   TCM ECC 和 VTOR 两项手册已有结论（§13.6），不必再当未知。
2. 把 §19 已经走通的阶段补上实测数据。调度器能跑不等于 tick 频率、抢占、
   栈高水位都验证过；尤其是 fault 注入要确认**解码正确**而不只是有输出。
3. 明确 MCU0/MCU1 的 MPU 和 cache 初始化代码究竟由哪一层执行。
4. 为每个恢复的外设 IRQ 建立“handler、逻辑优先级、是否调用 RTOS、pending 清除方式”表。
5. 为 DMA 和 IPC 建立专用 linker section、cache line 对齐和所有权协议。
6. 测量 MSP 和各任务栈高水位，再根据数据缩放，不靠经验猜测。
7. 将两个 ELF/map 的固定区间合并成一份发布内存地图，并在 CI 中检查重叠。

## 24. 参考文件

工程内参考：

- `doc/Technical_ReferenceManual.txt`：BCM8915x 处理器、SRAM/TCM、启动、中断和 cache/MPU 事实。
- `doc/BCM8915X_BareMetal_driver_user_guide.pdf`：裸机驱动接口及外设中断处理参考。
- `doc/freertos_port.md`：本次 FreeRTOS 加入工程的阶段实施和验证记录。
- `doc/firmware_connmand.ld.md`：链接相关补充说明。
- `lightic_app/freeRTOS/portable/GCC/ARM_CM7/ReadMe.txt`：CM7 端口适用范围。
- `lightic_app/freeRTOS/portable/GCC/ARM_CM7/r0p1/port.c`：SVC、PendSV、SysTick 和启动首任务实现。

阅读顺序建议：

```text
本笔记
  -> startup.S + exceptions.S
  -> FreeRTOSConfig.h
  -> mcu0.ld / mcu1.ld / firmware_common.ld
  -> lic_os.c
  -> FreeRTOS port.c
  -> TRM 对应的 Cortex-M、memory、interrupt、boot 章节
  -> 裸机驱动用户指南中实际使用的外设章节
```

最后牢记：FreeRTOS portable 层解决的是“Cortex-M7 如何调度”，板级代码仍要解决“CPU 从哪里启动、内存是否已经初始化、哪个中断能调用内核、cache/DMA 是否一致、两个 MCU 如何互不踩内存”。只有这些层的契约都明确，移植才真正完成。

## 25. stdlib / newlib 契约

### 25.1 现状：名义上链接了 libc，实际只用到两个函数

`Makefile` 里的组合有点自相矛盾：

```make
MCU0_LDFLAGS = $(FPU_FLAGS) -mthumb -nostdlib
MCU0_LIBS    = ../build/liblidar.a -Wl,--start-group -lc -lnosys -lgcc -lm -Wl,--end-group
```

`-nostdlib` 关掉了默认库和启动文件，后面又手工把 `-lc -lm -lnosys -lgcc` 加回来。
净结果是"按需拉取"：实测最终镜像里来自 libc 的只有 `memcpy` 和 `memset`
（`nm` 确认），以及 libgcc 的 64 位除法辅助函数。没有 `malloc`，没有 stdio，
没有 `__libc_init_array`，`nm -u` 显示无未定义符号。

这个状态是能工作的，而且日志模块自带极简格式化器（`%s %u %d %x %c %%`），
刻意避开了 `vsnprintf`。

### 25.2 要真正启用 stdlib，缺的是七项

**1. C 堆的地址（最危险的一项，已修）**

原来 `mcu0.ld` 写的是：

```ld
PROVIDE(end = __image_end__);
```

`_sbrk` 用 `end` 作堆底。而 `__image_end__` 算出来正好是 `0x010453D8`（当时的值），
**与 `ucHeap` 的起始地址逐字节相同** —— 第一次 `malloc()` 就会开始啃 FreeRTOS 的堆池，
两套分配器互相覆盖，而且症状会以"FreeRTOS 对象内容莫名损坏"的形式出现在别处。
`firmware_common.ld` 更直接：根本没定义 `end`，MCU1 一旦链接 `malloc` 就是
undefined reference。

现在两个脚本都改成：

```ld
PROVIDE(end  = __freertos_heap_end__);
PROVIDE(_end = __freertos_heap_end__);
```

即 C 堆从 FreeRTOS 堆池之后开始。但注意这只是把冲突挪走了，**没有给 C 堆划定上界**：
`_sbrk` 会一路涨到 SRAM 窗口尽头。真要用 malloc，应该显式给一个
`__heap_limit__` 并实现一个会失败返回的 `_sbrk`，而不是让它默默越界。

**2. 线程安全**

`configUSE_NEWLIB_REENTRANT = 0`。newlib 的 `malloc` 用 `__malloc_lock` /
`__malloc_unlock` 保护堆，默认实现是空的。多任务同时 `malloc` 会损坏堆。
最省的补法：

```c
void __malloc_lock(struct _reent *r)   { (void)r; vTaskSuspendAll(); }
void __malloc_unlock(struct _reent *r) { (void)r; (void)xTaskResumeAll(); }
```

注意这两个函数**不能**在调度器启动前被调用（`vTaskSuspendAll` 有前提），
所以要么保证不在 pre-scheduler 阶段 malloc，要么用
`xTaskGetSchedulerState()` 加一道判断。

`strtok` / `rand` / `errno` 这些还需要 per-task 的 `_reent`，那才需要打开
`configUSE_NEWLIB_REENTRANT=1`，代价是每个 TCB 增大约 0x200 字节，
并且要正确初始化 `_impure_ptr`。

**3. `.data` 装载从"碰巧不需要"变成必需**

实测 `libc_a-impure.o` 里 `_impure_ptr`（4 B）和 `_impure_data`（0x200 B）
都是**带初值的 `.data`**，且带重定位。也就是说一旦链进 newlib，
§13.6 里那条"谁复制 `.data`"就从潜在风险变成必然故障 ——
`_impure_ptr` 为 0 时任何 newlib 调用都会解引用空指针。

MCU0 目前把 `.data*` 并进 `.text` payload、由 bootloader 整块复制，所以没问题。
MCU1 现在也一并进了 payload（§13.6 的修复），但仍要确认 loader 是复制整个
payload 还是只到 `__text_end__`。

**4. 栈预算**

newlib-nano 的 `vfprintf` 大约要 1 KB 栈，完整 newlib 更多。
当前 `configMINIMAL_STACK_SIZE = 128`（512 B）不够，任何调用 `printf` 家族的任务
至少要 512 words（2 KB）。别忘了 `%f` 还会再多要一截，而 §11.3 说过
这个参数的单位是 `StackType_t` 不是字节。

**5. 链接选项**

`MCU0_LIBS` 里没有 `-specs=nano.specs`（只有那个已经不用的旧 `INC_LIBS` 有）。
不加就会拉进完整 newlib，代码体积差好几 KB。工具链里
`nano.specs` 确实存在（已确认路径）。浮点格式化还要额外
`-u _printf_float`，否则 `%f` 静默打不出来。

**顺带一个坑**：旧 `INC_LIBS` 里有 `-lrdimon`，那是 semihosting 库。
它把 IO 走调试器通道，**没有调试器连着时会直接挂死**，绝不能进产品镜像。
现在 `MCU0_LIBS` 没带它，保持这样。

**6. IO stub**

`_write` / `_read` / `_close` / `_fstat` / `_isatty` / `_lseek` / `_exit`。
`-lnosys` 提供了弱实现（返回错误），链接能过，但要让 `printf` 真的出字
必须自己实现 `_write` 接到 UART。考虑到日志已经有轮询/缓冲双路径，
`_write` 应当转发给 `Lic_Log`，而不是再开一条独立的 UART 访问路径。

**7. `assert.h`**

标准 `assert()` 会拉进 `__assert_func` → `fprintf` → 整个 stdio。
要么提供自己的 `__assert_func`，要么继续只用 `configASSERT`
（当前落到 `Lic_OsAssertFailed()`，会记录文件行号并停机）。

### 25.3 建议：不要引入 stdio

理由是它换不来东西。你已经有 `Lic_Log`：自带格式化、分级、tick 时间戳、
调度器前后自动切换传输路径、panic 时能排空缓冲。`printf` 能多做的只有
`%f` 和宽度/精度修饰符，代价是 6 KB 上下的代码、一个需要加锁的 `malloc`、
每个用它的任务多 1 KB 栈，外加上面七项里的每一项都得维护正确。

真正需要动态内存时，比补 `_sbrk` + 两个 lock 更省的做法是把它接到已有的堆：

```make
MCU0_LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc
```

```c
void *__wrap_malloc(size_t n) { return pvPortMalloc(n); }
void  __wrap_free(void *p)    { vPortFree(p); }
```

这样只有一个堆、一套统计（`xPortGetMinimumEverFreeHeapSize()`），
线程安全由 FreeRTOS 自己保证，也不需要 `end` / `_sbrk` / `__malloc_lock`。
需要 `memcpy`/`memset`/`strlen` 这类纯函数就直接用，它们无状态、可重入，
本来就已经在链接了。

如果最终还是决定上完整 newlib，上面第 1、2、3 项是硬前提，
少任何一项都会表现为"偶发的内存损坏"而不是干净的链接错误。
