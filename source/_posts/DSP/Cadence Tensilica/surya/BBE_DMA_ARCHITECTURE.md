# BBE iDMA 数据传输架构完整说明

> 本文档整合了 BBE (Range Azimuth Accelerator) FFT Core 到 DSP 的完整数据传输链路，
> 涵盖 FFT 输出接口、iDMA 描述符配置、环形缓冲机制、以及 Complex/Dual Real 两种模式。
>
> **配置基准**: `fft_size = 4096`, `MAG2_SIZE = 4096`, `CHIRPS_PER_CHIRPFRAME = 4`

---

## 目录

1. [系统架构概览](#一系统架构概览)
2. [FFT Core 输出接口](#二fft-core-输出接口)
3. [iDMA 描述符配置](#三idma-描述符配置)
4. [Complex Mode vs Dual Real Mode](#四complex-mode-vs-dual-real-mode)
5. [环形缓冲机制](#五环形缓冲机制)
6. [数据处理配合](#六数据处理配合)
7. [完整参数速查表](#七完整参数速查表)

---

## 一、系统架构概览

### 1.1 数据流路径

```
BBE FFT Core (硬件)
    ↓ FFT + mag2 + autoscale
Output Interface (16-bit 流式输出)
    ↓ TrigOut (每块触发)
BBE Internal SRAM (src_ch0 / src_ch2)
    ↓ iDMA (Twait 门控 + fftshift)
DSP DRAM (chirp_buffer + chirp_metadata)
    ↓ processEngineUpdate
DSP 信号处理 (CFAR / 目标检测)
    ↓ IRQ
SP (Signal Processor)
```

### 1.2 核心机制一览

| 层次 | 机制 | 作用 |
|------|------|------|
| **描述符层** | 循环链表 | iDMA 硬件自动循环执行，无需软件干预 |
| **缓冲区层** | 固定槽位重用 | `chirp_buffer[4]` 反复覆盖，节省内存 |
| **流控层** | BBE 外部触发 + NumDescriptors | 防止数据覆盖，提供安全阀 |
| **同步层** | IRQ + 内存屏障 | 帧完成通知 + 缓存一致性 |

### 1.3 关键参数（fft_size = 4096）

```c
// main.c:104
uint16_t fft_size = 4096;

// mem.h:9,12
#define CHIRPS_PER_CHIRPFRAME   (4U)
#define MAG2_SIZE               (4096U)

// mem.h - 目标缓冲区，每 chirp 4096 × 2 = 8192 字节 = 8 KB
uint16_t chirp_buffer[CHIRPS_PER_CHIRPFRAME][MAG2_SIZE];   // 4 × 4096
meta_id_t chirp_metadata[CHIRPS_PER_CHIRPFRAME];           // 4 个元数据
```

---

## 二、FFT Core 输出接口

### 2.1 输出数据格式

- **数据位宽**: 16 bits（`int16_t` / `uint16_t`）
- **数据内容**: mag2 值（幅度平方）
- **组织方式**: 线性数组，按样本索引连续存储

```
BBE DSP DRAM Memory - int16 type vector (fft_size = 4096)
┌────┬────┬─────┬──────┬──────┬─────┬──────┐
│ 0  │ 1  │ ... │ 511  │ 512  │ ... │ 4095 │
├────┼────┼─────┼──────┼──────┼─────┼──────┤
│ Is0│ Is1│ ... │Is511 │Is512 │ ... │Is4095│
└────┴────┴─────┴──────┴──────┴─────┴──────┘
  |<-- 16 bits -->|

  |<-- iDMA T₀ (512 samples = 1024 bytes) -->|
                          |<-- iDMA T₁ -->|  ...
```

### 2.2 块浮点表示（Block Floating Point）

BBE 输出为 16-bit 数据 + 块指数，扩展动态范围：

```
真实值 = (int16_t 输出值) × 2^(bexp)
```

元数据随数据同步传输：
```c
// main.c:400-403 — DSP 使用 bexp 还原真实值
ra_data_bexp        = chirp_metadata[i].bexp;       // 数据块指数
ra_data_magsq_bexp  = chirp_metadata[i].mag_bexp;   // mag2 块指数
event.input_data->signals.ra_data_bexp        = &ra_data_bexp;
event.input_data->signals.ra_data_magsq_bexp  = &ra_data_magsq_bexp;
```

### 2.3 流式输出 + 触发门控

BBE 不是批量输出，而是流式：每完成一块数据（1024 字节）发出 `TrigOut`，
iDMA 收到 `TrigIn` 后传输该块。**iDMA 传输速度完全由 BBE FFT 输出速度控制**。

```
BBE FFT Core ──完成 1024 字节──> TrigOut
                                    ↓
iDMA Data Desc (Twait=ExtTriggerNeeded) ──收到 TrigIn──> 传输该块 ──> TrigOut
```

---

## 三、iDMA 描述符配置

配置函数 `DMABufferConfigComplexMode` / `DMABufferConfigDualRealMode`
（bbe_dma_adapter.c）构建 iDMA 描述符链表。

### 3.1 传输参数计算（fft_size = 4096）

```c
output_real_samples    = fft_size;                          // 4096
dma_level              = 7;                                 // 每 1024 字节触发一次
transfer_size_in_bytes = (dma_level + 1U) * 128U;           // 8 × 128 = 1024 字节
transfer_block_qty     = (output_real_samples * 2U)
                         / transfer_size_in_bytes;          // (4096×2)/1024 = 8 块
*idma_descriptors_per_chirp = transfer_block_qty + 1U;      // 8 + 1 = 9 个描述符
```

> `× 2` 是因为每个样本占 2 字节（uint16_t）。

一个 chirp frame（main.c:118）：
```c
descriptors_per_chirpframe = CHIRPS_PER_CHIRPFRAME * descriptors_per_chirp;
                           = 4 * 9 = 36 个描述符
```

### 3.2 描述符链表布局

```
desc_buffer[]  (fft_size = 4096)
┌────────────────────────────────────────────┐
│ Chirp 0 (9 desc):                          │
│   [D0..D7]  8 个 Data Block (各 1024 bytes)│
│   [D8]      Metadata                        │
├────────────────────────────────────────────┤
│ Chirp 1: [D9..D17]   (9 desc)              │
│ Chirp 2: [D18..D26]  (9 desc)              │
│ Chirp 3: [D27..D35]  (9 desc, D35 IRQ)     │
├────────────────────────────────────────────┤
│ [D36] Loop Descriptor                       │
│       Control = &desc_buffer[0]  ───────────┼──┐
└────────────────────────────────────────────┘  │
        ▲                                        │
        └────────────────────────────────────────┘
             iDMA 自动循环（共 37 个描述符）
```

### 3.3 数据描述符配置

```c
// bbe_dma_adapter.c — 数据块描述符
descriptor_control.FieldType    = Type1D;
descriptor_control.PrivilegeSrc = Supervisor;
descriptor_control.PrivilegeDst = Supervisor;
descriptor_control.QoS          = LowPriority;
descriptor_control.Twait        = ExtTriggerNeeded;   // 等待 BBE 触发
descriptor_control.Trig         = SendTrigger;        // 完成后发送触发
descriptor_control.IRQ          = NoIRQonCompletion;  // 不产生中断

descriptor.SrcStartAdrs = src_addr;                   // 固定源（流式读取）
descriptor.RowBytes     = transfer_size_in_bytes;     // 1024 字节
```

### 3.4 元数据描述符配置

```c
// bbe_dma_adapter.c — 元数据描述符（每 chirp 末尾）
descriptor_control.Twait = NoExtTriggerNeeded;   // 无需触发
descriptor_control.Trig  = NoSendTrigger;

// 仅最后一个 chirp (j==3) 产生 IRQ，通知整帧完成
descriptor_control.IRQ = (j == 3U) ? IRQonCompletion : NoIRQonCompletion;

descriptor.SrcStartAdrs = src_meta_addr;
descriptor.DestStartAdrs = (Addr_t)(&chirp_metadata[j]);
descriptor.RowBytes = sizeof(meta_id_t);
```

### 3.5 循环终止描述符

```c
// bbe_dma_adapter.c:238-246 — 空描述符，Control 指回起点形成环
descriptor.Control       = (DMADescriptorField_t) &desc_buffer[0];
descriptor.SrcStartAdrs  = (Addr_t) 0;
descriptor.DestStartAdrs = (Addr_t) 0;
descriptor.RowBytes      = 0;
```

### 3.6 BBE 通道源地址映射

| bbe_id | 数据源 | 元数据源 | 说明 |
|--------|--------|----------|------|
| 0 | `src_ch0` | `src_meta_ch0` | 通道 0 (SRAM rxc0) |
| 1 | `src_ch0` | `src_meta_ch0` | 应为 SYSMEM，当前临时用 ch0（代码注释） |
| 2 | `src_ch2` | `src_meta_ch2` | 通道 2 (SRAM rxc1) |
| 3 | `src_ch2` | `src_meta_ch2` | 应为 SYSMEM，当前临时用 ch2（代码注释） |

### 3.7 fftshift（硬件卸载）

通过目标地址偏移，在 DMA 搬运时直接完成频谱中心化：

```c
uint16_t half_block_qty = transfer_block_qty / 2U;              // 8 / 2 = 4
uint16_t dest_block_idx = (i + half_block_qty) % transfer_block_qty;
descriptor.DestStartAdrs = (Addr_t)(
    ((int8_t *)(&chirp_buffer[j])) + (dest_block_idx * transfer_size_in_bytes)
);
```

**块映射（8 块）**:
```
源块索引:  [0] [1] [2] [3] [4] [5] [6] [7]
目标索引:  [4] [5] [6] [7] [0] [1] [2] [3]   dest = (i+4)%8
```

**频谱效果**:
```
源（FFT 自然输出）:  DC | +Freq → +Fs/2 | -Fs/2 ← -Freq
                     [0~2047]           [2048~4095]

fftshift 后:         -Fs/2 ← -Freq | DC=0 | +Freq → +Fs/2
                     [2048~4095]    ↑中心  [0~2047]
```

### 3.8 关键概念澄清

#### 3.8.1 transfer_block_qty 是什么？

**重要**：`transfer_block_qty` 是**块的数量**，不是单个块本身。

```c
// fft_size = 4096 时
transfer_size_in_bytes = 1024;                  // 单个块的大小
transfer_block_qty     = (4096 * 2) / 1024;     // = 8，一个 chirp 分成 8 个块
```

| 概念 | 大小 | 含义 |
|------|------|------|
| 单个块 | 1024 字节 = 512 样本 | ❌ 只是 **1/8 个 chirp** |
| transfer_block_qty 个块 | 8 × 1024 = 8192 字节 = 4096 样本 | ✅ **1 个完整 chirp** |

代码结构印证这一点：
```c
for (j = 0; j < CHIRPS_PER_CHIRPFRAME; j++)    // 外层：4 个 chirp
{
    for (i = 0; i < transfer_block_qty; i++)   // 内层：8 个块拼成 1 个 chirp
    {
        descriptor.RowBytes = 1024;             // 每块 1024 字节
        descriptor.DestStartAdrs = &chirp_buffer[j] + dest_block_idx * 1024;
    }
    // + 1 个 metadata 描述符
}
```

**传输过程**：BBE 每产生 512 个样本（1024 字节）发出一次 TrigOut，
iDMA 收到 TrigIn 后搬运这一块。需要 **8 次触发** 才能凑齐一个完整 chirp（4096 样本）。

#### 3.8.2 fftshift 硬件实现原理

这段代码的核心作用（bbe_dma_adapter.c:193-198）：

```c
uint16_t half_block_qty = transfer_block_qty / 2U;            // 8/2 = 4
uint16_t dest_block_idx = (i + half_block_qty) % transfer_block_qty;

descriptor.DestStartAdrs = (Addr_t)(
    ((int8_t *)(&chirp_buffer[j])) +
    (dest_block_idx * transfer_size_in_bytes)    // 使用偏移后的索引
);
```

**工作原理**：通过改变 DMA **目标地址偏移**，在搬运时直接重排数据顺序。

**块映射表（8 块示例）**：

| 源块 i | dest_block_idx = (i+4)%8 | BBE 输出样本范围 | chirp_buffer 目标位置 |
|--------|--------------------------|------------------|-----------------------|
| 0 | 4 | 0~511 | chirp_buffer[j][2048~2559] |
| 1 | 5 | 512~1023 | chirp_buffer[j][2560~3071] |
| 2 | 6 | 1024~1535 | chirp_buffer[j][3072~3583] |
| 3 | 7 | 1536~2047 | chirp_buffer[j][3584~4095] |
| 4 | 0 | **2048~2559** | chirp_buffer[j][**0~511**] |
| 5 | 1 | **2560~3071** | chirp_buffer[j][**512~1023**] |
| 6 | 2 | **3072~3583** | chirp_buffer[j][**1024~1535**] |
| 7 | 3 | **3584~4095** | chirp_buffer[j][**1536~2047**] |

**频谱搬移效果**：

```
BBE 自然输出顺序（FFT 标准输出）:
┌────────────────────┬────────────────────┐
│ [0~2047]           │ [2048~4095]        │
│ DC | +freq → +Fs/2 │ -Fs/2 ← -freq      │
└────────────────────┴────────────────────┘
       ↓ DMA 搬运时重排
chirp_buffer 中的顺序（fftshift 后）:
┌────────────────────┬────────────────────┐
│ [2048~4095]        │ [0~2047]           │
│ -Fs/2 ← -freq      │ DC=0 | +freq → +Fs/2│
└────────────────────┴────────────────────┘
          零频移到中心
```

**优势**：
- ✅ **零 CPU 开销**：fftshift 由 DMA 硬件完成，无需 DSP 额外计算
- ✅ **零额外内存**：无需临时缓冲区，直接写入最终位置
- ✅ **零时间损失**：与普通 DMA 传输速度相同（只改地址偏移）

如果没有这个优化，DSP 需要在 `chirp_buffer` 上额外执行：
```c
// 伪代码：软件 fftshift（现在不需要做）
for (i = 0; i < 2048; i++) {
    temp = chirp_buffer[i];
    chirp_buffer[i] = chirp_buffer[i + 2048];
    chirp_buffer[i + 2048] = temp;
}
```

#### 3.8.3 不是乒乓双缓冲

**澄清**：`DMABufferConfigComplexMode` **不是**传统的乒乓（双缓冲）设计。

**乒乓双缓冲**应该是：
```
Buffer A: 帧 N   （DSP 正在处理）
Buffer B: 帧 N+1 （iDMA 正在写入）
下一次交换：
Buffer A: 帧 N+2 （iDMA 写入）
Buffer B: 帧 N+1 （DSP 处理）
```

**实际实现**是单缓冲：
```c
// 只有一份缓冲区
uint16_t chirp_buffer[4][4096];    // 4 个 chirp，不是 2 份 buffer

// 目标地址始终指向同一块
descriptor.DestStartAdrs = &chirp_buffer[j];   // j ∈ {0,1,2,3}

// 循环描述符回到起点，再次写入同一块（覆盖旧数据）
descriptor.Control = &desc_buffer[0];  // 第 37 个描述符
```

**不覆盖的保障**：靠**生产者(BBE)限速** + **外部触发门控**，而非双缓冲。

| 机制 | 作用 | 是否防覆盖 |
|------|------|-----------|
| **BBE 外部触发** (`Twait=ExtTriggerNeeded`) | iDMA 必须等 BBE 产生数据才能搬 | ✅ **主要防护** |
| **BBE FFT 慢** (2ms/frame) >> **DSP 处理快** (0.33ms/frame) | 天然时间窗口 | ✅ 1.7ms 富余 |
| NumDescriptors 流控 | DSP 卡死时 iDMA 停止 | ⚠️ 安全阀（不是主要防护） |

**时序配合**（单缓冲安全的原因）：
```
BBE:   [帧 N FFT 计算, ~2ms              ][帧 N+1 FFT...]
            ↓ 8 次触发(512 样本/次)
            
iDMA:  [搬帧 N→chirp_buffer, ~12μs][阻塞等 BBE 触发.........][搬帧 N+1]
                                  ↓ 描述符循环回 Desc 0
                                  ↓ 但 Twait 阻塞，等不到 BBE 触发
                                  
DSP:                              [检测 NumDesc==214]
                                  [补充配额 +36]
                                  [内存屏障]
                                  [处理 chirp_buffer, ~0.33ms]
                                  
安全窗口: DSP 处理完成 (0.33ms) << BBE 下一帧第一个触发 (~2ms)
         → DSP 处理期间 iDMA 绝不会覆盖 chirp_buffer ✓
```

**如果要改成真正的乒乓双缓冲**（当前不需要）：
```c
// 需要两份缓冲区
uint16_t chirp_buffer_A[4][4096];
uint16_t chirp_buffer_B[4][4096];

// 描述符在 A、B 之间交替
for (frame = 0; frame < 2; frame++) {
    buffer = (frame == 0) ? chirp_buffer_A : chirp_buffer_B;
    for (j = 0; j < 4; j++) {
        descriptor.DestStartAdrs = &buffer[j];
        // ...
    }
}
```

当前设计不需要双缓冲，因为 BBE 触发门控已经足够安全且高效。

---

## 四、Complex Mode vs Dual Real Mode

### 4.1 核心结论

**从 DMA 配置的角度，两种模式完全相同。** 两个函数生成完全相同的描述符配置，
因为它们输出的数据量相同（均为 `fft_size` 个样本）。

### 4.2 为什么数据量相同？

**Complex Mode（复数模式）**:
- 输入: 1 路 4096 点复数信号 (I + jQ)
- 处理: 4096 点复数 FFT
- 输出: 4096 个 mag2 样本（复数 FFT 每个频点都独立）

**Dual Real Mode（双实模式）**:
- 输入: 2 路各 4096 点实数信号（编码为复数：real0→I, real1→Q）
- 处理: 4096 点复数 FFT + 频域分离
- 输出: 每路 4096/2 = 2048 点 → 2 路合计 **4096 个样本**

**关键：实信号频谱的共轭对称性**
```
实信号 DFT:  X[k] = X*[N-k]   (0 < k < N)
→ 负频率是正频率的镜像，冗余
→ 每路实信号只需保留 N/2 个独立频点（0 ~ Fs/2）
→ 2 路 × (N/2) = N，与 Complex Mode 相同
```

### 4.3 模式对比

| 特性 | Complex Mode | Dual Real Mode |
|------|--------------|----------------|
| 输入 | 1 路复数信号 | 2 路实数信号 |
| FFT 类型 | 4096 点复数 FFT | 4096 点复数 FFT + 频域分离 |
| 每路输出频点 | 4096 (完整频谱) | 2048 (仅正频率 0~Fs/2) |
| 输出总样本数 | 4096 | 2 × 2048 = 4096 |
| DMA 传输量 | 8 KB / chirp | 8 KB / chirp |
| **DMA 描述符配置** | **相同** | **相同** |
| 频谱范围 | -Fs/2 ~ +Fs/2 | 每路 0 ~ Fs/2 |

### 4.4 唯一的代码差异：注释

```c
// Complex Mode
//Complex FFT X -> X mag2 output samples (mag2 enabled)
output_real_samples = fft_size;

// Dual Real Mode
//Dual Real FFT: 2 real inputs (N each) -> N/2 mag2 per channel -> Total N output samples
output_real_samples = fft_size;
```

实际执行结果完全一致。RA 硬件模式由 `ra_top_mode_t` 枚举控制
（`RA_COMPLEX_MODE=0` / `RA_DUAL_REAL_MODE=1`），区别在 **BBE 硬件内部**如何生成这
4096 个样本，而非 DMA 如何传输。

### 4.5 数据布局差异（语义）

DMA 透明传输 4096 个 uint16 样本，但 chirp_buffer 中数据语义不同：

```
Complex Mode:
  chirp_buffer[j][0~4095] = 完整复数 FFT 频谱的 mag2（经 fftshift）

Dual Real Mode (取决于 BBE 输出格式):
  选项 1 交错: [R0_0, R1_0, R0_1, R1_1, ...]
  选项 2 连续: [R0: 0~2047 | R1: 0~2047]
```

### 4.6 当前使用

```c
// main.c:109-110 — Complex 被注释，当前使用 Dual Real
// idmaCfgResult = DMABufferConfigComplexMode(BbeIndex, fft_size, &descriptors_per_chirp);
idmaCfgResult = DMABufferConfigDualRealMode(BbeIndex, fft_size, &descriptors_per_chirp);
```

---

## 五、环形缓冲机制

### 5.1 三层环形结构

#### 5.1.1 描述符链表循环（硬件层）

37 个描述符形成循环链表，最后一个描述符的 `Control` 字段指回第 0 个描述符。
iDMA 硬件自动跟随链表，无需软件干预。

#### 5.1.2 目标缓冲区重用（数据层）

`chirp_buffer[4][4096]` 只有 4 个槽位，每个 8 KB。每次循环覆盖旧数据。

**问题**：如何避免 iDMA 覆盖正在被 DSP 处理的数据？

#### 5.1.3 外部触发门控（关键流控）

**数据描述符设置**:
```c
descriptor_control.Twait = ExtTriggerNeeded;   // 必须等待 BBE TrigOut
```

iDMA 不是自由传输，而是**受 BBE FFT 输出速度控制**：
- BBE 每产生 1024 字节（一块数据）→ 发出 TrigOut
- iDMA 收到 TrigIn → 传输该块
- BBE FFT 处理慢（~500 μs/chirp）>> DSP 处理快（~82 μs/chirp）
- **时间窗口**：DSP 总能在 BBE 生成下一帧前完成处理

### 5.2 NumDescriptors 流控机制

#### 5.2.1 寄存器定义

```c
// main.c:39-42
#define READ_IDMA_NUMDESC           XT_RER(0x00910000 + 0x0C)      // 读剩余配额
#define WRITE_IDMA_NUMDESCINCR(x)   XT_WER((x), 0x00910000 + 0x10) // 增加配额
```

#### 5.2.2 寄存器行为

- `NumDescriptors`: iDMA 允许处理的描述符数量（配额）
- 每处理完一个描述符: `NumDescriptors--`
- `NumDescriptors == 0` 时: iDMA **暂停**（即使链表还有描述符）
- 软件写 `WRITE_IDMA_NUMDESCINCR(n)`: `NumDescriptors += n`

#### 5.2.3 初始化与流控参数

```c
// bbe_dma_adapter.c:167, 280
ChirpFFT_descriptorQty = IDMA_MAX_DESCRIPTORS;  // 250
SetRegNumDescIncr(ChirpFFT_descriptorQty);      // 初始配额 = 250

// main.c:351
desc_end_qty = IDMA_MAX_DESCRIPTORS - descriptors_per_chirpframe;
             = 250 - 36 = 214   // 一帧完成时的检测点
```

**最大缓冲深度**: 250 / 36 ≈ **6.9 个 chirp frame**

### 5.3 工作流程详解

#### Step 1: 初始化
```c
SetRegDescStartAdrs((Register_t) &desc_buffer[0]);  // 描述符起始地址
SetRegNumDescIncr(250);                             // NumDescriptors = 250
SetRegControl.Enable = EnableDMA;                   // 启动
```

#### Step 2: BBE 开始生成数据
```
BBE 处理 Chirp 0:
  FFT 计算 → 产生第一个 1024 字节 → TrigOut
  
iDMA 响应:
  Desc 0 收到 TrigIn → 检查 NumDescriptors(250>0)✓ 
       → 传输 → NumDescriptors-- (249) → 发送 TrigOut
```

#### Step 3: 完成一个 Chirp Frame
```
BBE 完成 Chirp 0~3 (4 chirps):
  - 每 chirp: 8 数据块 + 1 元数据
  - iDMA 处理 Desc 0~35 (36 个描述符)
  - NumDescriptors: 250 → 214
  - Chirp 3 元数据完成 → IRQ 中断
```

#### Step 4: DSP 检测并处理（main.c:348-361）
```c
num_descriptors = READ_IDMA_NUMDESC;              // 读取当前值
desc_end_qty = 250 - 36;                          // = 214

while (num_descriptors != 214)                    // 精确等待 214
    num_descriptors = READ_IDMA_NUMDESC;

WRITE_IDMA_NUMDESCINCR(36);                        // 立即补充配额 → 250
XT_MEMW(); XT_EXTW();                               // 内存屏障

for (i = 0; i < 4; i++)                            // 处理 4 chirps
    processEngineUpdate(&core, &event);            // chirp_buffer[i]
```

#### Step 5: 并发执行（关键）
```
DSP 端:   处理 Frame N 的 chirp_buffer[0~3] (~328 μs)
iDMA 端:  到达 Desc 36 → 跳回 Desc 0，但 Twait 阻塞，等待 BBE 触发
BBE 端:   仍在计算下一帧 FFT（~2000 μs），未产生 TrigOut

→ DSP 处理期间 iDMA 阻塞在 Desc 0，不会覆盖 chirp_buffer[0] ✓
```

### 5.4 为什么单缓冲区安全？

**时序约束（fft_size = 4096 估算）**:
```
T_BBE_chirp ≈ 500 μs   (4096 点 FFT + mag2)
T_DSP_chirp ≈ 82 μs    (数据搬运 + 处理)
T_iDMA_chirp ≈ 3 μs    (8 KB @ 触发控制)

一个 frame:
  周期时间 = 4 × 500 μs = 2000 μs  (受 BBE 限制)
  DSP 处理 = 4 × 82 μs = 328 μs
  DSP 富余 = 2000 - 328 = 1672 μs (84% 空闲)

结论: DSP 处理速度 >> BBE 生成速度 → 单缓冲区安全
```

**真正防止覆盖的是 BBE 外部触发门控，而非 NumDescriptors。**
NumDescriptors 的作用是：
1. **安全阀**: DSP 卡死时（连续 ~7 帧不补充配额），iDMA 停止防止失控
2. **同步标记**: 通过 `NumDescriptors == 214` 精确判断帧完成（比 IRQ 更准）

---

## 六、数据处理配合

### 6.1 DSP 主循环（main.c while(1)）

```c
while (1)
{
    // 1. 等待一个 chirp frame 完成
    num_descriptors = READ_IDMA_NUMDESC;
    desc_end_qty = 250 - 36;                          // 214
    
    while (num_descriptors != desc_end_qty)
        num_descriptors = READ_IDMA_NUMDESC;
    
    // 2. 立即补充配额（关键：在处理前补充！允许 iDMA 继续）
    WRITE_IDMA_NUMDESCINCR(36);                        // NumDesc: 214 → 250
    
    // 3. 内存屏障（确保 DMA 数据对 DSP 缓存可见）
    XT_MEMW();                                          // 内存写屏障
    XT_EXTW();                                          // 外部内存写屏障
    
    // 4. 处理 4 个 chirp
    for (i = 0U; i < 4U; i++)
    {
        event.UniqueID.UniqueID = chirp_metadata[i].index;
        event.input_data->signals.ra_mag2 = &(chirp_buffer[i][0]);
        ra_data_bexp        = chirp_metadata[i].bexp;
        ra_data_magsq_bexp  = chirp_metadata[i].mag_bexp;
        event.input_data->signals.ra_data_bexp        = &ra_data_bexp;
        event.input_data->signals.ra_data_magsq_bexp  = &ra_data_magsq_bexp;
        
        status = processEngineUpdate(&core, &event);   // CFAR / 目标检测
        ASSERT(status == PENoError);
    }
    
    // 5. 向 SP 发送中断
    if (record_signaling == DSP2SP_IRQ)
        raiseInterruptDSPToSP();
    
    // 6. 记录性能
    PCurrentBbe->application_dsp.cycle_count_dsp.cycles = 
        TimerCounter_CalculateCycles(&timer_counter);
}
```

### 6.2 配合要点

1. **先补充配额，再处理数据**: 允许 iDMA 立即开始下一帧（虽然会被 BBE 触发阻塞）
2. **内存屏障必不可少**: DSP 缓存需与 DMA 写入的内存同步
3. **处理速度要求**: 必须在下一帧数据到达前完成（实际有 1.7 ms 富余）
4. **元数据同步**: bexp/mag_bexp 与数据一起传输，用于块浮点还原
5. **断言检查**: 验证 metadata 索引正确（main.c:381-384）

### 6.3 数据流水线

```
时间轴: ─────────────────────────────────────────→

BBE:    [Chirp N 处理 ~2ms]    [Chirp N+1 处理]    [Chirp N+2]
              ↓ TrigOut (8次)         ↓                  ↓
              
iDMA:         [传输 N (~12μs)]       [等待触发]         [传输 N+1]
                     ↓ IRQ                   
                     
DSP:                 [等待] [处理 N (~328μs)] [等待]   [处理 N+1]

NumDesc:    250→214 →补充→ 250 →214 →补充→ 250
```

**零拷贝**: BBE → chirp_buffer → DSP 处理，无额外拷贝  
**低延迟**: 单缓冲，DSP 处理最新数据  
**流水线**: BBE 计算、iDMA 传输、DSP 处理并行（不同帧）

---

## 七、完整参数速查表

### 7.1 配置参数（fft_size = 4096）

| 参数 | 计算 | 值 | 来源 |
|------|------|-----|------|
| fft_size | — | 4096 | main.c:104 |
| MAG2_SIZE | — | 4096 | mem.h:12 |
| CHIRPS_PER_CHIRPFRAME | — | 4 | mem.h:9 |
| output_real_samples | fft_size | 4096 | bbe_dma_adapter.c |
| dma_level | — | 7 | bbe_dma_adapter.c |
| transfer_size_in_bytes | (7+1)×128 | 1024 字节 | bbe_dma_adapter.c |
| transfer_block_qty | (4096×2)/1024 | 8 块 | bbe_dma_adapter.c |
| descriptors_per_chirp | 8+1 | 9 | bbe_dma_adapter.c |
| descriptors_per_chirpframe | 4×9 | 36 | main.c:118 |
| total_descriptors | 36+1 | 37 | — |
| half_block_qty | 8/2 | 4 | bbe_dma_adapter.c |
| IDMA_MAX_DESCRIPTORS | — | 250 | bbe_dma_adapter.c:40 |
| desc_end_qty | 250-36 | 214 | main.c:351 |
| 最大缓冲深度 | 250/36 | ≈6.9 帧 | — |

### 7.2 数据量

| 项目 | 计算 | 值 |
|------|------|-----|
| 每 chirp 数据量 | 4096×2 | 8 KB |
| 每 chirp 传输块 | — | 8 块 |
| 每 frame 数据量 | 4×8 KB | 32 KB |
| chirp_buffer 总大小 | 4×4096×2 | 32 KB |

### 7.3 fftshift 块映射（8 块）

| 源块 i | dest = (i+4)%8 | 源样本范围 | 目标样本范围 |
|--------|----------------|-----------|-------------|
| 0 | 4 | 0~511 | 2048~2559 |
| 1 | 5 | 512~1023 | 2560~3071 |
| 2 | 6 | 1024~1535 | 3072~3583 |
| 3 | 7 | 1536~2047 | 3584~4095 |
| 4 | 0 | 2048~2559 | 0~511 |
| 5 | 1 | 2560~3071 | 512~1023 |
| 6 | 2 | 3072~3583 | 1024~1535 |
| 7 | 3 | 3584~4095 | 1536~2047 |

### 7.4 描述符控制字段

| 字段 | 数据描述符 | 元数据描述符 |
|------|-----------|-------------|
| FieldType | Type1D | Type1D |
| PrivilegeSrc/Dst | Supervisor | Supervisor |
| QoS | LowPriority | LowPriority |
| Twait | ExtTriggerNeeded | NoExtTriggerNeeded |
| Trig | SendTrigger | NoSendTrigger |
| IRQ | NoIRQonCompletion | j==3 时 IRQonCompletion |
| RowBytes | 1024 | sizeof(meta_id_t) |

---

## 附录：关键设计要点

✅ **零拷贝流式传输**: BBE → SRAM → DMA → chirp_buffer → DSP，无冗余拷贝  
✅ **硬件 fftshift**: 通过 DMA 目标地址偏移实现频谱中心化，卸载 CPU  
✅ **硬件循环链表**: iDMA 自动循环，无需软件重配描述符  
✅ **外部触发门控**: BBE FFT 输出速度是瓶颈，天然防止数据覆盖  
✅ **单缓冲高效**: DSP 处理（328μs）<< BBE 生成（2ms），84% 富余  
✅ **模式统一**: Complex/Dual Real 共享 DMA 配置，区别仅在 BBE 内部  
✅ **块浮点**: 16-bit 数据 + bexp 指数，扩展动态范围  

⚠️ **潜在风险**:
- DSP 处理变慢可能导致数据覆盖（当前有充足裕量）
- BBE 输出加速会缩短处理窗口（需重新评估时序）
- 增大 chirp frame 需检查描述符总数 < 250
