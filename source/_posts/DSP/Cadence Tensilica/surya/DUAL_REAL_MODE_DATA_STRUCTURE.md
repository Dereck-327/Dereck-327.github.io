# Dual Real Mode 数据结构与 I/Q 分离详解

## 概述

本文档详细说明在 Dual Real Mode 下，DMA 搬运来的数据结构，以及如何理解和处理这些数据。

**配置基准**：`fft_size = 4096`, `ra_top_mode = RA_DUAL_REAL_MODE`, `enable_magsq = true`

---

## 一、Dual Real Mode 工作原理

### 1.1 信号编码与处理流程

```
输入：两路实数 ADC 信号
┌─────────────────────────────────────────────────────────────┐
│ real0[0..4095]  - 第一路实数信号（例如：TX0 的接收信号）    │
│ real1[0..4095]  - 第二路实数信号（例如：TX1 的接收信号）    │
└─────────────────────────────────────────────────────────────┘
                    ↓ 复数编码
┌─────────────────────────────────────────────────────────────┐
│ x[n] = real0[n] + j*real1[n]   (n = 0..4095)               │
│ 组合成一个 4096 点的复数信号                                 │
└─────────────────────────────────────────────────────────────┘
                    ↓ 4096 点复数 FFT
┌─────────────────────────────────────────────────────────────┐
│ X[k] = FFT{x[n]}   (k = 0..4095)                           │
│ 复数频谱                                                     │
└─────────────────────────────────────────────────────────────┘
                    ↓ 频域分离
┌─────────────────────────────────────────────────────────────┐
│ Real0[k] = (X[k] + X*[N-k]) / 2        (real0 的频谱)       │
│ Real1[k] = (X[k] - X*[N-k]) / (2j)     (real1 的频谱)       │
└─────────────────────────────────────────────────────────────┘
                    ↓ 利用共轭对称性，只保留正频率
┌─────────────────────────────────────────────────────────────┐
│ Real0[0..2047]  - real0 的频谱，0 ~ Fs/2                   │
│ Real1[0..2047]  - real1 的频谱，0 ~ Fs/2                   │
│ （实信号负频率是冗余的，X[k] = X*[N-k]）                    │
└─────────────────────────────────────────────────────────────┘
                    ↓ mag2 计算
┌─────────────────────────────────────────────────────────────┐
│ mag2_real0[k] = |Real0[k]|²   (k = 0..2047)                │
│ mag2_real1[k] = |Real1[k]|²   (k = 0..2047)                │
└─────────────────────────────────────────────────────────────┘
                    ↓ BBE 输出到 SRAM
┌─────────────────────────────────────────────────────────────┐
│ 总共 4096 个 uint16_t mag2 值                               │
└─────────────────────────────────────────────────────────────┘
                    ↓ iDMA 搬运
┌─────────────────────────────────────────────────────────────┐
│ chirp_buffer[j][0..4095]  (j = 0..3)                       │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 关键公式

**频域分离公式**（BBE 内部实现）：
```
已知: X[k] = FFT{real0[n] + j*real1[n]}

分离:
  Real0[k] = (X[k] + conj(X[N-k])) / 2
  Real1[k] = (X[k] - conj(X[N-k])) / (2j)

其中 conj() 表示共轭，N = 4096
```

**共轭对称性**（实信号的特性）：
```
对于实信号的 DFT:
  X[k] = X*[N-k]   (0 < k < N)

因此:
  X[0]      - DC 分量（实数）
  X[1..2047] - 正频率分量
  X[2048]   - Nyquist 频率（实数）
  X[2049..4095] - 负频率分量（与正频率共轭对称，冗余）

只需保留 k = 0..2047 即可表示完整信息
```

---

## 二、DMA 搬运的数据结构

### 2.1 当前配置下的输出

根据代码配置（system_config.c）：
```c
.enable_magsq           = true,   // 输出 mag2（幅度平方）
.enable_magsq_autoscale = true,   // mag2 自动缩放
.enable_full_res_magsq  = false   // 不输出全分辨率 mag2
```

**结论**：BBE 输出的是 **mag2 值**（uint16_t），不是复数频谱。

### 2.2 数据排列方式（两种可能）

chirp_buffer[j][0..4095] 中存储的 4096 个 uint16_t，可能按以下方式排列：

#### 选项 1：交错存储（Interleaved）

```
chirp_buffer[j][]:
┌────┬────┬────┬────┬────┬────┬─────┬─────┐
│ 0  │ 1  │ 2  │ 3  │ 4  │ 5  │ ... │ 4095│
├────┼────┼────┼────┼────┼────┼─────┼─────┤
│R0_0│R1_0│R0_1│R1_1│R0_2│R1_2│ ... │R1_2047│
└────┴────┴────┴────┴────┴────┴─────┴─────┘
  ↑    ↑    ↑    ↑
 real0 real1 real0 real1
 freq0 freq0 freq1 freq1

索引:
  chirp_buffer[j][2*k]   = |Real0[k]|²   (k = 0..2047)
  chirp_buffer[j][2*k+1] = |Real1[k]|²   (k = 0..2047)
```

#### 选项 2：连续存储（Contiguous）

```
chirp_buffer[j][]:
┌──────────────────────────┬──────────────────────────┐
│ 0 .. 2047                │ 2048 .. 4095             │
├──────────────────────────┼──────────────────────────┤
│ Real0[0..2047] 的 mag2   │ Real1[0..2047] 的 mag2   │
└──────────────────────────┴──────────────────────────┘
        第一路信号                  第二路信号

索引:
  chirp_buffer[j][k]      = |Real0[k]|²   (k = 0..2047)
  chirp_buffer[j][k+2048] = |Real1[k]|²   (k = 0..2047)
```

### 2.3 如何确定实际排列方式？

**方法 1：查阅 BBE 硬件手册**  
查看 Range Azimuth Accelerator 的 Dual Real Mode 输出格式规范。

**方法 2：实验验证**  
```c
// 在 main.c 的处理循环中添加调试代码
if (chirp_metadata[0].index == 0) {  // 只打印第一帧
    printf("chirp_buffer[0][0..7]: ");
    for (int k = 0; k < 8; k++) {
        printf("%u ", chirp_buffer[0][k]);
    }
    printf("\n");
    
    printf("chirp_buffer[0][2048..2055]: ");
    for (int k = 2048; k < 2056; k++) {
        printf("%u ", chirp_buffer[0][k]);
    }
    printf("\n");
}
```

观察输出规律：
- 如果前 8 个值呈现明显交替模式 → 可能是交错
- 如果 0~2047 和 2048~4095 两段有不同的统计特性 → 可能是连续

**方法 3：分析频谱特征**  
```c
// 计算两段的能量
uint64_t energy_first_half = 0, energy_second_half = 0;
for (int k = 0; k < 2048; k++) {
    energy_first_half  += chirp_buffer[0][k];
    energy_second_half += chirp_buffer[0][k + 2048];
}

// 如果两段能量相近 → 可能是连续存储（两路信号能量相似）
// 如果前半能量 >> 后半 → 可能是交错（DC 附近能量高，高频能量低）
```

---

## 三、数据处理方法

### 3.1 当前输出是 mag2，无法直接得到 I/Q

**关键事实**：`enable_magsq = true` 意味着 BBE 只输出幅度平方，**相位信息已丢失**。

```
复数频谱:  Real0[k] = I[k] + j*Q[k]
mag2:      |Real0[k]|² = I[k]² + Q[k]²

从 mag2 无法反推 I 和 Q！（信息丢失）
```

如果需要复数频谱（I/Q 分量），必须修改 BBE 配置：
```c
// system_config.c
.enable_magsq           = false,   // 禁用 mag2，输出复数数据
.enable_magsq_autoscale = false,
```

**但这会改变数据格式**：
- 输出变为复数（每个样本 2 个 int16：实部 + 虚部）
- 数据量翻倍：4096 样本 × 2 (I+Q) × 2 字节 = 16 KB / chirp
- DMA 配置需要相应调整（transfer_block_qty 翻倍）

### 3.2 如果只需要区分两路信号的 mag2

假设数据是**连续存储**（常见配置）：

```c
// 提取两路信号的 mag2
uint16_t *real0_mag2 = &chirp_buffer[j][0];      // 前 2048 个：real0
uint16_t *real1_mag2 = &chirp_buffer[j][2048];   // 后 2048 个：real1

// 分别处理
for (int k = 0; k < 2048; k++) {
    uint16_t mag2_tx0 = real0_mag2[k];   // TX0 的频率 k 的幅度平方
    uint16_t mag2_tx1 = real1_mag2[k];   // TX1 的频率 k 的幅度平方
    
    // 使用 bexp 还原真实值
    float real_mag2_tx0 = mag2_tx0 * pow(2.0f, chirp_metadata[j].mag_bexp);
    float real_mag2_tx1 = mag2_tx1 * pow(2.0f, chirp_metadata[j].mag_bexp);
    
    // 进一步处理...
}
```

假设数据是**交错存储**：

```c
// 提取两路信号的 mag2
for (int k = 0; k < 2048; k++) {
    uint16_t mag2_tx0 = chirp_buffer[j][2*k];     // 偶数索引：real0
    uint16_t mag2_tx1 = chirp_buffer[j][2*k + 1]; // 奇数索引：real1
    
    // 使用 bexp 还原
    float real_mag2_tx0 = mag2_tx0 * pow(2.0f, chirp_metadata[j].mag_bexp);
    float real_mag2_tx1 = mag2_tx1 * pow(2.0f, chirp_metadata[j].mag_bexp);
    
    // 处理...
}
```

### 3.3 验证数据排列的简单测试

```c
// 在 main.c 的 while(1) 循环中
if (chirp_metadata[0].index % 100 == 0) {  // 每 100 帧打印一次
    // 测试：计算 DC 分量（k=0）
    uint16_t dc0 = chirp_buffer[0][0];
    uint16_t dc1_candidate1 = chirp_buffer[0][1];      // 交错假设
    uint16_t dc1_candidate2 = chirp_buffer[0][2048];   // 连续假设
    
    printf("DC components: [0]=%u, [1]=%u, [2048]=%u\n", 
           dc0, dc1_candidate1, dc1_candidate2);
    
    // DC 分量通常最大，观察哪个候选值更合理
}
```

---

## 四、如果需要完整的复数频谱（I/Q）

### 4.1 修改 BBE 配置

```c
// system_config.c 或初始化代码中
const ra_init_params_t ra_config = {
    // ... 其他配置 ...
    .enable_magsq           = false,  // 禁用 mag2
    .enable_magsq_autoscale = false,
    .enable_full_res_magsq  = false
};
```

### 4.2 数据格式变化

**Complex Mode 输出**（禁用 mag2）：
```
每个样本: 2 个 int16 (实部 I, 虚部 Q)
数据量: 4096 样本 × 2 分量 × 2 字节 = 16 KB / chirp
```

**Dual Real Mode 输出**（禁用 mag2）：
```
Real0 频谱: 2048 个复数 = 2048 × 2 × 2 = 8 KB
Real1 频谱: 2048 个复数 = 2048 × 2 × 2 = 8 KB
总计: 16 KB / chirp
```

### 4.3 DMA 配置调整

```c
// fft_size = 4096, 输出复数（不是 mag2）
output_real_samples = fft_size * 2;  // 每个样本 2 个分量（I/Q）

transfer_block_qty = (output_real_samples * 2U) / transfer_size_in_bytes;
                   = (8192 * 2) / 1024
                   = 16 块

descriptors_per_chirp = 16 + 1 = 17
descriptors_per_chirpframe = 4 × 17 = 68
```

**描述符数量检查**：68 < 250 ✓（在 IDMA_MAX_DESCRIPTORS 限制内）

### 4.4 数据解析

假设连续存储复数数据：

```c
typedef struct {
    int16_t real;  // I 分量
    int16_t imag;  // Q 分量
} complex_int16_t;

// 强制类型转换
complex_int16_t *complex_buffer = (complex_int16_t *)chirp_buffer[j];

// Dual Real Mode 下的布局（假设）
complex_int16_t *real0_spectrum = &complex_buffer[0];      // 0~2047
complex_int16_t *real1_spectrum = &complex_buffer[2048];   // 2048~4095

// 提取第 k 个频点
for (int k = 0; k < 2048; k++) {
    int16_t real0_I = real0_spectrum[k].real;
    int16_t real0_Q = real0_spectrum[k].imag;
    int16_t real1_I = real1_spectrum[k].real;
    int16_t real1_Q = real1_spectrum[k].imag;
    
    // 计算幅度（如果需要）
    float mag0 = sqrtf(real0_I*real0_I + real0_Q*real0_Q);
    float mag1 = sqrtf(real1_I*real1_I + real1_Q*real1_Q);
    
    // 计算相位
    float phase0 = atan2f(real0_Q, real0_I);
    float phase1 = atan2f(real1_Q, real1_I);
}
```

---

## 五、推荐的验证流程

### Step 1: 确认当前数据格式

```c
// 在 DSP 代码中添加
void analyze_dual_real_data(void) {
    printf("=== Dual Real Data Analysis ===\n");
    printf("First 8 samples: ");
    for (int i = 0; i < 8; i++) {
        printf("%u ", chirp_buffer[0][i]);
    }
    printf("\nSamples around 2048: ");
    for (int i = 2046; i < 2054; i++) {
        printf("%u ", chirp_buffer[0][i]);
    }
    printf("\n");
    
    // 统计特征
    uint32_t sum_first = 0, sum_second = 0;
    for (int i = 0; i < 2048; i++) {
        sum_first  += chirp_buffer[0][i];
        sum_second += chirp_buffer[0][i + 2048];
    }
    printf("Avg first half: %u, Avg second half: %u\n", 
           sum_first/2048, sum_second/2048);
}
```

### Step 2: 与已知信号对比

如果有信号源产生已知频率的测试信号：
```
例如: TX0 产生 10 MHz 正弦波，TX1 产生 20 MHz 正弦波
→ 在频谱中应该看到两个峰值
→ 根据峰值位置判断数据排列
```

### Step 3: 文档查询

查阅 indie Semiconductor 的 BBE 技术手册：
- Range Azimuth Accelerator 章节
- Dual Real Mode 输出格式
- DMA 接口规范

---

## 六、总结

| 问题 | 答案 |
|------|------|
| **当前输出是什么？** | mag2（幅度平方），uint16_t 类型 |
| **能直接得到 I/Q 吗？** | ❌ 不能，mag2 丢失了相位信息 |
| **数据排列方式？** | 可能是连续或交错，需实验确定（推测连续） |
| **两路信号如何区分？** | 连续：前 2048/后 2048；交错：偶数/奇数索引 |
| **如何获得 I/Q？** | 设置 `enable_magsq=false`，DMA 配置需相应调整 |
| **修改后数据量？** | 从 8 KB/chirp 变为 16 KB/chirp（翻倍） |

**推荐方案**（基于当前 mag2 输出）：
1. 实验确定数据排列方式（连续 vs 交错）
2. 根据排列方式分别提取两路 mag2 数据
3. 使用 bexp 还原真实幅度平方值
4. 如果需要相位信息，必须禁用 mag2 输出

**如果需要 I/Q 复数频谱**：
1. 修改 `enable_magsq = false`
2. 调整 DMA 配置（描述符数量翻倍）
3. 数据解析时按复数结构处理
4. 注意内存占用和处理时间增加
