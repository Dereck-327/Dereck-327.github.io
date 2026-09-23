


## notebook

### OCP

OCP（Open Core Protocol）是一种面向片上系统 IP 互连的标准接口协议，常用于连接芯片内部的 DSP、硬件加速模块、存储控制器以及第三方 IP 核。与 ARM 的 AMBA 体系（AXI/AHB）并列，它强调的是“统一接口定义”和“模块化集成”。

#### 1. 常见片上总线/接口协议概览

| 协议 | 组织/来源 | 典型特征 | 典型应用 |
| --- | --- | --- | --- |
| AMBA AXI | ARM | 高性能、高并发、支持乱序、读写通道分离 | CPU、DDR、NPU、大带宽主干网络 |
| AMBA AHB | ARM | 结构简单、控制逻辑清晰、适合中速控制总线 | SRAM、GPIO、DMA、寄存器访问 |
| OCP | OCP-IP 联盟（现属 Accellera） | 以 Socket/接口抽象为核心，可配置、灵活、跨厂商 IP 集成友好 | DSP、专用加速器、跨厂商 IP 互联 |

#### 2. OCP 的核心特点

1. 点对点接口独立性
   OCP 把 IP 核的功能逻辑与片上互连网络（Interconnect / NoC）解耦，允许不同厂商的模块以统一接口接入系统。

2. 高度可配置
   OCP 不强制绑定固定的物理总线拓扑，可以配置成简单的单次读写接口，也可以扩展成支持 Burst、Pipelined、Out-of-order 的高性能接口。

3. 统一管理
   它既支持带内（In-band）的地址、数据、控制信号，也兼容旁路调试和 Sideband 信号传输，从而更适合复杂 SoC 的模块化管理。

#### 3. AXI / AHB / OCP 对比

| 特性 | AMBA AXI | AMBA AHB | OCP |
| --- | --- | --- | --- |
| 制定组织 | ARM | ARM | OCP-IP 联盟（现 Accellera） |
| 总线架构 | 读写通道分离（AR/AW/R/W/B） | 共享单总线，地址与数据复用 | Socket 抽象，接口可配置 |
| 传输模式 | 非阻塞、乱序、支持 Threading | 流水线、按顺序处理 | 高度可定制，支持点对点、乱序、流控 |
| 并发能力 | 极高，支持多 Outstanding 事务 | 较低，同一时刻通常只能处理一个主事务 | 取决于配置，可从简单 AHB 级别扩展到 AXI 级别 |
| 典型场景 | CPU、DDR、NPU、大带宽主干网络 | SRAM、GPIO、DMA、控制总线 | 跨厂商 IP、专用 DSP、加速器互联 |

#### 4. 详细协议分析与从设备（Secondary / Slave）行为模式

这里的 Secondary 通常指在总线事务中被动接收请求并响应的节点，例如 SRAM、DDR 控制器、寄存器接口等。它对应到 AXI/AHB 里的 Slave，也可以理解为 OCP 里的 Target。

##### 4.1 AXI（Advanced eXtensible Interface）

AXI 是当前高性能 SoC 内部总线的主流方案。它的核心创新是将传输拆分成 5 个独立通道：

- 读地址通道：AR
- 读数据通道：R
- 写地址通道：AW
- 写数据通道：W
- 写响应通道：B

AXI 从设备的核心特点：

1. 读写全双工
   AXI 从设备可以同时接收写数据并输出读数据，读写逻辑完全解耦，带来更高吞吐率。

2. 支持乱序响应
   从设备依靠 ARID / AWID 跟踪不同事务。若 DDR 先收到慢速读地址 A，随后收到缓存命中的读地址 B，则可以优先返回 B 的数据，再返回 A 的数据，从而尽量减少 Head-of-Line Blocking。

3. 突发传输只传首地址
   主设备在 AR/AW 通道只发送首地址和 burst 长度（AxLEN），从设备内部地址生成逻辑会自动计算后续 Beat 地址，显著降低地址通道开销。

##### 4.2 AHB（Advanced High-performance Bus）

AHB 是上一代经典的片上总线，属于“单总线/共享总线”结构。虽然采用了两级流水线（地址与数据重叠），但在任意时钟周期内，整个总线仍然只能处理一个事务。

AHB 从设备的核心特点：

1. 严格按序响应
   AHB 从设备必须按照主设备发起请求的顺序依次响应。如果处理较慢，则拉低 HREADY，插入等待状态（Wait States），整个总线后续请求会被强制挂起。

2. 控制与数据紧耦合
   - T1 周期：主设备发出 HADDR、HTRANS、HWRITE 等地址和控制信号。
   - T2 周期：从设备采样地址，同时驱动或采样 HWDATA / HRDATA。

3. SPLIT / RETRY 机制
   在旧版 AHB 中，如果从设备需要长时间准备数据，可以发出 SPLIT 响应，让总线仲裁器把总线使用权切给其他 Master，避免死锁；在现代 AHB-Lite 中，这个机制被简化了。

##### 4.3 OCP（Open Core Protocol）

与 ARM 独占的 AMBA 体系不同，OCP 是一种独立于总线拓扑的 Socket 接口规范，强调“标准化接口”和“跨厂商模块互联”。

OCP 从设备的核心特点：

1. 模块化与高度可配置
   OCP 不强制绑定具体的物理总线结构，既可配置成极简的单周期接口，也可扩展为带 Tag、Threads、Sideband 的高性能接口。

2. 基于 Request / Response 机制
   OCP 的请求命令（MCmd）可包含 Read、Write、Broadcast 等指令；从设备通过 SResp 返回 NULL（等待）、DVA（Data Valid / Ack）、ERR（错误）或 FAIL 等状态。

3. 原生的多线程与 Tag 支持
   与 AXI 的 ID 类似，OCP 通过 MThreadID / SThreadID 和 MTag 支持并发请求和非按序响应，适合高性能 DSP、车载 SoC 或专用加速器互联场景。

#### 5. 总结：如何选择与适配？

在现代复杂 SoC 中，这三种协议通常混合并存：

- 核心计算与大内存链路：采用 AXI4，利用高并发和乱序处理能力最大化 DDR 带宽。
- 中低速控制节点：采用 AHB-Lite，结构简单、门数较小、时序容易收敛。
- 跨模块 IP 套接字转换：使用 OCP 做模块间抽象封装，再通过 Bridge 转接到 AXI / AHB 主干网络。

因此，AXI 适合高性能 backbone，AHB 适合控制面和中速外设，OCP 更适合作为跨厂商、跨模块的标准接口适配层。


### HSADC (High-Speed Analog-to-Digital Converter)

高速模数转换器，负责将输入的连续模拟信号（如电压、电流、传感器高频采样信号）快速转换成数字信号，供芯片内部的 CPU（如 Cortex-M7）或 DSP（如 Tensilica ConnX）进行计算处理。
核心特点：
1. 高采样率：通常采样率在数兆赫兹（MSPS）到数百兆赫兹（GSPS）级别。
2. 低延迟：用于对实时性要求极高的场景（如车载以太网 PHY 信号采样、电机控制相位检测、高频电流监测等）。
3. 典型应用：在车载/网关芯片（如 BCM8915X 系列或类似 MCU）中，HSADC 常用于物理层（PHY）信号诊断、快速过流/过压保护、高精度电机相电流采样等。

### HSREF (High-Speed Reference / High-Side Reference)

High-Speed Reference（高速基准源 / 高速参考电压）
作用：
HSADC 转换模拟信号时，需要一个极其稳定、低噪声且具备高动态响应特性的参考电压（$\text{V}_{\text{REF}}$）。HSREF 就是专为 HSADC 或高速比较器提供的高速基准信号/电路。  
为什么需要“高速”基准？
普通 ADC 的基准电压主要要求“静态精度高”（直流稳定）。而 HSADC 采样速度极快，在开关电容抽样瞬间会向基准源抽取突发电流（Dynamic Current Spikes）。如果基准源响应太慢，会导致基准电压下陷（Drop），从而严重降低 ADC 的转换精度（THD 和 ENOB）。HSREF 具备高驱动能力和快速建立时间（Fast Settling Time），能确保 HSADC 在高速转换过程中基准电压保持绝对稳定。

### HSAFE (High-Speed Analog Front End)

高速模拟前端，是连接外部物理接口（如车载以太网双绞线、高频传感器）与芯片内部数字处理核心（DSP/CPU）之间的模拟信号预处理桥梁。  
核心组成：通常包含低噪声放大器（LNA）、可编程增益放大器（PGA）、抗混叠滤波器（AAF）、驱动器（Driver）以及配套的高速模数转换器（HSADC）。
主要作用：  
信号调理：对外部输入的微弱或受干扰的高频模拟信号进行放大、滤波、降噪和阻抗匹配。
模数转换接口：将调理后的连续模拟信号输送给 HSADC 转换为数字报文，供物理层（DSP）进行基带信号解调。
在 Broadcom 车载芯片中的应用：在 Broadcom BroadR-Reach / Automotive Ethernet (100BASE-T1 / 1000BASE-T1) 芯片中，HSAFE 负责处理双绞线上传输的高频 PAM3/PAM4 编码模拟信号。

### ACQCMN (Acquisition Common)

采集通用/公共控制模块，是负责管理和协调多个数据采集通道（Acquisition Channels）及 HSAFE/HSADC 运行的集中式控制与时序逻辑单元。
核心作用：
通道调度与多路复用（Multiplexing & Scheduling）：管理多个 HSAFE 输入通道对 HSADC 资源的共享与轮询采样。
时序与同步（Timing & Triggering）：为各采集通道提供统一的触发信号、采样时钟同步以及相位对齐，确保多通道数据采样的时空一致性。
基准与校准管理（Reference & Calibration）：配合 HSREF（高速基准源）等模块，对各个 AFE 通道进行直流偏置（DC Offset）校准、增益校准及自动增益控制（AGC）的公共逻辑调度。
配置寄存器接口：向总线（如 AXI/AHB）提供统一的配置寄存器视图（RDB），方便 CPU 对所有采集通道进行集中式参数配置。


# HSADC

## CCT

Capture/Compare Timer（捕获/比较定时器）
CCT 是芯片内部专门用来做定时、计数、脉冲捕获与 PWM 信号生成的硬件外设模块
Capture Mode（捕获模式）： 当外部引脚触发指定信号（如上升沿/下降沿）时，CCT 会瞬间将此时 UDC 计数器（Up/Down Counter，加/减计数器）的值锁存（Capture）到寄存器中，常用于测量外部信号的频率、脉冲宽度或周期。

Compare Mode（比较模式）： 内部硬件会实时将 UDC 计数器的当前值与预设的比较寄存器值进行比对，当两者相等时，硬件会自动触发特定的动作（如翻转 GPIO 引脚电平生成 PWM 信号，或触发 DMA/中断）。




  重新生成镜像时，把 image_q8 作为文件传给 num=3（当前它是空的）：

  sudo python3 -E $RIGELSW/utils/create_image.py -f \
    -P a55_boot_img_unsigned.bin,num=0,format=raw,size=2M,type=A2,offset=64 \
    -P Image.gz,rigel-initramfs-rigel.cpio.gz,bcm89158.dtb,format=vfat,num=1,size=24M \
    -P format=ext4,num=2,./rootfs/*,size=56M \
    -P format=ext4,num=3,./rootfs/data/image_q8_core0.bin,./rootfs/data/image_q8_core1.bin,./rootfs/data/image_q8_core2.bin,./rootfs/data/image_q8_core3.bin,size=4M \
    -s 90M \
    -n a55_disk.img


    