# HSADCH_FullInit() 是应用层对 4 路 HSADC 做“一次性完整初始化”的封装，负责：

  1. 初始化选中的 HSADC 实例及其内部存储器；
  2. 配置并启动前台/后台校准；
  3. 等待校准结束，并处理超时；
  4. 配置采样速率和采集参数；
  5. 配置模拟偏置和数字偏置；
  6. 根据参数决定是否启用 FFT。

  代码位置：app/src/bcm/hsadc_helper.c:138。

  ———

  ## 一、函数接口和参数

  int32_t HSADCH_FullInit(uint32_t aAdcSelect,
                          uint32_t aSampleFreq,
                          uint32_t aAdcSampleSize,
                          uint32_t aAdcAnalogOffset,
                          uint32_t aAdcDigitalOffset,
                          uint32_t aFftEnable)

  各参数含义如下：

   参数                 含义
  ━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   aAdcSelect           ADC 通道选择掩码，bit0 到 bit3 分别对应 ADC0 到 ADC3
  ───────────────────  ──────────────────────────────────────────────────────
   aSampleFreq          采样速率选择，0 表示 1.25 GHz，1 表示 5 GHz
  ───────────────────  ──────────────────────────────────────────────────────
   aAdcSampleSize       每次采集的采样点数
  ───────────────────  ──────────────────────────────────────────────────────
   aAdcAnalogOffset     模拟直流偏置，用于在模拟域调整 ADC 输入工作点
  ───────────────────  ──────────────────────────────────────────────────────
   aAdcDigitalOffset    数字偏置，用于在数字域对采样数据增加偏移
  ───────────────────  ──────────────────────────────────────────────────────
   aFftEnable           非零时启用 FFT，等于 FALSE 时不配置 FFT

  通道掩码的对应关系：

  aAdcSelect = 0x1  -> ADC0
  aAdcSelect = 0x2  -> ADC1
  aAdcSelect = 0x4  -> ADC2
  aAdcSelect = 0x8  -> ADC3
  aAdcSelect = 0xF  -> ADC0、ADC1、ADC2、ADC3

  当前调用方 app/src/tasks/acq_task.c:160 使用：

  adcSelect = 0xFUL;

  也就是初始化全部 4 路 HSADC。

  ———

  # 二、初始化前提

  HSADCH_FullInit() 本身不负责配置 HSADC 外部模拟电源、PLL、复位、时钟等底层资源。

  在调用它之前，需要先完成 HSADC/HSAFE 的基础硬件准备，包括：

  - HSAFE 初始化；
  - Rescal 校准；
  - HSADC PLL 配置并等待锁定；
  - HSADC 复位释放；
  - HSREF 复位释放；
  - OCP/FFT 时钟配置；
  - ADC 时钟使能。

  这些操作位于 app/src/bcm/BCM8915X_BareMetal_helper.c:107 附近。

  因此，HSADCH_FullInit() 更准确地说是：

  > 在 HSADC 基础时钟、PLL 和复位已经准备好的前提下，完成每个采集通道的软件驱动初始化、校准和采集参数配置。

  ———

  # 三、函数整体执行流程

  函数整体逻辑可以概括为：

  遍历选中的 ADC
          |
          v
  HSADC_DrvInit()
          |
          v
  配置校准参数
          |
          v
  触发校准
          |
          v
  等待 5 ms
          |
          v
  轮询每个 ADC 的校准状态
          |
          v
  配置采样速率和采集控制器
          |
          v
  配置模拟偏置
          |
          v
  打开模拟偏置相关开关
          |
          v
  配置数字偏置
          |
          v
  如果需要，配置 FFT

  ———

  # 四、第一阶段：初始化 HSADC 实例

  对应代码：app/src/bcm/hsadc_helper.c:154。

  for (adcId = 0; adcId < HSADC_NUM_CHANNELS; adcId += 1) {
      if ((aAdcSelect & (1UL << adcId)) != 0UL) {
          CHK_RETVAL(retVal = HSADC_DrvInit(adcId));
      }
  }

  HSADC_NUM_CHANNELS 定义为：

  #define HSADC_NUM_CHANNELS (4UL)

  因此 adcId 会依次遍历 0、1、2、3。

  只有当：

  (aAdcSelect & (1UL << adcId)) != 0UL

  时，才会初始化对应通道。

  例如：

  aAdcSelect = 0x5;  // 二进制 0101

  则会初始化：

  ADC0、ADC2

  不会操作 ADC1 和 ADC3。

  ## HSADC_DrvInit() 做了什么

  底层实现位于 drivers/bcm8915x/drivers/hsadc/hsadc_drv.c:262。

  它主要完成：

  1. 使能 ADC buffer memory 初始化；
  2. 使能 FFT buffer memory 初始化；
  3. 使能 FFT memory 初始化；
  4. 打开 memory 内建检查；
  5. 释放这些 memory 的 reset；
  6. 轮询 memory-init-done 状态；
  7. 设置驱动状态为 initialized。

  也就是说，这一步主要不是配置采样速率，而是确保 HSADC 内部的采样缓存和 FFT 缓存已经可用。

  如果 memory 初始化失败，底层驱动会返回错误，HSADCH_FullInit() 通过 CHK_RETVAL() 跳转到 err，直接返回错误码。

  CHK_RETVAL 定义如下：

  #define CHK_RETVAL(x)                   \
  {                                       \
      if (((x) != BCM_ERR_OK)) {          \
          goto err;                       \
      }                                   \
  }

  因此，函数遇到任何底层驱动错误都会提前退出。

  ———

  # 五、第二阶段：计算校准时间

  对应代码：app/src/bcm/hsadc_helper.c:161。

  #define HSADC_CALIB_DURATION_NS (2500000UL)

  该宏表示校准时间：

  2,500,000 ns = 2.5 ms

  代码将时间转换成 HSADC 使用的 204.8 ns tick：

  calibCfg.calBgRdbDuration =
      (HSADC_CALIB_DURATION_NS * 10UL) / 2048UL;

  计算过程为：

  2,500,000 ns × 10 / 2048
  ≈ 12,207

  底层定义中，一个 tick 对应：

  204.8 ns

  所以：

  12,207 × 204.8 ns ≈ 2,499,993.6 ns

  约等于 2.5 ms。

  最终写入：

  HSADC_ADCCAL_CONTROL1

  底层实现见 drivers/bcm8915x/drivers/hsadc/hsadc_drv.c:493。

  ———

  # 六、第三阶段：配置并触发 ADC 校准

  对应代码：app/src/bcm/hsadc_helper.c:146。

  ## 1. 设置校准状态选择

  uint8_t fgen = 1U;
  uint8_t bgen = 1U;
  uint16_t calStatesEn = (3U * fgen) + (4U * bgen);

  计算结果：

  calStatesEn = 3 × 1 + 4 × 1 = 7

  也就是：

  7 = 0b00111

  根据 HSADC_DrvTriggerCalib() 的接口定义：

  bit0：FG offset
  bit1：FG daccal
  bit2：BG offset and gain
  bit3：BG offset
  bit4：BG gain

  因此当前值 7 实际置位：

  bit0、bit1、bit2

  也就是启用：

  - 前台 Offset 校准；
  - 前台 DAC 校准；
  - 后台 Offset/Gain 校准。

  底层触发实现位于 drivers/bcm8915x/drivers/hsadc/hsadc_drv.c:531。

  ## 2. 调用 HSADC_DrvInitCalibration()

  HSADC_DrvInitCalibration(adcId, &calibCfg);

  该接口会配置校准相关寄存器，包括：

  - adccal_control5
      - 设置 fg_daccal_nmax = 12；

  - adccal_control4
      - 设置 fg_offset_mode = 1；

  - adccal_control2
      - bg_gain_mu = 3；
      - bg_gain_nmax = 2；
      - bg_cal_cfg = 0；
      - bg_offset_mu = 4；

  - adccal_control3
      - 配置 signal-front-end bias mode；

  - adccal_control1
      - 写入后台校准持续时间 calBgRdbDuration。

  对应底层实现见 drivers/bcm8915x/drivers/hsadc/hsadc_drv.c:447。

  ## 3. 触发校准

  HSADC_DrvTriggerCalib(adcId, calStatesEn);

  底层操作分为两步：

  control0 &= ~HSADC_ADCCAL_CONTROL0_CAL_STATES_EN_MASK;
  HSADC_Regs[hwId]->adccal_control0 = control0 | calStatesEn;

  先写入校准类型，然后：

  HSADC_Regs[hwId]->adccal_control0 |=
      HSADC_ADCCAL_CONTROL0_CAL_START_MASK;

  设置 CAL_START，启动 ADC 校准。

  需要注意的是，函数先完成所有选中通道的 HSADC_DrvInit()，然后才逐个配置并触发校准。因此选中的多个 ADC 会在相近的时间开始校准。

  ———

  # 七、第四阶段：等待和检查校准结果

  对应代码：app/src/bcm/hsadc_helper.c:172。

  首先固定延时：

  BCM_DelayUs(5000UL);

  即：

  等待 5 ms

  这样做是为了给校准硬件一个初始执行时间，避免刚触发后立即频繁轮询。

  随后逐个 ADC 检查校准状态：

  calibStatus.calStatus = 0U;

  for (i = 0UL; i < HSADC_CALIBRATION_TIMEOUT_US; i += 100UL) {
      CHK_RETVAL(retVal =
          HSADC_DrvFftGetCalibStatus(adcId, &calibStatus));

      if (calibStatus.calStatus != 1U) {
          break;
      }

      BCM_DelayUs(100UL);
  }

  相关宏：

  #define HSADC_CALIBRATION_TIMEOUT_US (50000UL)

  轮询参数：

  最大时间：50 ms
  轮询间隔：100 us
  最大轮询次数：500 次

  底层 HSADC_DrvFftGetCalibStatus() 实际读取：

  adccal_control0

  中的 CAL_BUSY 字段：

  calStatus = 1：校准进行中
  calStatus = 0：校准完成

  如果循环结束时仍然是：

  calibStatus.calStatus == 1U

  则认为超时：

  log_w("HSADC Calibration Timedout for channel %d", adcId);
  retVal = BCM_ERR_TIME_OUT;
  goto err;

  因此，从时间上看：

  固定等待 5 ms
  +
  最多轮询等待 50 ms

  最坏情况下，函数大约会等待 55 ms，然后返回 BCM_ERR_TIME_OUT。

  这里有一个实现细节：校准状态是按 ADC 逐个检查的。如果 ADC0 很快完成，随后才检查 ADC1；但是校准本身已经在前面的触发阶段启动了，并不是检查 ADC0 完成后才触发 ADC1。

  ———

  # 八、第五阶段：选择采样速率

  对应代码：app/src/bcm/hsadc_helper.c:194。

  uint32_t adcSamplingMode =
      (aSampleFreq == LIDAR_ADC_SAMPLE_FREQ_1_25G) ?
      HSADC_SAMPLING_MODE_1P25G :
      HSADC_SAMPLING_MODE_5G;

  定义为：

  #define LIDAR_ADC_SAMPLE_FREQ_1_25G (0UL)
  #define LIDAR_ADC_SAMPLE_FREQ_5G    (1UL)

  驱动层定义为：

  #define HSADC_SAMPLING_MODE_1P25G (0UL)
  #define HSADC_SAMPLING_MODE_5G    (1UL)

  所以当前逻辑是：

  aSampleFreq == 0  -> 1.25 GHz
  其他值            -> 5 GHz

  这一点需要注意：函数没有显式检查 aSampleFreq 是否只能为 0 或 1。如果传入了非法值，例如 2，当前实现会默认为 5 GHz，而不是返回参数错误。

  底层 HSADC_DrvConfigSamplingMode() 最终修改：

  HSADC_ACQ_CONTROL0.SAMPLING_MODE

  在驱动实现中，5G 模式会写入对应的硬件编码值 2，1.25G 模式写入 0。

  ———

  # 九、第六阶段：配置采集控制器

  对应代码：app/src/bcm/hsadc_helper.c:198。

  HSADC_DrvConfigAcqController(
      adcId,
      0xFU,
      0U,
      0U,
      ((aAdcSampleSize / 16UL) - 1UL));

  传入的五个参数分别是：

  channelEnable = 0xF
  decimationRate = 0
  aggregateCount = 0
  captureSize = aAdcSampleSize / 16 - 1

  ## 1. channelEnable = 0xF

  0xF = 0b1111

  表示开启 4 个内部采集通道。

  驱动接口文档说明：

  - 1.25G 模式下，channel enable 的每一位对应一个通道；
  - 5G 模式下，要求 4 个 bit 全部置位。

  因此对于当前 5G 使用场景，0xF 是正确的配置。

  ## 2. decimationRate = 0

  根据驱动接口：

  0：不抽取
  1：抽取 2 倍
  2：抽取 4 倍

  这里配置为 0，表示不做抽取，保留全部采样数据。

  ## 3. aggregateCount = 0

  代码传入：

  aggregateCount = 0U;

  底层会将它写入：

  HSADC_ACQ_CONTROL0.AGG_CNT

  但是接口注释描述的有效范围是 1 到 32。因此这里存在一个需要注意的实现差异：

  - 如果硬件字段采用“实际数量减一”的编码，那么 0 可能代表聚合 1 次；
  - 如果硬件字段直接表示数量，那么 0 就不符合接口文档。

  从当前代码意图来看，它使用的是单次采集模式，因此 0 很可能是硬件定义的“单次/一次聚合”的编码。但最终含义应以 HSADC 硬件手册对 AGG_CNT 的编码说明为准。

  ## 4. captureSize

  采集大小计算为：

  (aAdcSampleSize / 16UL) - 1UL

  底层接口说明 captureSize 的单位是 16 个 ADC sample，且采用减一编码。

  例如：

   aAdcSampleSize    写入的 captureSize
  ━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━
              512                    31
  ────────────────  ────────────────────
             1024                    63
  ────────────────  ────────────────────
             2048                   127
  ────────────────  ────────────────────
             4096                   255
  ────────────────  ────────────────────
             8192                   511

  对于当前默认配置：

  #define ACQ_CAPTURE_SIZE (8UL * 1024UL)

  所以：

  aAdcSampleSize = 8192
  captureSize = 8192 / 16 - 1
              = 512 - 1
              = 511

  驱动将其写入：

  HSADC_ACQ_CONTROL2.CAP_SIZE

  底层实现见 drivers/bcm8915x/drivers/hsadc/hsadc_drv.c:646。

  ———

  # 十、第七阶段：配置捕获模式为单次采集

  对应代码：

  HSADC_DrvConfigCaptureMode(
      adcId,
      HSADC_CAPTURE_MODE_SINGLE);

  定义为：

  #define HSADC_CAPTURE_MODE_SINGLE (1UL)

  驱动将这个值写入：

  HSADC_ACQ_CONTROL0.CAP_MODE

  捕获模式定义为：

  0：不采集
  1：单次采集
  2：聚合采集
  3：连续采集

  这里配置成单次采集，表示每次由外部触发信号启动一次采样，采满设定的采样点数后停止。

  触发信号来自后续配置的采集触发链路；HSADCH_FullInit() 只配置 HSADC 的响应方式，并不在此处启动实际采集。

  ———

  # 十一、第八阶段：配置读完成地址

  对应代码：

  HSADC_DrvConfigReadDoneAddress(
      adcId,
      ((aAdcSampleSize / 16UL) - 1UL));

  例如 8192 点采样时：

  rdDoneAddr = 511

  驱动将它写入：

  HSADC_ACQ_CONTROL3.RD_DONE_ADDR

  这个地址用于指示 OCP/DMA/软件读取采样 buffer 时，读取到哪个地址位置可以认为采样数据已经被读完。

  需要区分两个概念：

  CAP_SIZE       -> 本次采集写入多少采样数据
  RD_DONE_ADDR   -> 读数据过程到哪个地址时认为读取完成

  本函数让二者使用相同的地址编码：

  (aAdcSampleSize / 16) - 1

  对于 8192 点采样，二者都是 511。

  ———

  # 十二、第九阶段：配置读取时间窗口

  对应代码：

  HSADC_DrvConfigReadCapSize(adcId, 0x7FBUL);

  其中：

  0x7FB = 2043

  驱动将它写入：

  HSADC_ACQ_CONTROL3.RD_CAP_SIZE

  该字段决定给 FFT 逻辑或 OCP 读取 ADC buffer 分配多长时间。

  这里的 0x7FB 并不是 ADC 的采样点数，也不是采集长度。它是 HSADC 读取阶段的时间/窗口配置值，单位是硬件规定的 16-sample 相关单位。

  驱动接口注释说明，这个值通常应当：

  > 比两次采集触发之间的间隔少几个时钟周期，约少 5 个时钟周期。

  当前代码将它固定为 0x7FB，没有根据采样大小或触发周期动态计算。因此如果未来修改：

  - 采样频率；
  - 采集触发间隔；
  - FFT 时钟；
  - 单次采集长度；

  就需要重新确认 RD_CAP_SIZE = 0x7FB 是否仍然满足时序要求。

  ———

  # 十三、第十阶段：配置模拟偏置

  对应代码：app/src/bcm/hsadc_helper.c:210。

  HSADCRegs[adcId]->adccal_control3 &=
      ~HSADC_ADCCAL_CONTROL3_NON_BG_ADC_DC_OFFSET_MASK;

  HSADCRegs[adcId]->adccal_control3 |=
      (aAdcAnalogOffset <<
       HSADC_ADCCAL_CONTROL3_NON_BG_ADC_DC_OFFSET_SHIFT) &
      HSADC_ADCCAL_CONTROL3_NON_BG_ADC_DC_OFFSET_MASK;

  寄存器字段为：

  HSADC_ADCCAL_CONTROL3_NON_BG_ADC_DC_OFFSET_MASK
      = 0xF0000

  HSADC_ADCCAL_CONTROL3_NON_BG_ADC_DC_OFFSET_SHIFT
      = 16

  也就是：

  adccal_control3[19:16]

  该字段宽度为 4 bit，因此 aAdcAnalogOffset 实际上只能有效表达 4 bit 值，超出部分会被 mask 截断。

  当前调用方的配置为：

  #define ACQ_ADC_ANA_OFFSET_DEF 8UL

  在 FFT/FMCW 模式下：

  adcAnalogOffset = 8;

  在 RAW/ToF 模式下：

  adcAnalogOffset = 0;

  调用方逻辑见 app/src/tasks/acq_task.c:168。

  设计意图是：

  - FFT/FMCW 信号通常是围绕直流中心摆动的正弦/复信号，需要预留完整的模拟动态范围；
  - ToF 脉冲通常位于正向区域，因此不使用该模拟负偏置。

  代码注释称这是：

  Configure ADC analog offset to negative maximum to avoid clipping for LiDAR pulses

  从硬件字段名称看，它配置的是：

  NON_BG_ADC_DC_OFFSET

  即非后台校准路径上的 ADC DC offset。

  ———

  # 十四、第十一阶段：打开模拟偏置相关开关

  对应代码：app/src/bcm/hsadc_helper.c:219。

  if ((aAdcSelect & (1UL << 0)) != 0UL) {
      HSAFE_REG->adc0_config1 |= 2UL;
  }

  ADC1、ADC2、ADC3 也做同样操作。

  这里 2UL 对应：

  bit1

  代码注释写的是：

  /* Enable ADC0 */

  但这一段周围的注释是：

  Enable the Analog offset for selected channels

  需要注意，当前 RDB 定义中：

  HSAFE_ADC_CONFIG1_RESERVED_1_MASK
      = 0x2UL

  也就是说 adc*_config1 的 bit1 在寄存器头文件中被标记为 RESERVED_1。该寄存器中明确命名的字段是：

  bit0：EN_CLKGEN
  bit2~5：LPF_5G
  bit6~9：LPF_1P25G
  bit1：Reserved

  所以从当前源码和 RDB 定义来看：

  - 代码意图是打开 ADC/模拟 offset 相关功能；
  - 但写入的是 adc*_config1 的 bit1；
  - RDB 将 bit1 标记为保留位；
  - bit0 才是明确命名的 EN_CLKGEN。

  这部分的真实硬件含义需要结合 HSADC/HSAFE 的芯片 TRM 或硬件团队定义进一步确认。至少可以确定，HSADCH_FullInit() 没有通过这里打开 ADC 时钟；ADC 时钟已经在前置的 HSAFE_DrvAdcClkCfg() 中配置。

  ———

  # 十五、第十二阶段：配置数字偏置

  对应代码：app/src/bcm/hsadc_helper.c:233。

  HSADC_DrvTofOffsetConfig(adcId, aAdcDigitalOffset);

  底层将参数直接写入：

  HSADC_Regs[hwId]->tof_offset

  接口注释说明这个偏置是一个有符号值，会加到输入 ADC 信号上，使信号向正方向移动。

  当前调用方配置：

  #define ACQ_ADC_DIG_OFFSET_DEF 200UL

  在 RAW/ToF 模式下：

  adcDigitalOffset = 200;

  在 FFT/FMCW 模式下：

  adcDigitalOffset = 0;

  也就是说：

   模式    模拟偏置    数字偏置
  ━━━━━━  ━━━━━━━━━━  ━━━━━━━━━━
   FFT            8           0
  ──────  ──────────  ──────────
   FMCW           8           0
  ──────  ──────────  ──────────
   RAW            0         200
  ──────  ──────────  ──────────
   ToF            0         200

  这种配置的目的通常是：

  - FFT/FMCW：保留完整的双极性信号，不能额外把波形整体向正方向推移；
  - RAW/ToF：ToF 脉冲通常需要保证输出数据落在有效的正值范围，因此增加数字偏置 200。

  另外，驱动接口将 tofOffset 定义为 uint16_t，而 HSADCH_FullInit() 的参数是 uint32_t。如果传入超过 16 bit 的值，最终会发生截断。虽然当前使用的 0 和 200 没有问题，但如果以后要传入负数，需要特别确认调用方式和二进制补码表达。

  ———

  # 十六、第十三阶段：可选配置 FFT

  对应代码：app/src/bcm/hsadc_helper.c:240。

  if (aFftEnable != FALSE) {
      CHK_RETVAL(retVal =
          HSADCH_ConfigFft(aAdcSelect, aAdcSampleSize));
  }

  只要 aFftEnable 非零，就会配置 FFT。

  HSADCH_ConfigFft() 主要做三件事：

  1. 根据采样点数转换 FFT size 字段；
  2. 配置 FFT 读取周期和起始地址；
  3. 设置 FFT 预分频器。

  ## 1. 采样点数到 FFT size 的映射

  app/src/bcm/hsadc_helper.c:64 中的映射关系：

   采样点数    FFT size 字段
  ━━━━━━━━━━  ━━━━━━━━━━━━━━━
        512                3
  ──────────  ───────────────
       1024                4
  ──────────  ───────────────
       2048                5
  ──────────  ───────────────
       4096                6
  ──────────  ───────────────
       8192                7

  HSADC 头文件也说明：

  FFT size = 7 -> 8K
  FFT size = 6 -> 4K
  FFT size = 5 -> 2K
  FFT size = 4 -> 1K
  FFT size = 3 -> 512

  如果启用 FFT 时，aAdcSampleSize 不是这 5 个值之一，则：

  retVal = BCM_ERR_INVAL_PARAMS;
  goto err;

  因此：

  aFftEnable == FALSE

  时，函数只要求采样点数满足采集控制器的约束；

  aFftEnable != FALSE

  时，采样点数必须是：

  512、1024、2048、4096 或 8192

  ## 2. FFT 配置值

  fftCfg.fftEnable = 1U;
  fftCfg.fftSize = fftSize;
  fftCfg.captureStartAddress = 0U;

  含义是：

  - 打开 FFT；
  - 采集从 ADC buffer 起始位置开始；
  - FFT size 根据采样点数决定。

  HSADC_DrvFftConfig() 会修改：

  ACQ_CONTROL4.FFT_RD_PERIOD
  ACQ_CONTROL2.CAP_START_ADDR
  ACQ_CONTROL0.FFT_EN
  ACQ_CONTROL0.FFT_SIZE

  ## 3. FFT 读取周期

  代码计算：

  fftCfg.fftReadPeriod =
      ((aAdcSampleSize / 16UL) * 3UL) - 5UL;

  也就是：

  FFT_RD_PERIOD = (sampleSize / 16) × 3 - 5

  不同采样点数对应：

   采样点数    FFT read period
  ━━━━━━━━━━  ━━━━━━━━━━━━━━━━━
        512                 91
  ──────────  ─────────────────
       1024                187
  ──────────  ─────────────────
       2048                379
  ──────────  ─────────────────
       4096                763
  ──────────  ─────────────────
       8192               1531

  对于当前默认 8192 点配置：

  fftSize = 7
  fftReadPeriod = 1531

  ## 4. FFT 预分频器

  acqControl4 &= ~HSADC_ACQ_CONTROL4_FFT_PREDIV_MASK;

  acqControl4 |=
      (fftSize << HSADC_ACQ_CONTROL4_FFT_PREDIV_SHIFT) &
      HSADC_ACQ_CONTROL4_FFT_PREDIV_MASK;

  写入：

  HSADC_ACQ_CONTROL4.FFT_PREDIV

  代码注释指出推荐值为：

  divide = 2^fftSize

  例如：

  fftSize = 7
  预分频 = 2^7 = 128

  注意这里写入的是 fftSize 字段编码 7，而不是直接写入整数 128。硬件根据字段值解释实际的二次幂分频。

  ———

  # 十七、默认调用实例的完整结果

  当前默认配置来自 app/src/tasks/acq_task.c:223：

  lidarMode     = LIDAR_MODE_RAW;
  adcSampleFreq = LIDAR_ADC_SAMPLE_FREQ_5G;
  adcSampleSize = 8192;

  进入 ACQ_Start() 后：

  adcSelect = 0xF;
  fftEnable = FALSE;
  adcAnalogOffset = 0;
  adcDigitalOffset = 200;

  因此 HSADCH_FullInit() 的实际配置效果是：

  ADC0~ADC3 全部初始化
  ADC0~ADC3 全部校准
  采样速率：5 GHz
  采集点数：8192
  通道使能：0xF
  不抽取
  单次捕获
  CAP_SIZE：511
  RD_DONE_ADDR：511
  RD_CAP_SIZE：0x7FB
  模拟偏置：0
  数字偏置：200
  FFT：关闭

  如果切换到 FFT 或 FMCW 模式，则变为：

  ADC0~ADC3 全部初始化
  ADC0~ADC3 全部校准
  采样速率：通常为 5 GHz
  采集点数：8192
  模拟偏置：8
  数字偏置：0
  FFT：开启
  FFT size：7
  FFT read period：1531
  FFT capture start address：0
  FFT pre-divider 字段：7

  ———

  # 十八、错误处理特点

  整个函数使用：

  CHK_RETVAL(...)

  处理底层驱动返回值。

  一旦某个步骤失败：

  立即跳转到 err
  返回当前 retVal

  函数不会：

  - 回滚已经写入的寄存器；
  - 反初始化前面已经初始化成功的 ADC；
  - 清除已经触发的其他 ADC 校准；
  - 恢复到调用前状态。

  所以如果中途失败，系统中的 HSADC 可能处于“部分初始化”状态。

  另外，当前函数没有主动检查以下参数：

  aAdcSelect 是否只使用 bit0~bit3
  aSampleFreq 是否是合法值
  aAdcSampleSize 是否大于等于 16
  aAdcSampleSize 是否为 16 的倍数
  aAdcAnalogOffset 是否不超过 4 bit

  其中 aAdcSampleSize 如果小于 16：

  (aAdcSampleSize / 16UL) - 1UL

  由于是无符号计算，可能发生下溢，得到很大的数值。启用 FFT 时，FFT 配置函数会检查采样点数是否为支持的 5 种大小；但不启用 FFT 时，采样点数检查依赖底层硬件/驱动或调用方保证。

  ———

  ## 总结

  HSADCH_FullInit() 的核心初始化顺序是：

  HSADC 内存初始化
      ->
  校准参数配置
      ->
  启动 FG/BG 校准
      ->
  等待校准完成
      ->
  选择 1.25G/5G 采样模式
      ->
  设置采集通道、采集长度和捕获模式
      ->
  设置读完成地址和读取窗口
      ->
  配置模拟 DC offset
      ->
  打开 ADC 相关模拟开关
      ->
  配置数字 ToF offset
      ->
  按需开启 FFT

  它本身是应用层的组合函数，真正的寄存器操作主要由 HSADC_Drv*() 底层驱动完成；但模拟 offset 以及 HSAFE adc*_config1 的 bit1 是直接寄存器操作，需要重点核对芯片 TRM，因为当前 RDB 文件将该 bit 标为 reserved。

