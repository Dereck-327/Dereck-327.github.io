---
title: BBE32EP note      
data: 2026-06-16 13:41:56
tags: [dsp]           
categories: [cadence tensilica]            
description: connx bbe32ep dsp 
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg
---
# ConnX BBE32EP

BBE: Baseband Engine
32: 32路 MAC（乘累加）能力
    - 这意味着 DSP 在一个时钟周期内，其 SIMD（单指令多数据）向量执行单元可以并行完成 32个 16-bit × 16-bit 的复数或实数乘累加运算。这是衡量其通信基带处理吞吐量最核心的指标。
EP: Enhanced Performance（增强性能版）或 Extended Precision（扩展精度版）
    - 相对于前代（如 BBE32），“EP” 版本引入了更强大的硬核功能，特别强化了对浮点数（Floating-Point）的支持（包括单精度 Float32 和半精度 Float16/BF16），并且极大地优化了多天线阵列、雷达信号处理中所必须的矩阵运算（Matrix Operations）和向量穿梭（Vector Shuffle）效率。

## SIMD && VLIW

Cadence BBE 同时使用了 SIMD + VLIW，因此一个 Cycle 内既可以执行多条不同指令，每条指令又可能同时处理多个数据。  

### SIMD

SIMD = Single Instruction Multiple Data（单指令多数据）  

普通 CPU：  
```c
for(i=0;i<8;i++)
    c[i]=a[i]+b[i];
```
普通执行过程：  
```txt
Cycle1
a0+b0

Cycle2
a1+b1

Cycle3
a2+b2

...
```
8 次加法。  

SIMD 可以变成  
```txt
Cycle1

[a0 a1 a2 a3]
+
[b0 b1 b2 b3]

↓

[c0 c1 c2 c3]
```
如果 SIMD 宽度是 8：  
```txt
a0~a7

+

b0~b7

↓

一次全部完成
```

### VLIW

一个 Cycle 执行多条指令

| unit | SLOT0 | SLOT1 | SLOT2 | SLOT3 | SLOT4 |
|------|-------|-------|-------|-------|-------|
| func | Load/Store Unit | Load Unit (LSU2) | Vector ALU | ALU | Move Unit |
| description | Load & Store operations | Load operations (2nd LSU) | ADD SUB MAC Multiply ops | Pure ALU operations | Move operations |

#### VLIW Slot 详解

**SLOT0 - Load/Store Unit (LSU1)**
- 主要用于 loads 和 stores 操作
- 是主要的 LSU（Load-Store Unit），负责数据在寄存器和内存之间的搬运
- 可同时支持 load 或 store 操作

**SLOT1 - Load Unit (LSU2)**  
- 专属第二个 LSU，主要用于 load 操作
- 用于在 SLOT0 已经被占用时，进行额外的数据加载
- 提升内存访问并行度

**SLOT2 - Vector ALU**
- 处理向量算术逻辑运算，特别是带乘法的操作
- 支持 ADD、SUB、MAC（乘累加）等向量运算
- 是 DSP 数据通路的核心部分

**SLOT3 - Pure ALU**
- 纯粹的标量 ALU 操作
- 负责控制流、地址计算等不涉及向量的操作
- 处理非向量的算术和逻辑运算

**SLOT4 - Move Unit**
- 负责数据搬运和重排操作
- 支持 Permutation、Shuffle、Select、Compare 等向量操作
- 用于向量元素间的数据重组和变换



## BBE_SIMD_WIDTH

```h
#define BBE_SIMD_WIDTH 0x10
```

含义：Xtensa DSP的向量宽度 = 16个16位元素一个向量寄存器可以同时存储：

- 16 × 16bit = 256bits（一条指令处理16个数据）

## MOVVI

MOVVI: Move Vector Immediate(向量立即数赋值指令)
在向量处理器中，为了实现单指令多数据（SIMD），一条指令需要同时将同一个立即数加载到向量寄存器的多个分量（Slots/Elements）中

| Symbolic Name      | Immediate Argument | Value Assigned | Description |
| ------------------ | ------------------ | -------------- | ----------- |
| BBE_MOVVI_INT16_M1 | -1                 | 32'hFFFF_FFFF  | int16 -1    |
| BBE_MOVVI_ZERO     | 0                  | 32'h0000_0000  | zero        |
| BBE_MOVVI_INT16_1  | 1                  | 32'h0001_0001  | int16 +1    |

> Immediate Argument（立即数参数）： 汇编指令中书写的原始数值（如 -1, 0, 1），通常存储在指令码的有限位（如 5位、7位或 16位）中。
> Value Assigned（底层分配值）： 32位（32'h...）的十六进制表示。这不是整个向量寄存器的宽度，而是立即数经过硬件符号位扩展（Sign Extension）或复制拼接（Replication）后，填入一个 32位数据单元（或两个16位插槽）中的实际二进制形态。
> Int16 / Zero 描述： 明确了该操作是针对 16位有符号整数（int16） 向量操作进行优化的。

e.g. BBE_MOVVI_INT16_M1

- 符号名： BBE_MOVVI_INT16_M1 (Move Vector Immediate Int16 Minus 1)
- 立即数： -1
- 分配值： 32'hFFFF_FFFF
- 详细解释：

  - 在计算机中，-1 的 16位有符号补码是 16'hFFFF。由于该指令旨在初始化一个 16位的向量，32位的数据空间被切分为两个 16位的通道（Low 16-bit 和 High 16-bit）。硬件将 -1 的 16位补码同时复制到了高 16位和低 16位中：

  $$
  ext{High 16-bit} = \text{0xFFFF} \quad | \quad \text{Low 16-bit} = \text{0xFFFF}
  $$

  - 拼接在一起就形成了 32'hFFFF_FFFF

## Vector Shuffle Operations

Vector Shuffle Operations: 向量穿梭/向量重排

当执行向量计算时，数据在内存中的存储顺序（如连续的采样点）往往不能直接满足算法的计算需求（如 FFT 的蝶形运算、复数交织与解交织、矩阵转置）。Shuffle 操作的目的就是不用重新读写内存，直接在寄存器层面打乱并重新排列向量内部的元素顺序。

## Programming in Prototypes

### extract protos

提取出的内联函数/原语声明原型
这是 Tensilica 编译工具链中一个非常独特且关键的自动化机制。简单来说，它是编译器为了让你能在 C/C++ 代码中直接调用 BBE32EP 硬件特有的向量指令和 TIE（Tensilica Instruction Extension）扩展指令，而自动生成的 Intrinsics（内联函数原型）头文件。

为了让开发者不用写繁琐且易错的汇编代码，Tensilica 提供了一套机制：

1. 硬件设计师用 TIE 语言定义好 BBE32EP 的硬件指令。
2. 工具链中的开发工具会自动提取（Extract）这些指令在 C 语言中对应的函数原型（Prototypes）。
3. 最终生成一个或多个 .h 头文件，这就是 Extract Protos。

Example: The following code shows the use of an Extract proto; in this case, to extract the real values from a complex vector.

```
// Complex conjugate multiplies 
xb_c40 cxprod = ycin[i] * ~ hcin[i]; 
xb_c40 hcxabs = hcin[i] * ~ hcin[i]; 
// Extract real magnitude squared for channel 
xb_int40 habs = BBE_EXTRACTR_FROMC40(hcxabs);
```

### Combine Protos

合并原型

在 TIE（Tensilica Instruction Extension）语言和工具链中，当不同的硬件模块、协同处理器（Coprocessor）或自定义扩展指令被分别定义后，工具链需要将这些分散的、面向不同硬件切片的 C 语言内联函数原型（Prototypes）进行合并与归网，这个过程或生成的产物被称为 Combine Protos。

- Extract Protos（提取）： 负责把单一 TIE 模块（例如单独的 BBE32 向量加速器）的硬件指令“抽离”并转化为 C 语言的 extern 函数声明。
- Combine Protos（合并）： 将这些提取出来的多个独立头文件（如基础 Boolean 扩展、标准输入输出端口扩展、以及特定的 BBE32 向量算法扩展）组合、封装或链接到一个统一的编译上下文或特定的宏定义文件中（例如生成最终的全局配置头文件），确保编译器（如 xt-clang）能够无冲突地、完整地识别所有并行硬件流水线的指令原型。

Combine Protos 确保了这些不同硬件单元的数据类型原型（如标量 int、40位高精度复数 xb_c40、以及 512 位宽向量 vint16x32）可以在同一个 C 文件中自由地进行数据类型转换、抽取（如 BBE_EXTRACTR_FROMC40）和参数传递，而不会引发编译器的符号冲突。

如果在特定的 Makefile/构建脚本中看到了 combine_protos 或类似的工具链命令，它的主要作用通常是刷新编译环境。

### Move Protos && Operator Overload Protos

Move Protos 和 Operator Overload Protos 是工具链自动生成的两类关键内联函数（Intrinsic）原型文件。

#### Move Protos

是指专门负责在不同类型的寄存器之间、标量与向量之间、以及内存与寄存器之间进行数据搬运和转换的内联函数原型集合。

##### 🛠️ 核心解决的问题

在 BBE32EP 架构中，存在多种不同形态的寄存器：

- 标准的 32 位标量寄存器（AR 寄存器）
- 40 位的高精度累加器/复数寄存器（如存储 xb_c40）
- 512 位的超宽向量寄存器（如存储 vint16x32）

硬件上，把数据从标量寄存器复制到向量寄存器，或者在不同的向量通道间进行广播，需要调用非常特定的硬件电路。Move Protos 就是这些搬运指令的 C 语言接口。

在生成的头文件中，Move Protos 通常包含以下几类操作：

- 广播移动 (Broadcast Move)： 将一个普通的 C 语言变量（如 int a = 5;）的值，复制并充满整个向量寄存器的所有通道。这就是前面提到的 MOVVI 指令在 C 语言层面的函数原型。
- 协同处理器间移动 (Coprocessor Move)： 将标量核心计算出的索引值或配置参数，移动到 BBE32EP 向量控制寄存器中（例如 XT_WUR_... 或 XT_RUR_... 读写用户寄存器原语）。
- 类型强制移动 (Cast/Format Move)： 将 40 位复数寄存器中的低位直接搬移到另一个标量寄存器，不进行算术修改，纯粹是数据通路的切换。

#### Operator Overload Protos（运算符重载原型）

专门为 C++ 开发 准备的原型文件。它利用 C++ 的运算符重载（Operator Overloading）特性，将前面提到的硬件内联函数（如 XT_VCONNX_ADD(...)）包装成人类更易读的通用运算符（如 +、-、*）。

##### 🛠️ 核心解决的问题

在纯 C 语言中，如果你想让两个 xb_c40 类型的复数相乘，并加上另一个复数，由于 C 语言不支持运算符重载，你必须写出极其臃肿的代码：

```c
// 纯 C 语言形态：可读性差，极易写错参数
xb_c40 result = XT_CONNX_MULA(ycin, XT_CONNX_CONJ(hcin), accum);
```

而在 C++ 中，有了 Operator Overload Protos 的支持，编译器在后台把这些底层的 Intrinsics 映射到了标准的数学运算符上。

```cpp
// C++ 形态：简洁直观
xb_c40 cxprod = ycin[i] * ~ hcin[i];
```

## BBE宏名解析

### BBE_SVNX16U_IP

Store vector of 16-way 16-bit elements, addressing: base address from address register, offset from immediate, with base address update post-increment

BBE_SVNX16U_IP
    ├─ S = Store
    ├─ V = Vector
    ├─ NX = 可变长度
    ├─ 16 = 16位
    ├─ U = Unsigned（无符号）
    └─ IP = Immediate Post-increment

### BBE_MULUUNX16

16-way 16-bit unsigned multiply - producing wide (40-bit) results with sign-extension

### BBE_SRANX40

使用来自向量移位/选择寄存器的有符号移位量（双向）进行 16 路 40 位移位运算
BBE_SRANX40 是一个 16 路移位操作，目标向量是一个 16 路 40 位元素的向量。输入向量是一个宽向量寄存器 wvr。输出结果写入宽向量寄存器 wvt。移位量由输入向量移位/选择量寄存器 sr 决定。在这种情况下，输入宽向量寄存器 wvr 中的每个元素都可以应用不同的移位量。移位量被限制在 -40 到 40 的范围内（低于或高于此范围的值会被限制）。这些是双向移位。正移位量将沿名称所暗示的方向进行移位。负移位量将沿名称所暗示的相反方向进行移位。零移位将输入保持不变地传递到输出。所应用的移位类型及其方向均为右移运算。

### BBE_PACKLNX40

BBE_PACKLNX40 是一个内部函数，用于调用 BBE_PACKLNX40 指令。
BBE_PACKLNX40 指令对来自 wvec 输入寄存器 wvp 的 16 个 40 位输入元素进行打包，不进行移位、舍入或饱和操作。每个元素的最低 16 位被提取出来，并按顺序写入输出窄向量寄存器 vt。

### BBE_LANX16_IP

BBE_：基带/向量计算引擎指令。
LA：Load Align（对齐加载）。这是它与普通 LV (Load Vector) 的本质区别。
NX16：Narrow 向量，16 个通道，处理 16 位有符号元素（共 256 位/32 字节）。
_IP：Increment Pointer。指令执行完后，基地址寄存器 ars 自动隐式加 32 字节（因为加载宽度是 32 字节），自动指向下一个紧邻的数据块。

假设你的起始地址偏离了 $k$ 个字节（$k$ 即地址的低 5 位，例如 0x03）。你想要的 32 字节数据实际上跨越了两个对齐的 32 字节物理内存块（Block A 和 Block B）。硬件的处理流水线如下：

- 强制对齐物理读取：硬件自动把地址低 5 位清零（地址变回对齐线），把当前的物理块（Block B）整体读出来。
- 利用 valign 跨界拼接：此时，你想要的完整数据，前半部分其实躺在上一次读完留下的 valign 寄存器（uul，保存着旧的 Block A）的高位里；后半部分躺在刚刚读进来的新物理块（Block B）的低位里。
- 滑动窗口横移：硬件内部的移位拼接器（Shifter）立刻开动：
  $$
  \text{输出到 vrul} = \text{新读入物理块的低 } k \text{ 字节} \ + \ \text{uul 寄存器的高 } (32 - k) \text{ 字节}
  $$
- 状态迭代（Prime 预热）：拼接完成后，刚读进来的新物理块（Block B）会强行覆盖写入 uul 寄存器。这样，当循环进入下一轮、执行下一个 _IP 指令时，Block B 就变成了“旧数据”，静静等待与未来的 Block C 进行拼接。

第一次调用该指令时，由于 uul 里面是垃圾数据，拼出来的值是错的（这个过程叫 Prime/预热流水线）。但从第二次循环开始，uul 永远精准保留着上一个周期的物理块。
此时，伴随着 _IP 每次自动自增 32 字节，DSP 可以在每一个时钟周期内，一边往下读一个新物理块，一边跟 uul 拼出一个非对齐向量送给算法。由于有 valign 在底层做硬件挡箭牌，非对齐的内存访问在算法层被彻底隐形了，而且做到了零时延开销

### BBE_LANX16_PP

同[BBE_LANX16_IP](#BBE_LANX16_IP)_PP：Plus/Post-Increment Pointer (with register offset)。这是它的灵魂所在。

- 之前的 _IP 后缀，地址自增是隐式且死板的 32 字节（c + 0，固定向前移动一个向量宽度）。
- 现在的 _PP 后缀，允许你传入一个标量寄存器或者动态改变的步长（Stride）来更新基地址。

_PP (Plus) 与 _IP (Increment) 的选型战场
在实际编写高性能 DSP 算法（如 Symmetric FIR 滤波器 或 DFT/FFT 二维矩阵转置）时，选 _IP 还是 _PP 决定了你能不能把流水线压榨到极致：

| 场景   | 访问模式          | 推荐指令      | 选择原因                                                             | 典型例子                                                                       |
| ------ | ----------------- | ------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| 场景 A | 数据连续存储      | BBE_LANX16_IP | 步长固定为 32 字节，硬件隐式加 32，不需要额外占用标量寄存器`art`。 | 时域连续音频样点或射频 IQ 流                                                   |
| 场景 B | 非连续/跳跃式访问 | BBE_LANX16_PP | 需要可变步长（Stride）按寄存器动态更新地址。                         | 下采样（例如步长 64 字节）、按列读取二维矩阵、FFT 蝶形或多天线 MIMO 的跨行访问 |

### BBE_SETDUALMAX

初始化Xtensa的硬件最大值追踪寄存器：
  ├─ MAX寄存器 = MIN_INT16 (-32768)
  ├─ MAXIDX寄存器 = 0
  └─ 这些是内部硬件寄存器，自动跟踪最大值

初始化“单/双峰值搜索（Single/Dual Peak Search）”硬件状态机的专有指令
BBE 内部有 5 个专门为峰值搜索定制的寄存器。因为当前机器的 SIMD 宽度 $N = 16$（即 512 位宽），所以这些寄存器被物理切分为 $\frac{16}{2} = 8$ 个并行的通道（Lanes）：

| 寄存器名称  | 数量/通道 | 单通道位宽 | 物理意义与存储内容                                       | 初始化状态（由本指令执行）                |
| ----------- | --------- | ---------- | -------------------------------------------------------- | ----------------------------------------- |
| BBE_MAX     | 8 个元素  | 32-bit     | 缓存当前搜索到的**第一大（最大）**的 8 个局部峰值。      | 全部赋值为`arr`（通常传入 `INT_MIN`） |
| BBE_MAX2    | 8 个元素  | 32-bit     | 缓存当前搜索到的**第二大（次大）**的 8 个局部峰值。      | 全部赋值为`arr`（通常传入 `INT_MIN`） |
| BBE_MAXIDX  | 8 个元素  | 12-bit     | 存放 BBE_MAX 中 8 个最大值分别对应的时间/样本索引位置。  | 全部清零（0）                             |
| BBE_MAXIDX2 | 8 个元素  | 12-bit     | 存放 BBE_MAX2 中 8 个次大值分别对应的时间/样本索引位置。 | 全部清零（0）                             |
| BBE_IDX     | 1 个标量  | 11-bit     | 全局计数器，用来记录当前循环走到了第几个样本点。         | 清零（0）                                 |

为了节省硬件，指令将 16 个通道两两成对（例如通道 0 和 1 归为第 0 个 Lane-Pair），因此状态寄存器的通道数减半（变为 $16 / 2 = 8$ 个通道）

### BBE_DMAXNX16

```c
#include <xtensa/tie/xt_bbe32.h>
extern void BBE_DMAXNX16(xb_vecNx16 a, xb_vecNx16 b);
```

D = Dual（双向量）
MAX = 最大值
NX16 = 不定长16位向量

功能：

- 比较 a 和 b 中的所有元素
- 自动更新硬件的MAX和MAXIDX寄存器

> 协同指令链：
> `BBE_GTMAXNX16` & `BBE_MOVDUALMAXT`： 用于将 `a` 的结果（`BBE_MAX`）和 `b` 的结果（`BBE_MAX2`）进行跨向量的横向整合与比较，合并成一套统一的峰值结果。
> `BBE_SELMAXIDX`： 结合最终留下的 `BBE_MAXIDX`（或 `BBE_MAXIDX2`）中的相对索引值与全局计数器 `BBE_IDX` 的值，反推出该最大值在原始庞大数组中的绝对内存索引地址（Absolute Index）。

### BBE_GTMAXNX16

```c
#include <xtensa/tie/xt_bbe32.h>
extern vboolN BBE_GTMAXNX16(void);
```

峰值搜索指令集中的跨向量结果整合与比较指令
在通过 `BBE_DMAXNX16` 指令处理完大批量的数据后，a 向量的局部最大值被保留在 BBE_MAX 中，b 向量的局部最大值被保留在 BBE_MAX2 中。BBE_GTMAXNX16 的核心任务就是对这两组状态寄存器进行横向大小比对，并将结果映射回一个标准的 16 通道向量布尔寄存器（Vector Boolean Register, vbr）中。

整个指令的逐通道行为可以映射如下：

| 比较操作 (32-bit Signed)                   | 条件是否成立 | 输出布尔通道 vbr[2i] (偶数) | 输出布尔通道 vbr[2i+1] (奇数) |
| ------------------------------------------ | ------------ | --------------------------- | ----------------------------- |
| $\text{BBE\_MAX2}_0 > \text{BBE\_MAX}_0$ | 是 / 否      | vbr[0] = true / false       | vbr[1] = true / false         |
| $\text{BBE\_MAX2}_1 > \text{BBE\_MAX}_1$ | 是 / 否      | vbr[2] = true / false       | vbr[3] = true / false         |
| $\text{BBE\_MAX2}_2 > \text{BBE\_MAX}_2$ | 是 / 否      | vbr[4] = true / false       | vbr[5] = true / false         |
| $\text{BBE\_MAX2}_3 > \text{BBE\_MAX}_3$ | 是 / 否      | vbr[6] = true / false       | vbr[7] = true / false         |
| $\text{BBE\_MAX2}_4 > \text{BBE\_MAX}_4$ | 是 / 否      | vbr[8] = true / false       | vbr[9] = true / false         |
| $\text{BBE\_MAX2}_5 > \text{BBE\_MAX}_5$ | 是 / 否      | vbr[10] = true / false      | vbr[11] = true / false        |
| $\text{BBE\_MAX2}_6 > \text{BBE\_MAX}_6$ | 是 / 否      | vbr[12] = true / false      | vbr[13] = true / false        |
| $\text{BBE\_MAX2}_7 > \text{BBE\_MAX}_7$ | 是 / 否      | vbr[14] = true / false      | vbr[15] = true / false        |

在整个双峰值搜索算法流程中，BBE_GTMAXNX16 扮演着决策生成器的角色：

- 产生掩码（Mask）： 它生成的 vbr 寄存器实际上是一个选择掩码（Selection Mask）。如果 vbr 的某一对通道为 true，说明在这一轮或这一组数据的对比中，vs 向量分支产生的值比 vr 向量分支更大。
- 指导后续数据移动： 该指令输出的 vbr 会紧接着被 `BBE_MOVDUALMAXT` 指令用作控制条件。`BBE_MOVDUALMAXT` 会根据 vbr 中的布尔值，决定是将 BBE_MAX2/BBE_MAXIDX2 的值搬移融合，还是保留 BBE_MAX/BBE_MAXIDX 的值。

### BBE_MOVDUALMAXT

```c
#include <xtensa/tie/xt_bbe32.h>
extern void BBE_MOVDUALMAXT(vboolN a);
```

峰值搜索指令集中的数据融合与归并（Merge/Conditional Move）指令。在上一阶段，BBE_GTMAXNX16 指令已经通过比较 BBE_MAX2 和 BBE_MAX 的大小，生成了一个 16 通道的布尔掩码寄存器 vbr。本指令 BBE_MOVDUALMAXT 的核心任务就是根据 vbr 的指示，将 vs 向量分支的获胜结果（最大值和索引）有条件地合并到 vr 向量分支的状态寄存器中，从而完成两组向量的最终交汇。以下是该指令的详细工作原理和数据流向解析：

1. 核心控制逻辑：

   - 偶数通道对齐正如前两条指令所提及，状态寄存器（BBE_MAX 等）只有 8 个通道，而输入的布尔寄存器 vbr 有 16 个通道。- 为了精确定位控制条件，该指令采用“仅检查 vbr 偶数通道（Even Elements）”的寻址映射规则。
   - 对于状态寄存器的第 $j$ 个通道（$j = 0, 1, \dots, 7$），它对应的 vbr 控制位是第 $i = 2j$ 个通道（显然 $i$ 永远是偶数，且 $j = i / 2$）。
2. 详细数据移动行为对于每一个通道 $j$（从 $0$ 到 $7$），令 vbr 的偶数通道索引 $i = 2j$。指令同时对数值（MAX）和索引（MAXIDX）执行以下条件赋值操作：

   1. 最大值（MAX）的合并

      - 条件成立 (If vbr[i] is true): 说明在该通道位置上，vs 分支的值更大。因此，将 BBE_MAX2 的值覆盖到 BBE_MAX 中。

      $$
      text{BBE\_MAX}[j] \leftarrow \text{BBE\_MAX2}[j]
      $$

      - 条件不成立 (Otherwise):
      - 说明 vr 分支的原值更大或相等。保持原状，不作修改。

   $$
   text{BBE\_MAX}[j] \leftarrow \text{BBE\_MAX}[j]
   $$

   2. 相对索引（MAXIDX）的合并与最大值的移动完全同步，为了保证最大值与其索引的绝对对应：

      - 条件成立 (If vbr[i] is true):

      $$
      text{BBE\_MAXIDX}[j] \leftarrow \text{BBE\_MAXIDX2}[j]
      $$

      - 条件不成立 (Otherwise):

      $$
      text{BBE\_MAXIDX}[j] \leftarrow \text{BBE\_MAXIDX}[j]
      $$
   3. 逐通道数据映射流向表
      当指令执行时，8 个通道的内部合并逻辑如下表所示（注意 vbr 只读取偶数下标）：

   | 检查 vbr 偶数通道 | 状态寄存器目标通道 j=i/2 | 条件为 true 时的执行结果 (覆盖)                                                                | 条件为 false 时的执行结果 (保持)            |
   | ----------------- | ------------------------ | ---------------------------------------------------------------------------------------------- | ------------------------------------------- |
   | vbr[0]            | 通道 0 (j=0)             | $\text{BBE\_MAX}[0] = \text{BBE\_MAX2}[0]$$\text{BBE\_MAXIDX}[0] = \text{BBE\_MAXIDX2}[0]$ | 保持 BBE_MAX[0] 不变保持 BBE_MAXIDX[0] 不变 |
   | vbr[2]            | 通道 1 (j=1)             | $\text{BBE\_MAX}[1] = \text{BBE\_MAX2}[1]$$\text{BBE\_MAXIDX}[1] = \text{BBE\_MAXIDX2}[1]$ | 保持 BBE_MAX[1] 不变保持 BBE_MAXIDX[1] 不变 |
   | vbr[4]            | 通道 2 (j=2)             | $\text{BBE\_MAX}[2] = \text{BBE\_MAX2}[2]$$\text{BBE\_MAXIDX}[2] = \text{BBE\_MAXIDX2}[2]$ | 保持 BBE_MAX[2] 不变保持 BBE_MAXIDX[2] 不变 |
   | vbr[6]            | 通道 3 (j=3)             | $\text{BBE\_MAX}[3] = \text{BBE\_MAX2}[3]$$\text{BBE\_MAXIDX}[3] = \text{BBE\_MAXIDX2}[3]$ | 保持 BBE_MAX[3] 不变保持 BBE_MAXIDX[3] 不变 |
   | vbr[8]            | 通道 4 (j=4)             | $\text{BBE\_MAX}[4] = \text{BBE\_MAX2}[4]$$\text{BBE\_MAXIDX}[4] = \text{BBE\_MAXIDX2}[4]$ | 保持 BBE_MAX[4] 不变保持 BBE_MAXIDX[4] 不变 |
   | vbr[10]           | 通道 5 (j=5)             | $\text{BBE\_MAX}[5] = \text{BBE\_MAX2}[5]$$\text{BBE\_MAXIDX}[5] = \text{BBE\_MAXIDX2}[5]$ | 保持 BBE_MAX[5] 不变保持 BBE_MAXIDX[5] 不变 |
   | vbr[12]           | 通道 6 (j=6)             | $\text{BBE\_MAX}[6] = \text{BBE\_MAX2}[6]$$\text{BBE\_MAXIDX}[6] = \text{BBE\_MAXIDX2}[6]$ | 保持 BBE_MAX[6] 不变保持 BBE_MAXIDX[6] 不变 |
   | vbr[14]           | 通道 7 (j=7)             | $\text{BBE\_MAX}[7] = \text{BBE\_MAX2}[7]$$\text{BBE\_MAXIDX}[7] = \text{BBE\_MAXIDX2}[7]$ | 保持 BBE_MAX[7] 不变保持 BBE_MAXIDX[7] 不变 |

至此，由三个指令构成的完整双峰值搜索（Dual Peak Search）核心流程已经清晰：

- BBE_DMAXNX16 (数据输入与初筛)： 将输入向量 vr 和 vs 的数据不断流式输入，在各自的通道对里两两PK，分别把阶段性最大值和相对索引留在 BBE_MAX/IDX（对应 vr）和 BBE_MAX2/IDX2（对应 vs）中。
- BBE_GTMAXNX16 (跨向量大比拼)： 比较两边的结果（BBE_MAX2 > BBE_MAX），生成一个 16 通道的布尔掩码 vbr。由于上一指令的映射特性，vbr 的奇偶通道对（如 0 和 1）具有相同的布尔值。
- BBE_MOVDUALMAXT (结果收尾融合)： 本指令登场，读取 vbr 的偶数通道。如果为 true，就把 MAX2 分支由于更优而胜出的数值和索引，打包移入 MAX 分支。

### BBE_RMINNX16

R: reducte

### BBE_MOVVA

带有 `MOVVA` 或 `MOVV` 尾缀且指定了元素大小（如 `A16`、`A32`）的指令，其核心功能之一就是 Splat（标量广播到矢量）

### BBE_GENX16

BBE_GENX16(v1, v2): 返回 v1 > v2 的布尔向量

### BBE_SEQNX16

返回序列向量

### BBE_SHFLNX16I

按选项改变向量序列

| Select Index | Description                | Element Selection (Least Significant, ..., Most Significant)   |
| ------------ | -------------------------- | -------------------------------------------------------------- |
| 0            | BBE_SHFLI_REP_0X4          | 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3                 |
| 1            | BBE_SHFLI_REP_1X4          | 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7                 |
| 2            | BBE_SHFLI_REP_2X4          | 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11         |
| 3            | BBE_SHFLI_REP_3X4          | 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15 |
| 4            | BBE_SHFLI_SWAP_1           | 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14           |
| 5            | BBE_SHFLI_SWAP_2           | 2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13           |
| 6            | BBE_SHFLI_SWAP_4           | 4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11           |
| 7            | BBE_SHFLI_SWAP_8           | 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7           |
| 8            | BBE_SHFLI_DOUBLE_1_LO      | 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7                 |
| 9            | BBE_SHFLI_DOUBLE_1_HI      | 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15     |
| 10           | BBE_SHFLI_DOUBLE_2_LO      | 0, 1, 0, 1, 2, 3, 2, 3, 4, 5, 4, 5, 6, 7, 6, 7                 |
| 11           | BBE_SHFLI_DOUBLE_2_HI      | 8, 9, 8, 9, 10, 11, 10, 11, 12, 13, 12, 13, 14, 15, 14, 15     |
| 12           | BBE_SHFLI_DOUBLE_4_LO      | 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7                 |
| 13           | BBE_SHFLI_DOUBLE_4_HI      | 8, 9, 10, 11, 8, 9, 10, 11, 12, 13, 14, 15, 12, 13, 14, 15     |
| 14           | BBE_SHFLI_DUPLICATE_1_EVEN | 0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14           |
| 15           | BBE_SHFLI_DUPLICATE_1_ODD  | 1, 1, 3, 3, 5, 5, 7, 7, 9, 9, 11, 11, 13, 13, 15, 15           |
| 16           | BBE_SHFLI_DUPLICATE_2_EVEN | 0, 1, 0, 1, 4, 5, 4, 5, 8, 9, 8, 9, 12, 13, 12, 13             |
| 17           | BBE_SHFLI_DUPLICATE_2_ODD  | 2, 3, 2, 3, 6, 7, 6, 7, 10, 11, 10, 11, 14, 15, 14, 15         |
| 18           | BBE_SHFLI_DUPLICATE_4_EVEN | 0, 1, 2, 3, 0, 1, 2, 3, 8, 9, 10, 11, 8, 9, 10, 11             |
| 19           | BBE_SHFLI_DUPLICATE_4_ODD  | 4, 5, 6, 7, 4, 5, 6, 7, 12, 13, 14, 15, 12, 13, 14, 15         |
| 20           | BBE_SHFLI_REVERSE_1        | 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0           |
| 21           | BBE_SHFLI_REVERSE_2        | 14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1           |
| 22           | BBE_SHFLI_REVERSE_4        | 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3           |
| 23           | BBE_SHFLI_REP_2X4_OFFSET_0 | 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3                 |
| 24           | BBE_SHFLI_REP_2X4_OFFSET_1 | 4, 5, 4, 5, 4, 5, 4, 5, 6, 7, 6, 7, 6, 7, 6, 7                 |
| 25           | BBE_SHFLI_REP_2X4_OFFSET_2 | 8, 9, 8, 9, 8, 9, 8, 9, 10, 11, 10, 11, 10, 11, 10, 11         |
| 26           | BBE_SHFLI_REP_2X4_OFFSET_3 | 12, 13, 12, 13, 12, 13, 12, 13, 14, 15, 14, 15, 14, 15, 14, 15 |

### BBE_MOVNX16T(v1, v2, b)

16-way 16-bit move for predicated TRUE elements
条件移动	`if(b[i]) result[i]=v1[i] else v2[i]`

### BBE_RMAXNX16

R	Reduction（规约）	意味着该操作将一个多元素的矢量输入，“浓缩/规约”为一个单一的目标值。

1. 规约（Reduction）的直观理解在普通的 SIMD（单指令多数据）矢量加法中，通常是两个矢量“对应通道”相加，输出还是一个矢量：$[A, B, C, D] + [E, F, G, H] = [A+E, B+F, C+G, D+H]$（通道数量没变）而规约加法（Reduction Add）打破了通道的边界。它是将同一个矢量内部的所有元素，像做串联加法一样全部加在一起，最终融合成一个孤零零的结果：$\text{Result} = A + B + C + D$（多通道浓缩成单通道）
2. 

### BBE_ZALIGN

Z = Zero（零初始化）
ALIGN = 对齐状态

BBE_ZALIGN() 初始化为：
  ├─ offset = 0
  ├─ buffer = empty
  └─ sync = ready

### BBE_SAVNX16_XP

```c
#include <xtensa/tie/xt_bbe32.h>
extern void BBE_SAVNX16_XP(xb_vecNx16 a, valign b /*inout*/, xb_vecNx16 * c /*inout*/, int d);
```

> SA: Store Aligned（存储对齐）
> VNX16: 向量16位
> _XP: eXtended Pointer increment（扩展的指针递增，以字节为单位）

1. `xb_vecNx16 a`: 数据向量，输入矢量（当前周期要处理的数据）（要写入内存的源向量寄存器数据）
2. `valign b`: 这不是一个普通变量，而是一个**对齐指针状态机**，矢量对齐控制字（inout，用于非对齐内存加载），用于追踪：├─ 当前的对齐偏移量├─ 待处理的缓冲数据├─ 存储操作的同步状态└─ 非对齐访问时的字节对齐处理
3. `xb_vecNx16 * c`： 累加器/目标矢量指针（inout，既是输入也是输出）
4. `int d`: 内存地址的后增量变址（Post-increment Offset）

### BBE_SANX16POS_FP

SA = Store Aligned
NX16POS = NX16 Position
_FP = Flush and Position（刷新和定位）

功能：

1. 刷新（flush）对齐缓冲区
2. 确保前面的所有写操作完成
3. 定位到正确的位置以进行下一轮写入

### int16_rtor_xb_vecNxc16

并不是一个直接的函数，而是一个命名极其规范的类型转换、寄存器映射（Register Transfer）或者内联转换宏的逻辑表述。

① int16_r （源类型/寄存器）

- int16：标准的 16 位有符号整型（int16_t）。
- _r：在 Tensilica 的底层硬件描述中，通常代表 Register（标量寄存器文件），具体来说是普通的 32 位标量寄存器（AR 寄存器），内部存放了一个 16 位的有效数据。

② tor （动作）

- 它是 "to"（走向/转换至） 的缩写，代表数据从左侧的标量环境流向右侧的 SIMD 矢量环境。

③ `xb_vecNxc16` （目标类型）这是 ConnX BBE32EP 中最核心的复数矢量数据类型：

- `xb_`：Xplorer / Xtensa Base 针对 ConnX 扩展的前缀。
- `vec`：Vector（矢量寄存器）。
- `N`：代表矢量中包含的元素数量（取决于具体配置，1024位满宽度下可以容纳 32 个复数，则 $N=32$）
- `xc`：代表 Complex（复数）。意味着每个元素由 Real（实部） 和 Imag（虚部） 两个通道组成。
- `16`：代表实部和虚部各自占据 16位 宽度。因此一个 c16 复数元素实际占用 32 位（16b R + 16b I）

在代码中触发这个转换逻辑（通常表现为 Initialization 或 Cast/Splat 宏）时，硬件会将一个标量的 16 位实数，转换并填充到一个超宽的 16 位复数矢量寄存器中。由于源数据是一个标量实数（只有实部，没有虚部），为了匹配目标复数矢量的格式，硬件和编译器通常会采用“广播 + 虚部置零”（Splat with Zero Imaginary）的策略：

- 虚部填充 0：将标量的 int16 作为复数的实部（Real），虚部（Imag）自动补 0，拼成一个 (int16_t, 0) 的标准复数单元。
- 全矢量广播（Splat）：将这个拼好的复数单元，复制到目标矢量寄存器 `xb_vecNxc16` 的所有 $N$ 个复数通道中。

### BBE_RADDNX40

16-way 40-bit signed sum reduction

### BBE_OPERATOR_SLLI40

矢量逻辑左移（Vector Shift Left Logical）专用硬件算子

### BBE_SAVNX16U_XP

同[BBE_SAVNX16_XP](#bbe_savnx16_xp)

## BBE DATA Type

### vsaN

向量状态寄存器数据类型（Vector Shift Accumulator Status Data Type）在 BBE32EP 这种超宽 SIMD 架构中，一条向量算术右移指令（比如 vint32x16 的右移）要求 16 个并行通道同时进行移位。

- 如果芯片设计师允许用一个普通的 32 位标量寄存器（AR 寄存器）直接驱动 16 个通道移位，在硬件电路上会面临毁灭性的扇出（Fan-out）时延危机：一个标量寄存器的信号要跨越很长的物理芯片边界，同时驱动 16 组巨大的算术移位器（Barrel Shifters），这会导致时钟频率根本上不去。
  为了解决这个问题，Tensilica 在向量执行单元（Vector Execution Unit）的内部，紧挨着 16/32 路乘加器（MAC）和移位器，设计了一个专用的状态寄存器硬件阵列，在软件语义上对应的类型就是 vsaN。
- 物理位置： 它不在普通的标量寄存器堆里，也不在通用的 512 位向量寄存器堆里。它是一个影子控制器/协处理器状态寄存器。
- 核心职责： 它专门用来存储、缓冲和同步当前向量流水线所需的移位位数（Shift Amount）和舍入/饱和（Rounding/Saturation）控制标志。

```txt
prime (一次):     读块0 → valign存 in[1..15] → 指针到块1
                          ↓
IP (i=0):  读块1 → 拼 in[1..15]+in[16] = in[1..16] → valign存 in[17..31] → 指针到块2
                          ↓
IP (i=1):  读块2 → 拼 in[17..31]+in[32] = in[17..32] → valign存 in[33..47] → 指针到块3
                          ↓
IP (i=2):  ...连续流式，每次一个向量...
```

### BBE_MAGINX16C

并行计算 16 个 16-bit 复数元素的模长平方（Magnitude Squared），并交错写入一个宽向量寄存器中。

#### 数据结构

**输入格式：**

- 使用两个 256-bit 窄向量寄存器 $v_r$ 和 $v_s$。
- 每个寄存器包含 8 个 32-bit 复数元素（共计 $8 + 8 = 16$ 个复数）。
- 单个复数元素格式：低 16-bit 为实部（Real），高 16-bit 为虚部（Imaginary），均为 16-bit 有符号整数。

**输出格式：**

- 输出到 1 个 640-bit 宽向量寄存器 $wv_t$。
- 包含 16 个 40-bit 实数元素（经过符号扩展/防溢出处理）。

#### 计算逻辑

对第 $i$ 个输入复数元素，其输出的模长平方计算公式为：

$$
\text{result}_i = (\text{real}_i)^2 + (\text{imag}_i)^2
$$

即 $|Z|^2 = \text{Re}^2 + \text{Im}^2$。

#### 交错映射（Interleaving）

两个输入寄存器 $v_r$ 和 $v_s$ 中的元素在输出寄存器 $wv_t$ 中按交叉交错方式排列：

| 输出索引 | 数据来源 | 计算公式 |
|---------|---------|--------|
| Element 0 | $v_r[0]$ | $(v_r[0].\text{re})^2 + (v_r[0].\text{im})^2$ |
| Element 1 | $v_s[0]$ | $(v_s[0].\text{re})^2 + (v_s[0].\text{im})^2$ |
| Element 2 | $v_r[1]$ | $(v_r[1].\text{re})^2 + (v_r[1].\text{im})^2$ |
| Element 3 | $v_s[1]$ | $(v_s[1].\text{re})^2 + (v_s[1].\text{im})^2$ |
| ... | ... | ... |

模式：奇数索引来自 $v_s$，偶数索引来自 $v_r$，按顺序交错。


## 谓词

谓词（Predicate） 是一种控制指令是否真正执行的布尔条件，可以理解为一位（或一组）真假标志，用于避免分支跳转，提高流水线效率。



# 相关

[Floating-Point](floating_point.md)
