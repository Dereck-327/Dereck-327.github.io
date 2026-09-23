# CCUH_SetupAcqTrigger() 

是整个采集触发链路的“节拍发生器 + 路由配置器”。它并不直接触发 TXSLZR、HSADC 或 HSREF，而是完成以下几件事：

  1. 配置 CAPCOM1 生成一条“有效采集行窗口”；
  2. 将 NSU_TRG2 接入 CAPCOM1 的 Timer1 Event0，作为这一行采集时序的外部同步事件；
  3. 把 CAPCOM1 的 TMR1_COUT0 路由为主 acquisition trigger (acq_trg) 来源；
  4. 配置 ACQ trigger counter，使其按 aTrigInterval 周期产生采集触发；
  5. 打开 acquisition trigger counter。

  代码位置：app/src/bcm/ccu_helper.c:136。

  void CCUH_SetupAcqTrigger(uint32_t aTrigInterval,
                            uint32_t aNumSamples,
                            uint32_t aLineGap)

  函数参数单位都是微秒或数量：

   参数             含义                                           当前默认值
  ━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━
   aTrigInterval    两次 acquisition trigger 的间隔，单位 µs             5 µs
  ───────────────  ─────────────────────────────────────────────  ────────────
   aNumSamples      每行要进行多少次 acquisition                         2000
  ───────────────  ─────────────────────────────────────────────  ────────────
   aLineGap         一行结束后到下一行开始前的空档时间，单位 µs       1000 µs

  默认值来自 app/src/tasks/acq_task.c:25：

  #define ACQ_TRIGGER_INTERVAL_US  (5UL)
  #define ACQ_NUM_ACQS_PER_LINE    (2000UL)
  #define ACQ_LINE_GAP_US          (1000UL)

  ———

  ## 一、函数在整体采集链路中的位置

  在 ACQ_Start() 中，顺序是：

  HSADC 初始化与校准
      ->
  配置 FIR（如果启用）
      ->
  配置 TXSLZR / HSADC / HSREF 的附加触发延迟
      ->
  配置 TX Serializer ToF 脉冲 pattern
      ->
  CCUH_SetupAcqTrigger()
      ->
  CCUH_StartNsuTrigger(2, 500000)

  调用位置见 app/src/tasks/acq_task.c:189。

  完整触发路径可以理解为：

  NSU channel 2 event
      |
      v
  ACQCMN Timer1 Event0
      |
      v
  CAPCOM1 / Timer1
      |
      v
  CAPCOM1 COUT0
      |
      v
  ACQCMN acq_trg source
      |
      v
  ACQ trigger counter
      |
      v
  ACQCMN trigger outputs
      |
      +--> TXSLZR trigger
      +--> HSADC trigger
      +--> HSREF trigger

  其中，最后三路的相对 delay 由前面的 ACQ_SetupTriggerDelays() 配置。

  ———

  # 二、先计算“一行”的时间结构

  函数一开始：

  uint32_t activeLineTimeUs = aTrigInterval * aNumSamples;
  uint32_t lineTimeUs = activeLineTimeUs + aLineGap;
  uint32_t startTime = 1UL;
  uint32_t endTime = startTime + activeLineTimeUs;

  默认参数代入后：

  aTrigInterval = 5 us
  aNumSamples   = 2000
  aLineGap      = 1000 us

  得到：

  activeLineTimeUs = 5 × 2000
                   = 10000 us
                   = 10 ms

  lineTimeUs = 10000 + 1000
             = 11000 us
             = 11 ms

  startTime = 1 us
  endTime   = 1 + 10000
            = 10001 us

  所以一行的时序目标是：

  0 us        行周期开始
  1 us        有效采集窗口开始
  10001 us    有效采集窗口结束
  11000 us    本行结束，进入下一行

  图示如下：

  一行总周期：11 ms
  |------------------------------------------------------------------|

  0 us      1 us                           10001 us         11000 us
  |---------|================================|----------------|
            <------ 有效采集窗口 10 ms ------> <--- gap 1 ms -->

            每 5 us 产生一次采集 trigger
            共计目标：2000 次

  从配置意图看：

  - 有效窗口为 10 ms；
  - 在窗口内每隔 5 µs 做一次 ToF 发射与 ADC 采集；
  - 一行理论上包含 2000 次 acquisition；
  - 行结束后保留 1 ms 的空闲时间，供系统进入下一行、处理/同步或等待扫描机构动作。

  因此，行频率约为：

  1 / 11 ms ≈ 90.91 Hz

  ———

  # 三、第一部分：配置 CAPCOM1 的 1 µs 时间基准和行周期

  代码：

  CCUH_CapComConfigure(1UL,
                       CCUH_CAPCOM_PRE_SCALAR,
                       lineTimeUs,
                       1UL);

  其中：

  #define CCUH_CAPCOM_PRE_SCALAR (100UL)

  即调用等价于：

  CCUH_CapComConfigure(1, 100, 11000, 1);

  这表示使用：

  CAPCOM1
  预分频值：100
  自动重装周期：11000
  Event0 mask/control：1

  辅助函数位于 app/src/bcm/ccu_helper.c:48。

  ———

  ## 1. 配置预分频器

  CAPCOM_REG[aCCTIdx]->psc_ratio =
      (CAPCOM_REG[aCCTIdx]->psc_ratio & ~CCT_PSC_RATIO_MASK) |
      ((aPreScaler - 1UL) << CCT_PSC_RATIO_SHIFT);

  当前：

  aPreScaler = 100
  写入 PSC_RATIO = 99

  代码注释说明 CAPCOM 基础时钟为 100 MHz：

  100 MHz -> 一个原始时钟周期为 10 ns
  预分频 100 -> 一个 CAPCOM tick 为 1 µs

  所以后续写入 CAPCOM 的：

  ARR
  CCR A
  CCR B

  都按 1 µs 为单位解释。

  这里利用的是常见的“寄存器值 = 实际分频值 - 1”编码：

  PSC_RATIO = 99
  实际除数    = 100

  ———

  ## 2. 配置 CAPCOM 的自动重装周期

  CAPCOM_REG[aCCTIdx]->udc_ctrl =
      (CAPCOM_REG[aCCTIdx]->udc_ctrl & ~CCT_UDC_CTRL_ARR_MASK) |
      ((aReload - 1UL) << CCT_UDC_CTRL_ARR_SHIFT);

  当前：

  aReload = lineTimeUs = 11000
  ARR     = 10999

  寄存器字段为：

  CCT_UDC_CTRL.ARR = bit[31:16]

  因此 CAPCOM1 的计数周期是：

  11000 × 1 µs = 11 ms

  计数到周期末后重新装载，进入下一行。

  ———

  ## 3. 强制启用计数器

  CAPCOM_REG[aCCTIdx]->udc_ctrl |=
      CCT_UDC_CTRL_HW_EN_OVR_MASK;

  即设置：

  CCT_UDC_CTRL.HW_EN_OVR = 1

  含义是覆盖正常硬件使能条件，让计数器可以运行。

  然后：

  CAPCOM_REG[aCCTIdx]->udc_ctrl &=
      ~CCT_UDC_CTRL_UDEN_MASK;

  清除：

  CCT_UDC_CTRL.UDEN = 0

  按照源码注释，这代表配置为单向向上计数：

  Unidirectional up-counter

  即计数器从起点向上计数，到达 ARR 后回绕/重装。

  ———

  ## 4. Event0 与计数器/预分频器的关系

  函数还做了：

  CAPCOM_REG[aCCTIdx]->psc_ctrl &=
      ~(aEvtEnMsk << CCT_PSC_CTRL_MASK_EVT0_SHIFT);

  CAPCOM_REG[aCCTIdx]->udc_ctrl &=
      ~(aEvtEnMsk << CCT_UDC_CTRL_UDC_MASK_EVT0_SHIFT);

  当前：

  aEvtEnMsk = 1

  所以清除的是：

  PSC_CTRL.MASK_EVT0
  UDC_CTRL.UDC_MASK_EVT0

  从字段名字和注释看，这意味着 Event0 不被 mask，它可以参与 CAPCOM 的同步/触发控制。

  后面该 Event0 会连接到：

  NSU_TRG2

  因此整体意图是让外部的 NSU channel 2 事件与 CAPCOM1 的行时序相关联。

  不过，RDB 头文件只提供了字段名和位定义，没有给出 Event0 对 CAPCOM counter 的精确硬件语义，例如：

  - Event0 是启动 counter；
  - Event0 是复位 counter；
  - Event0 是重新同步 counter；
  - Event0 只作用于 prescaler；
  - Event0 对 compare output 的具体作用。

  源码注释明确将其称为 trigger source，因此可以确认它被用于触发/同步，但具体是“从 Event0 的哪个瞬间开始计行周期”，仍需以 CCT/ACQCMN TRM 为准。

  ———

  # 四、第二部分：生成有效采集行窗口

  代码：

  CCUH_CapComCompareConfigure(1UL,
                               startTime,
                               endTime,
                               1UL,
                               0UL);

  代入默认值：

  CCUH_CapComCompareConfigure(1, 1, 10001, 1, 0);

  该函数位于 app/src/bcm/ccu_helper.c:84。

  它使用 CAPCOM1 的 channel 0：

  Channel 0, sub-channel A
  Channel 0, sub-channel B

  配置以下内容。

  ———

  ## 1. 使能 A/B compare mode

  CAPCOM_REG[aCCTIdx]->cct_ccr_regs[0UL].ccr0_ctrl |=
      CCT_CCR_CTRL_A_CCSEL_MASK;

  CAPCOM_REG[aCCTIdx]->cct_ccr_regs[0UL].ccr0_ctrl |=
      CCT_CCR_CTRL_B_CCSEL_MASK;

  设置：

  CCR0_CTRL.A_CCSEL = 1
  CCR0_CTRL.B_CCSEL = 1

  含义是将 channel 0 的 A/B 子通道切换到 compare 模式。

  ———

  ## 2. 配置 XOR 行为

  regVal &= ~(CCT_CCR_CTRL_A_EN_XOR_MASK |
              CCT_CCR_CTRL_B_EN_XOR_MASK);

  CAPCOM_REG[aCCTIdx]->cct_ccr_regs[0UL].ccr0_ctrl =
      regVal |
      (aAEnXor << CCT_CCR_CTRL_A_EN_XOR_SHIFT) |
      (aBEnXor << CCT_CCR_CTRL_B_EN_XOR_SHIFT);

  当前：

  aAEnXor = 1
  aBEnXor = 0

  因此：

  A_EN_XOR = 1
  B_EN_XOR = 0

  代码注释把这段描述为：

  Active signal from startTime to endTime - Enable XOR operation

  意图是通过 compare point 和 output XOR/compare 逻辑生成一段“有效采集窗口”：

  开始时刻：1 us
  结束时刻：10001 us

  也就是说，这个输出不是每 5 µs 的单次采集脉冲，而是“本行是否处于有效采集区间”的门控/窗口信号。

  由于当前仓库中的 RDB 没有提供 CAPCOM compare 输出状态机的完整真值表，不能只凭 A_EN_XOR=1、B_EN_XOR=0 严格断言某个边沿是上升沿还是下降沿。但按照函数注释和参数命名，其目标是产生：

  1 us 到 10001 us 之间为 active 的窗口

  ———

  ## 3. 设置 compare 时刻

  CAPCOM_REG[aCCTIdx]->cct_ccr_regs[0UL].ccr0_val =
      regVal | (aAVal << CCT_CCR_VAL_A_VAL_SHIFT);

  CAPCOM_REG[aCCTIdx]->cct_ccr_regs[0UL].ccr0_val =
      regVal | (aBVal << CCT_CCR_VAL_B_VAL_SHIFT);

  默认值：

  CCR0_VAL.A = 1
  CCR0_VAL.B = 10001

  因为 CAPCOM tick 是 1 µs，所以对应：

  A compare：行开始后 1 µs
  B compare：行开始后 10001 µs

  可以画成：

  CAPCOM1 counter（1 µs/tick）

  counter:
  0      1                                    10001      11000
  |------|====================================|----------|
         A compare                             B compare
         采集窗口开始                           采集窗口结束

  ———

  # 五、第三部分：使能 CAPCOM1 的 COUT0 输出

  代码：

  CCUH_CapComOutputEnable(1UL, 0UL, 0UL, 0UL);

  位于 app/src/bcm/ccu_helper.c:118。

  即：

  CAPCOM instance = 1
  output index    = 0
  channel         = 0
  subchannel      = A

  函数中：

  CAPCOM_REG[aCCTIdx]->out_ctrl |=
      1UL << (8UL * aOut + 2UL * aChnl + aSubChnl);

  代入参数：

  bit position = 8 × 0 + 2 × 0 + 0
               = 0

  所以它设置：

  CAPCOM1.OUT_CTRL bit0

  即使能：

  CAPCOM1 COUT0 <- channel 0, subchannel A

  随后：

  CAPCOM_REG[aCCTIdx]->ctrla |= CCT_CTRLA_EN_MASK;

  设置：

  CAPCOM1.CTRLA.CCT_EN = 1

  这一步会使能 CAPCOM1 定时器。

  代码中特意注明：

  must be last step of initialization

  不过这里是 CCUH_CapComOutputEnable() 内的“CAPCOM 局部配置最后一步”。从整个 CCUH_SetupAcqTrigger() 看，CAPCOM timer 在后续 ACQCMN trigger route 配置之前就已经 enable。

  当前调用顺序中，真正的 NSU trigger 是在 CCUH_SetupAcqTrigger() 完成后才由：

  CCUH_StartNsuTrigger(2UL, 500000UL);

  启动的，因此通常不会在路由还没配置完时就产生有效的 NSU 同步事件。

  ———

  # 六、第四部分：把 NSU_TRG2 接到 Timer1 Event0

  代码：

  regVal = ACQCMN_REG->tmr1_event0_src_ctrl &
           ~ACQCMN_TMR1_EVENT0_SRC_CTRL_NSU_TRG2_MASK;

  ACQCMN_REG->tmr1_event0_src_ctrl =
           regVal |
           (4UL << ACQCMN_TMR1_EVENT0_SRC_CTRL_NSU_TRG2_SHIFT);

  随后：

  ACQCMN_REG->tmr1_event0_src_sel |=
      ACQCMN_TMR1_EVENT0_SRC_SEL_NSU_TRG2_EN_MASK;

  对应源码位置：app/src/bcm/ccu_helper.c:153。

  寄存器字段：

  TMR1_EVENT0_SRC_CTRL.NSU_TRG2 = bit[3:0]
  TMR1_EVENT0_SRC_SEL.NSU_TRG2_EN = bit0

  当前写入：

  TMR1_EVENT0_SRC_CTRL.NSU_TRG2 = 4
  TMR1_EVENT0_SRC_SEL.NSU_TRG2_EN = 1

  代码注释说明这个配置含义为：

  Configure input trigger for Timer1 Event0 - NSU_TRG2 falling edge

  也就是选择：

  NSU_TRG2 的 falling edge

  作为 Timer1 Event0 的输入事件。

  从后续调用可知，NSU channel 2 会被安排在 500 µs 后产生：

  CCUH_StartNsuTrigger(2UL, 500000UL);

  该函数用 NSU 的 10 ns 计数器，将事件时间设置为：

  当前时间 + 500000 ns
  = 当前时间 + 500 µs

  因此，这个 NSU_TRG2 事件是整条采集时序的起始/同步事件。

  ———

  # 七、第五部分：将 TMR1_COUT0 选为主 acq_trg 来源

  代码：

  regVal = ACQCMN_REG->acq_trg_src_ctrl0 &
           ~ACQCMN_CAPCOM_CMN_SRC_CTRL_TMR1_COUT0_MASK;

  ACQCMN_REG->acq_trg_src_ctrl0 =
           regVal |
           (0UL << ACQCMN_CAPCOM_CMN_SRC_CTRL_TMR1_COUT0_SHIFT);

  ACQCMN_REG->acq_trg_src_sel |=
           ACQCMN_ACQ_TRG_SRC_SEL_TMR1_COUT0_EN_MASK;

  对应源码位置：app/src/bcm/ccu_helper.c:160。

  字段为：

  ACQ_TRG_SRC_CTRL0.TMR1_COUT0 = bit[19:16]
  ACQ_TRG_SRC_SEL.TMR1_COUT0_EN = bit4

  当前配置：

  TMR1_COUT0 source select field = 0
  TMR1_COUT0 enable = 1

  代码注释已经明确说明：

  Select Timer1 COUT0 as the source for the main acquisition trigger (acq_trg).

  也就是说，这里将 CAPCOM1 输出的时序窗口送入 ACQ Common 的 acquisition trigger 网络。

  此处形成的关系是：

  CAPCOM1 channel 0/A
      |
      v
  TMR1_COUT0
      |
      v
  ACQCMN acq_trg source

  之后 acq_trg 会进入后面的 acquisition trigger counter，并进一步分发到：

  TXSLZR
  HSADC
  HSREF

  ———

  # 八、第六部分：将微秒周期转换为 3.2 ns 计数周期

  代码：

  uint32_t acqTrigInterCycles =
      (aTrigInterval * 10000UL + 16UL) / 32UL;

  位置：app/src/bcm/ccu_helper.c:167。

  注释说明这个 counter 的单位是：

  3.2 ns per cycle

  换算公式：

  1 µs = 1000 ns
  一个 counter tick = 3.2 ns

  触发间隔对应 tick 数
  = aTrigInterval × 1000 / 3.2
  = aTrigInterval × 10000 / 32

  代码中额外加 16：

  (aTrigInterval * 10000UL + 16UL) / 32UL

  这是整数除法的“四舍五入到最近整数”写法。

  因为：

  16 = 32 / 2

  所以它等价于：

  round(aTrigInterval × 10000 / 32)

  ———

  ## 默认 5 µs 时的计算

  aTrigInterval = 5 µs

  代入：

  acqTrigInterCycles
  = (5 × 10000 + 16) / 32
  = 50016 / 32
  = 1563

  实际周期：

  1563 × 3.2 ns
  = 5001.6 ns
  = 5.0016 µs

  理论目标为 5.0000 µs，因此单个触发间隔的量化误差是：

  +1.6 ns

  这个误差来自 3.2 ns 的计时粒度，不能由整数计数器完全消除。

  对应的实际触发频率约为：

  1 / 5.0016 µs
  ≈ 199.936 kHz

  而理想 5 µs 周期是：

  200 kHz

  ———

  # 九、第七部分：写入 ACQ_TRG_CNTR_CTRL

  代码：

  regVal = ACQCMN_REG->acq_trg_cntr_ctrl &
           ~ACQCMN_ACQ_TRG_CNTR_CTRL_PROG_CNT_MASK;

  ACQCMN_REG->acq_trg_cntr_ctrl =
           regVal |
           ((acqTrigInterCycles - 1UL) <<
            ACQCMN_ACQ_TRG_CNTR_CTRL_PROG_CNT_SHIFT);

  默认配置：

  acqTrigInterCycles = 1563
  PROG_CNT           = 1563 - 1
                     = 1562
                     = 0x061A

  寄存器字段：

  ACQ_TRG_CNTR_CTRL.PROG_CNT = bit[15:0]
  ACQ_TRG_CNTR_CTRL.ACQ_TRG_CNTR_EN = bit16

  采用的是典型的“周期长度减一”编码：

  实际周期 = PROG_CNT + 1

  因此：

  PROG_CNT = 1562
  实际计数周期 = 1563 个 3.2 ns tick
  实际触发间隔 = 5.0016 µs

  随后：

  ACQCMN_REG->acq_trg_cntr_ctrl |=
      ACQCMN_ACQ_TRG_CNTR_CTRL_ACQ_TRG_CNTR_EN_MASK;

  设置 bit16：

  ACQ_TRG_CNTR_EN = 1

  即启用 acquisition trigger counter。

  ———

  # 十、默认配置的最终寄存器结果

  在默认配置下，函数产生的关键配置如下：

   模块       字段                                    值    解释
  ━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   CAPCOM1    PSC_RATIO                               99    100 MHz / 100 = 1 MHz，即 1 µs tick
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   CAPCOM1    UDC_CTRL.ARR                         10999    行周期为 11000 µs，即 11 ms
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   CAPCOM1    CCR0_VAL.A                               1    有效窗口起点约为 1 µs
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   CAPCOM1    CCR0_VAL.B                           10001    有效窗口终点约为 10001 µs
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   CAPCOM1    OUT_CTRL bit0                            1    使能 output0/channel0/A，即 TMR1_COUT0
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   CAPCOM1    CTRLA.CCT_EN                             1    启动 CAPCOM1
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   ACQCMN     TMR1_EVENT0_SRC_CTRL.NSU_TRG2            4    选择 NSU_TRG2 对应的事件/边沿编码
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   ACQCMN     TMR1_EVENT0_SRC_SEL.NSU_TRG2_EN          1    使能 NSU_TRG2 到 Timer1 Event0 的路由
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   ACQCMN     ACQ_TRG_SRC_SEL.TMR1_COUT0_EN            1    使能 TMR1_COUT0 作为 acq_trg 来源
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   ACQCMN     ACQ_TRG_CNTR_CTRL.PROG_CNT            1562    实际周期为 1563 × 3.2 ns
  ─────────  ───────────────────────────────────  ───────  ────────────────────────────────────────
   ACQCMN     ACQ_TRG_CNTR_CTRL.ACQ_TRG_CNTR_EN        1    启用 acquisition trigger counter

  ———

  # 十一、默认运行时序

  默认参数下，较完整的时序可以概括为：

  NSU channel 2 在约 500 µs 后产生事件
      |
      v
  NSU_TRG2 falling edge
      |
      v
  ACQCMN Timer1 Event0
      |
      v
  CAPCOM1 的 11 ms 行周期同步/开始
      |
      +-- 约 1 µs 后进入 active acquisition window
      |
      +-- 在约 10 ms 的有效窗口中，以约 5.0016 µs 周期产生 acq_trg
      |
      +-- 理论目标为 2000 次 acquisition
      |
      +-- 之后保留约 1 ms line gap
      |
      v
  下一行周期

  在每个 acq_trg 到来后：

  acq_trg
      |
      +-- TXSLZR trigger（加 TXSLZR delay）
      |       |
      |       +-- 输出 ToF 发送脉冲
      |
      +-- HSADC trigger（加 HSADC delay）
      |       |
      |       +-- 开始 ADC 单次采集
      |
      +-- HSREF trigger（加 HSREF delay）
              |
              +-- 开始参考采样

  ———

  # 十二、一个值得注意的量化误差

  代码希望在 10 ms 的有效窗口中得到：

  2000 × 5 µs = 10 ms

  但实际 acq_trg counter 的周期被量化到：

  5.0016 µs

  因此 2000 个周期的累计时间是：

  2000 × 5.0016 µs
  = 10003.2 µs
  = 10.0032 ms

  而代码定义的有效窗口是：

  10000 µs

  两者相差约：

  3.2 µs

  也就是约一个 3.2 ns tick 的 1000 倍，或者约 0.64 个 5 µs trigger 周期。

  这不一定意味着系统一定会少一次或多一次 trigger，因为最终是否发出触发还取决于：

  - TMR1_COUT0 的具体门控方式；
  - acq_trg counter 是否在窗口结束后立即停止/复位；
  - CAPCOM compare output 的真实输出状态机；
  - ACQCMN 中 trigger counter 的具体 gating 行为。

  但从纯数值上看，当前参数存在：

  目标有效窗口：10.0000 ms
  2000 个实际 trigger 周期：10.0032 ms

  的轻微不匹配。

  如果系统要求每行严格 2000 次采样，应当通过硬件计数器、DMA 完成计数、HSADC capture 状态或输出波形确认实际每行 trigger 数。

  ———

  # 十三、函数当前缺少的参数检查

  CCUH_SetupAcqTrigger() 返回 void，也没有输入参数保护。以下非法或极端值可能带来问题：

   情况                  当前风险
  ━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   aTrigInterval == 0    acqTrigInterCycles - 1UL 无符号下溢，写入极大值
  ────────────────────  ────────────────────────────────────────────────────────────────
   aNumSamples == 0      有效窗口长度为 0，endTime 与 startTime 重叠
  ────────────────────  ────────────────────────────────────────────────────────────────
   aLineGap 很大         lineTimeUs 可能超出 CAPCOM ARR 的 16-bit 容量
  ────────────────────  ────────────────────────────────────────────────────────────────
   lineTimeUs > 65536    ARR 仅 16 bit，实际写入会截断
  ────────────────────  ────────────────────────────────────────────────────────────────
   aTrigInterval 很大    PROG_CNT 仅 16 bit，周期可能截断
  ────────────────────  ────────────────────────────────────────────────────────────────
   运行中重复调用        未先停 CAPCOM 和 acq trigger counter，可能产生不连续或毛刺时序

  尤其是：

  CCT_UDC_CTRL.ARR          是 16 bit
  ACQ_TRG_CNTR_CTRL.PROG_CNT 是 16 bit

  所以需要满足：

  1 <= lineTimeUs <= 65536
  1 <= acqTrigInterCycles <= 65536

  默认值：

  lineTimeUs = 11000
  acqTrigInterCycles = 1563

  均在安全范围内。

  ———

  ## 简要总结

  CCUH_SetupAcqTrigger() 的作用可以压缩成一句话：

  > 用 CAPCOM1 建立“每行 11 ms、其中前约 10 ms 为有效采样区”的行时序，再用 3.2 ns 精度的 ACQ trigger counter 在有效窗口内按约 5 µs 的周期驱动 TXSLZR、HSADC 和 HSREF。

  默认配置下：

  行总时长：11 ms
  有效采集窗口：10 ms
  行间空闲：1 ms
  目标采集次数：2000 次/行
  目标 trigger 间隔：5 µs
  实际量化后的 counter 周期：5.0016 µs
  TXSLZR / HSADC / HSREF：由 acq_trg 驱动，并叠加各自输出 delay