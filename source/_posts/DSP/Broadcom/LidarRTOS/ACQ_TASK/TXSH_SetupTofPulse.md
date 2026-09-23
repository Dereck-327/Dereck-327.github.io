# TXSH_SetupTofPulse

TXSH_SetupTofPulse() 用来把 ToF 发射脉冲“编译”为 TX Serializer（TXSLZR）的 pattern memory 内容，并使能 sequencer 和 pattern generator。它并不是直接写一个“pulse width = N”的寄存器；它实际生成的是一段串行比特波形：

  前 aWidth 个 serial bit = 1
  后续 serial bit          = 0

  当 TXSLZR 收到来自 ACQCMN 的 TX trigger 时，会回放这个 pattern，从而输出一个高电平 ToF 发射脉冲。

  代码位置：app/src/bcm/txs_helper.c:40。

  ———

  ## 1. 函数接口

  int32_t TXSH_SetupTofPulse(uint32_t aWidth, uint32_t aPageSize)

  头文件中的参数说明见 app/inc/bcm/txs_helper.h:16：

  @param[in] aWidth    Pulse width in 5GHz Ticks (0.2 ns)
  @param[in] aPageSize Page size for TX serializer

  即：

   参数         含义                                                     单位
  ━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━
   aWidth       要输出为高电平的 serial bit 数量，也就是 ToF 脉冲宽度    5 GHz tick，0.2 ns
  ───────────  ───────────────────────────────────────────────────────  ────────────────────
   aPageSize    TXSLZR pattern page 的长度                               32-bit word 数量

  在当前调用点 app/src/tasks/acq_task.c:192：

  TXSH_SetupTofPulse(
      LIDAR_SHARED_CTX->config.tofPulseWidth * 5UL,
      256UL);

  默认 ToF 脉冲宽度为：

  #define ACQ_TOF_PULSE_WIDTH (2UL)

  因此默认实参是：

  aWidth    = 2 ns × 5 = 10 tick
  aPageSize = 256 word

  因为 TX serializer 按 5 GHz 工作：

  1 tick = 1 / 5 GHz = 0.2 ns
  10 tick = 10 × 0.2 ns = 2.0 ns

  所以默认目标波形就是：

  高电平维持 10 个 serial bit = 2 ns
  其余 pattern bit 为低电平

  ———

  # 2. 它与 ACQ_SetupTriggerDelays() 的关系

  两者职责不同，但在一次采集的时序链路中相连。

  CCU / NSU 产生 acquisition trigger
      |
      v
  ACQCMN
      |
      +-- 延迟 1 个 PULSE_DELAY tick --> TXSLZR trigger
      |
      +-- 延迟 10 个 PULSE_DELAY tick -> HSADC trigger
      |
      +-- 延迟 10 个 PULSE_DELAY tick -> HSREF trigger

  其中：

  - ACQ_SetupTriggerDelays() 决定 TXSLZR、HSADC、HSREF 何时接收各自的触发；
  - TXSH_SetupTofPulse() 决定 TXSLZR 接收触发后输出什么样的串行脉冲；
  - HSADCH_FullInit() 决定 HSADC 收到 trigger 后如何采样。

  因此可以这样理解：

  ACQ_SetupTriggerDelays()
      = 调整“什么时候开始”

  TXSH_SetupTofPulse()
      = 决定“开始后输出什么波形”

  HSADCH_FullInit()
      = 决定“ADC 如何采样这个事件”

  ———

  # 3. 初始化 TXSLZR 驱动

  函数开始处：

  CHK_RETVAL(retVal = TXSLZR_DrvInit(TXSLZR_HW_ID_0));

  见 app/src/bcm/txs_helper.c:49。

  这里使用 TXSLZR 硬件实例 0：

  TXSLZR_HW_ID_0

  底层 TXSLZR_DrvInit() 的行为比较轻量，主要是驱动状态初始化，并不负责完整的硬件复位、PLL、时钟或 pattern 清除。

  实现位于 drivers/bcm8915x/drivers/txslzr/txslzr_drv.c:174。

  在启用了 BCM8915X_PARAM_VALIDATION 的构建中，如果该 TXSLZR 驱动已经处于 initialized 状态，重复调用：

  TXSLZR_DrvInit(TXSLZR_HW_ID_0)

  可能返回 BCM_ERR_INVAL_PARAMS。

  这意味着当前 TXSH_SetupTofPulse() 更适合“启动时调用一次”的模型，而不是运行中反复重新配置不同脉冲宽度的模型。当前 ACQ_Start() 只在采集任务启动时执行一次，符合这一使用方式。

  ———

  # 4. page 和 pattern memory 的基本概念

  TXSLZR 使用 pattern-generator memory（PG memory）保存待串行输出的比特模式。

  一个 PG memory entry 是 32 bit：

  1 个 word = 32 个串行输出 bit

  函数传入：

  aPageSize = 256

  即使用：

  256 个 32-bit word

  总 pattern 长度是：

  256 × 32 = 8192 bit

  在 5 GHz serial clock 下：

  8192 × 0.2 ns = 1638.4 ns = 1.6384 us

  因此，当前 pattern page 的完整持续时间是：

  约 1.6384 us

  而默认 acquisition trigger 间隔是：

  #define ACQ_TRIGGER_INTERVAL_US (5UL)

  即约 5 µs 一次。

  所以当前参数下：

  pattern 回放时长：约 1.6384 us
  触发周期：约 5 us

  每次 pattern 在下一次 trigger 到来之前应该已经结束，有足够余量。

  ———

  # 5. 配置一个 page

  函数中的 page 数量定义：

  #define TXSLZR_NUM_PAGES_LOG2 (0UL)
  #define TXSLZR_NUM_PAGES      (1UL << TXSLZR_NUM_PAGES_LOG2)

  因此：

  TXSLZR_NUM_PAGES_LOG2 = 0
  TXSLZR_NUM_PAGES      = 1

  即只使用一个 pattern page，编号为 page 0。

  初始化 page 信息：

  TXSLZR_PageInfoType pageInfo = {0};

  pageInfo.pageStartAddr = 0x0UL;
  pageInfo.pageSize = aPageSize;

  当前结果：

  pageStartAddr = 0
  pageSize      = 256 word

  随后调用：

  TXSLZR_DrvPageConfig(TXSLZR_HW_ID_0,
                       TXSLZR_NUM_PAGES_LOG2,
                       (pageInfo.pageSize / TXSLZR_NUM_PAGES) - 1UL);

  代入默认值：

  TXSLZR_DrvPageConfig(0, 0, 255)

  这里的三个值分别意味着：

   参数         当前值    含义
  ━━━━━━━━━━━  ━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   hwId              0    TXSLZR instance 0
  ───────────  ────────  ───────────────────────────────────────
   numOfPage         0    硬件编码：1 page
  ───────────  ────────  ───────────────────────────────────────
   pageSize        255    硬件编码：256 word，通常是 length - 1

  底层驱动将这个值写入：

  TXSLZR.PAGE_SZ_0[7:0] = 0xFF

  也就是 page 0 的长度为 256 个 32-bit word。

  TXSLZR_DrvPageConfig() 实现在 drivers/bcm8915x/drivers/txslzr/txslzr_drv.c:229。

  ———

  # 6. 启用 TX_WORDFLIP

  函数执行：

  TXSLZR_REG->ser_tx_ctrl_2 |=
      TXSLZR_SER_TX_CTRL_2_TX_WORDFLIP_MASK;

  即设置：

  TXSLZR_SER_TX_CTRL_2.TX_WORDFLIP = 1

  寄存器字段是：

  SER_TX_CTRL_2[14] = TX_WORDFLIP

  定义见 drivers/bcm8915x/include/rdb/a0/txslzr_rdb.h:265。

  这一步的核心意图是处理软件内存 word 的 bit 顺序与物理 serializer 输出 bit 顺序之间的差异。

  因为随后代码用：

  (1UL << bitIdx) - 1UL

  构造低位连续为 1 的数据，例如：

  aWidth = 10
  pageData[0] = 0x000003FF

  二进制写法为：

  00000000 00000000 00000011 11111111

  低 10 bit 为 1。

  TX_WORDFLIP 的作用是让硬件按期望的输出方向解释/翻转 word 中的 bit 顺序，使软件构造的“连续 1”能够对应串行输出开始处的高电平脉冲。

  从当前 RDB 字段名只能确定它启用了 word flip；精确到“先输出 bit0 还是 bit31”的物理顺序仍需要 TXSLZR TRM 才能完全确认。但源码注释已经明确说明，这一步是为 ToF pulse pattern 的正确输出顺序服务。

  ———

  # 7. 计算脉冲在 page 中的位置

  代码：

  uint32_t wordIdx = aWidth / 32UL;
  uint32_t bitIdx  = aWidth % 32UL;

  每个 word 有 32 bit，所以：

  wordIdx = 脉冲跨过了多少个完整的 32-bit word
  bitIdx  = 在最后一个 word 中还需置高多少个 bit

  默认配置：

  aWidth = 10
  wordIdx = 10 / 32 = 0
  bitIdx  = 10 % 32 = 10

  也就是说：

  脉冲没有填满任何一个完整 word；
  第 0 个 word 的前 10 个有效输出 bit 为 1；
  之后全部为 0。

  ———

  # 8. 构造 ToF pulse pattern

  核心代码位于 app/src/bcm/txs_helper.c:66：

  for (idx = 0UL; idx < pageInfo.pageSize; idx++) {
      if (idx < wordIdx) {
          pageInfo.pageData[idx] = 0xFFFFFFFFUL;
      } else if (idx == wordIdx) {
          pageInfo.pageData[idx] = (1UL << bitIdx) - 1UL;
      } else {
          pageInfo.pageData[idx] = 0UL;
      }
  }

  逻辑为：

  完整覆盖的 word：写 0xFFFFFFFF
  最后一个部分覆盖 word：写 bitIdx 个连续 1
  剩余所有 word：写 0

  因此它生成的目标 pattern 是：

  111111111111...111000000000...000
  <---- aWidth ---->

  也就是一个单脉冲，而不是连续方波，也不是 PWM。

  ———

  ## 默认 2 ns 脉冲时的 page 内容

  默认：

  aWidth  = 10
  wordIdx = 0
  bitIdx  = 10

  所以：

  pageData[0] = (1UL << 10) - 1UL
              = 0x000003FFUL;

  并且：

  pageData[1]   = 0x00000000UL;
  pageData[2]   = 0x00000000UL;
  ...
  pageData[255] = 0x00000000UL;

  逻辑上表示：

  第 0 ~ 9 个 serial tick：高
  第 10 ~ 8191 个 serial tick：低

  所以输出波形是：

  TX trigger 到来
      |
      v
  输出高电平 2 ns
      |
      v
  保持低电平，直到 pattern 结束或下一次 trigger

  ———

  ## 其他脉冲宽度举例

   输入脉宽    aWidth    wordIdx    bitIdx    关键 page 内容
  ━━━━━━━━━━  ━━━━━━━━  ━━━━━━━━━  ━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
     0.2 ns         1          0         1    pageData[0] = 0x00000001
  ──────────  ────────  ─────────  ────────  ────────────────────────────────────────────────────
     1.0 ns         5          0         5    pageData[0] = 0x0000001F
  ──────────  ────────  ─────────  ────────  ────────────────────────────────────────────────────
     2.0 ns        10          0        10    pageData[0] = 0x000003FF
  ──────────  ────────  ─────────  ────────  ────────────────────────────────────────────────────
     6.4 ns        32          1         0    pageData[0] = 0xFFFFFFFF, pageData[1] = 0
  ──────────  ────────  ─────────  ────────  ────────────────────────────────────────────────────
     6.6 ns        33          1         1    pageData[0] = 0xFFFFFFFF, pageData[1] = 0x00000001
  ──────────  ────────  ─────────  ────────  ────────────────────────────────────────────────────
    12.8 ns        64          2         0    pageData[0..1] = 0xFFFFFFFF, pageData[2] = 0

  对于 bitIdx == 0 的情况，例如 aWidth = 32：

  (1UL << 0) - 1UL = 0

  这是正确的：前一个完整 word 已经提供了 32 个高电平 bit，后一个 word 不需要再置任何 bit。

  ———

  # 9. 将 pattern 写入 TXSLZR page memory

  构造完 pageData[] 后：

  TXSLZR_DrvPatGenWrPage(TXSLZR_HW_ID_0, &pageInfo);

  见 app/src/bcm/txs_helper.c:76。

  底层流程是：

  设置 PG memory 写地址为 pageStartAddr = 0
      ->
  如果 page 大小大于 1，打开 auto increment
      ->
  连续写入 pageData[0] 到 pageData[255]

  由于 page size 是 256，所以硬件侧会收到 256 次 32-bit pattern memory 写入。

  对应底层实现见 drivers/bcm8915x/drivers/txslzr/txslzr_drv.c:405。

  写入时使用的寄存器为：

  PG_MEM_IND_WR_CONTROL
  PG_MEM_IND_WR_DATA

  其中：

  PG_MEM_IND_WR_CONTROL[7:0]  = 起始地址
  PG_MEM_IND_WR_CONTROL[16]   = auto-increment enable
  PG_MEM_IND_WR_DATA[31:0]    = pattern word data

  ———

  # 10. 配置 sequence memory

  pattern page 只是“波形数据”；TXSLZR 还需要 sequence memory 来决定播放哪个 page、播放顺序以及何时结束。

  函数定义：

  #define TXSLZR_SEQ_SIZE (TXSLZR_NUM_PAGES)

  因为只使用一页：

  TXSLZR_SEQ_SIZE = 1

  所以：

  uint32_t seq_mem[1];

  接下来生成 sequence entry：

  for (idx = 0UL; idx < TXSLZR_SEQ_SIZE; idx++) {
      seq_mem[idx] =
          idx << TXSLZR_SEQ_MEM_IND_WR_DATA_SPI_DATA_SHIFT;
  }

  当前只有 idx = 0：

  seq_mem[0] = 0 << 16 = 0

  随后设置最后一项的 end flag：

  seq_mem[TXSLZR_SEQ_SIZE - 1UL] |=
      TXSLZR_SEQ_MEM_IND_WR_DATA_END_FLAG_MASK;

  因此：

  seq_mem[0] = 0x00000001

  含义是：

  选择 page 0
  并且这就是本次 sequence 的最后一个 entry

  相关 sequence memory 字段为：

  bit[31:16]：SPI_DATA / sequence data field
  bit[10:4] ：repeat count
  bit[0]    ：END_FLAG

  当前代码把 idx 写入 bit[31:16]，用作 sequence 中的 page 编号/页选择值；最后一项设置 END_FLAG。

  随后写入 sequence memory：

  TXSLZR_REG->seq_mem_ind_wr_control =
      TXSLZR_SEQ_MEM_IND_WR_CONTROL_INCR_MODE_MASK;

  TXSLZR_REG->seq_mem_ind_wr_data = seq_mem[0];

  这里直接赋值 control register：

  INCR_MODE = 1
  ADDR = 0

  即从 sequence memory 地址 0 开始自动递增写入。

  最终 sequence 实际只有一个动作：

  播放 page 0
  到达 end flag 后结束该 sequence

  ———

  # 11. 为什么要开 continuous mode

  最后阶段：

  TXSLZR_REG->seq_control |= TXSLZR_SEQ_CONTROL_CONT_MODE_MASK;
  TXSLZR_REG->seq_control |= TXSLZR_SEQ_CONTROL_SEQ_EN_MASK;

  即：

  SEQ_CONTROL.CONT_MODE = 1
  SEQ_CONTROL.SEQ_EN    = 1

  含义上可理解为：

  开启 sequence 控制器；
  配置为连续模式，使单页 sequence 可在后续触发/工作周期中重复使用。

  随后：

  TXSLZR_REG->control |= TXSLZR_CONTROL_PG_EN_MASK;

  设置：

  CONTROL.PG_EN = 1

  即启用 pattern generator。

  最终 TXSLZR 的关键状态是：

   寄存器字段                              配置值    作用
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━
   CONTROL.NUM_PAGES                            0    硬件编码：使用 1 个 page
  ───────────────────────────  ───────────────────  ───────────────────────────
   PAGE_SZ_0[7:0]                            0xFF    page 0 长度：256 word
  ───────────────────────────  ───────────────────  ───────────────────────────
   SER_TX_CTRL_2.TX_WORDFLIP                    1    调整 word 内 bit 输出顺序
  ───────────────────────────  ───────────────────  ───────────────────────────
   SEQ_MEM[0]                   page 0 + end flag    sequence 只播放 page 0
  ───────────────────────────  ───────────────────  ───────────────────────────
   SEQ_CONTROL.CONT_MODE                        1    连续模式
  ───────────────────────────  ───────────────────  ───────────────────────────
   SEQ_CONTROL.SEQ_EN                           1    使能 sequence
  ───────────────────────────  ───────────────────  ───────────────────────────
   CONTROL.PG_EN                                1    使能 pattern generator

  ———

  # 12. 完整执行逻辑

  把函数压缩成流程图，可以理解为：

  aWidth（单位：0.2 ns）
      |
      v
  计算 wordIdx = aWidth / 32
  计算 bitIdx  = aWidth % 32
      |
      v
  生成 256-word pattern page

  word 0 ... wordIdx - 1 : 0xFFFFFFFF
  word wordIdx            : (1 << bitIdx) - 1
  其余 word               : 0x00000000
      |
      v
  写入 TXSLZR page 0
      |
      v
  sequence memory 配置为：
      播放 page 0
      sequence end
      |
      v
  使能 continuous sequence
      |
      v
  使能 pattern generator
      |
      v
  等待 ACQCMN 的 TXSLZR trigger
      |
      v
  输出 ToF 高脉冲

  默认配置下：

  TX trigger
      |
      v
  高电平：10 bit × 0.2 ns = 2 ns
      |
      v
  低电平：剩余 8182 bit × 0.2 ns
      |
      v
  pattern 结束，等待下一次触发/连续 sequence 周期

  ———

  # 13. 与当前 5 µs acquisition 周期的时序关系

  默认配置：

  ToF pulse width            = 2 ns
  TX pattern page duration   = 256 × 32 × 0.2 ns = 1.6384 us
  Acquisition trigger period = 5 us

  因此每个 acquisition 周期的时间关系大致是：

  0 us             TXSLZR 获得 trigger
  0 ~ 2 ns         TXSLZR 输出 ToF 高脉冲
  2 ns ~ 1.6384 us TXSLZR 输出 pattern 中剩余低电平
  1.6384 ~ 5 us    空闲，等待下一次 acquisition trigger
  5 us             下一次采集触发

  同时，按照 ACQ_SetupTriggerDelays() 的寄存器级配置：

  TXSLZR delay = 1
  HSADC  delay = 10
  HSREF  delay = 10

  所以系统意图是：

  先让 TXSLZR 发射短 ToF pulse，
  随后 HSADC 和 HSREF 启动采样，
  从而捕获发射脉冲之后的返回光/电信号。

  严格的物理纳秒级关系仍然需要确认：

  - ACQCMN_TRG_OUT_CTRL.PULSE_DELAY 的 tick 单位；
  - TXSLZR trigger 到 serializer 首 bit 输出的内部延迟；
  - HSADC trigger 到首 sample 的内部 pipeline latency；
  - 发送链路、激光器、TIA、模拟前端的传播延迟。

  ———

  # 14. 重要边界条件与现有实现风险

  ## 1. aWidth 没有参数检查

  当前函数没有检查：

  aWidth == 0
  aWidth 是否超过 page 容量
  aWidth 是否适合当前 page size

  对于默认 aPageSize = 256：

  最大可表达 bit 数 = 256 × 32 = 8192 bit
  最大时间          = 8192 × 0.2 ns = 1.6384 us

  如果：

  aWidth > 8192

  循环只会把全部 256 word 写成 0xFFFFFFFF，实际输出会饱和为整页高电平，而不会得到用户期望的更宽脉冲。

  也就是说，当前效果会变为：

  请求 > 1.6384 us
  实际最多只能生成约 1.6384 us 的高电平

  但函数不会报错。

  ## 2. aPageSize > 256 会越界写栈内存

  TXSLZR_PageInfoType 的数组固定为：

  uint32_t pageData[256UL];

  如果未来调用：

  TXSH_SetupTofPulse(width, 257);

  则下面的循环会访问：

  pageInfo.pageData[256]

  造成栈数组越界。

  因此，aPageSize 必须满足：

  1 <= aPageSize <= 256

  但目前函数没有显式检查。

  ## 3. aPageSize == 0 会发生无符号下溢

  调用：

  (pageInfo.pageSize / TXSLZR_NUM_PAGES) - 1UL

  若：

  aPageSize = 0

  结果是：

  0 - 1UL = 0xFFFFFFFF

  然后传给页配置函数，显然不是有效配置。

  当前调用固定传入 256UL，所以实际运行没有问题，但接口自身不够健壮。

  ## 4. aWidth 的单位需要调用方保持一致

  TXSH_SetupTofPulse() 的 aWidth 单位不是 ns，而是：

  0.2 ns / tick

  当前调用方这样转换：

  tofPulseWidth * 5UL

  这是因为配置中的 tofPulseWidth 以 ns 表示：

  1 ns = 5 × 0.2 ns

  因此：

  TXSH_SetupTofPulse(tofPulseWidthNs * 5UL, 256UL);

  是正确的。

  但如果未来 tofPulseWidth 改为：

  - ps；
  - 0.1 ns；
  - 5 GHz tick；
  - 浮点 ns；

  这个乘 5 的关系必须同步修改。

  ## 5. pattern 的电平极性还依赖 TXSLZR 其他设置

  函数构造的 pattern 是“逻辑 1 表示 pulse active”，但外部 pin 上看到的是高脉冲还是低脉冲，还可能受：

  SER_TX_CTRL_2.TX_INVERT
  外部驱动链路极性
  激光器/发射器输入极性

  影响。

  该函数只设置：

  SER_TX_CTRL_2.TX_WORDFLIP

  没有设置：

  SER_TX_CTRL_2.TX_INVERT

  因此当前物理输出极性依赖硬件默认值或前级初始化配置。

  ———

  # 15. 简要总结

  TXSH_SetupTofPulse() 做的是“配置 TXSLZR 的 ToF 单脉冲 pattern”，不是单纯设置一个宽度字段。

  当前默认配置的实际含义是：

  输入 ToF 脉宽：2 ns
  转换到 5 GHz tick：10 tick
  pattern page：256 × 32 bit = 8192 bit
  page 0 内容：
      bit 0 ~ 9      = 1
      bit 10 ~ 8191  = 0

  sequence：
      只播放 page 0
      page 0 后结束 sequence
      continuous mode enabled

  TXSLZR：
      word flip enabled
      sequencer enabled
      pattern generator enabled

  它与前面的 trigger delay 配合后，目标时序为：

  ACQ trigger
      |
      +-- TXSLZR：较早收到 trigger，输出 2 ns ToF 发射脉冲
      |
      +-- HSADC：较后收到 trigger，开始回波采样
      |
      +-- HSREF：与 HSADC 对齐，进行参考采样

  当前实现可以满足默认 2 ns 脉冲，但建议后续为 aWidth 和 aPageSize 增加范围校验，至少限制：

  1 <= aPageSize <= 256
  0 <= aWidth <= aPageSize × 32

  这样可以避免 page buffer 越界、脉宽静默饱和和无符号下溢问题。

