  1. 当前代码中，NSU event channel 2 没有配置“周期触发”。
  2. CCUH_StartNsuTrigger(2UL, 500000UL) 只给 NSU channel 2 设置了一个未来约 500 µs 的比较时间，因此它的作用是“一次性启动整条采集时序”。
  3. 当前真正的采集触发周期不是 NSU channel 2 产生的，而是后面的 ACQCMN acquisition trigger counter 产生的。
  4. 当前默认配置下，实际采集触发间隔约为 5.0016 µs，目标值是 5 µs。
  5. ACQCMN 是 Acquisition Common，中文可以理解为“采集公共控制/触发模块”，它负责把 NSU、CAPCOM、采集计数器以及 TXSLZR、HSADC、HSREF 等模块连接起来。

  ———

  # 一、当前 NSU channel 2 的“周期”是多少

  当前调用代码：

  CCUH_StartNsuTrigger(2UL, 500000UL);

  位置：app/src/tasks/acq_task.c:198。

  对应函数：

  void CCUH_StartNsuTrigger(uint32_t aChannel, uint32_t aDelayNs)
  {
      uint32_t cur_cntr_val_l = NSU_REG->bin_l_nanosec_counter_rd;
      uint32_t cur_cntr_val_u = NSU_REG->bin_u_nanosec_counter_rd;

      uint32_t new_cntr_val_l =
          cur_cntr_val_l + (aDelayNs / 10UL);

      uint32_t new_cntr_val_u =
          (new_cntr_val_l < cur_cntr_val_l) ?
          (cur_cntr_val_u + 1UL) :
          cur_cntr_val_u;

      NSU_REG->nsuevtgenchan[aChannel].evtgen_ch0_cmptime_ns_l =
          new_cntr_val_l;

      NSU_REG->nsuevtgenchan[aChannel].evtgen_ch0_cmptime_ns_u =
          new_cntr_val_u;

      NSU_REG->evtgen_control |= 1UL << aChannel;
  }

  这里的 aDelayNs 为：

  500000 ns = 500 µs

  NSU 当前使用的时间计数单位是 10 ns，因此：

  500000 ns / 10 ns
  = 50000 个 NSU counter tick

  所以 channel 2 被设置成：

  比较时间 = 当前 NSU 时间 + 500 µs

  而不是：

  每隔 500 µs 触发一次

  ———

  ## 当前代码中没有配置周期寄存器

  NSU event generator 的寄存器结构主要包括：

  NSU_EVTGEN_CONTROL_TYPE evtgen_control;
  NSU_EVTGEN_TYPE nsuevtgenchan[8];

  每个 event channel 只有一个比较时间：

  evtgen_ch0_cmptime_ns_l
  evtgen_ch0_cmptime_ns_u

  没有看到：

  period
  reload
  repeat interval
  auto-increment compare time

  之类的周期配置寄存器。

  因此，从当前实现可以确定：

  NSU channel 2 被配置的是一个绝对比较时间点。

  当前源码没有配置 channel 2 的自动周期重装。

  更准确地说：

  NSU channel 2：一次性事件，用于启动采集
  ACQCMN acq_trg counter：连续产生每次采集的周期触发

  NSU 事件生成控制寄存器只有 channel enable 位：

  #define NSU_EVTGEN_CONTROL_CH_EN_MASK (0xffUL)

  启用 channel 2 的代码：

  NSU_REG->evtgen_control |= 1UL << 2;

  也就是：

  EVTGEN_CONTROL.CH_EN[2] = 1

  它使能 channel 2 的事件比较，但没有设置周期重载。

  ———

  # 二、NSU channel 2 实际负责什么

  当前采集启动流程：

  初始化 ACQCMN
      ->
  初始化 HSAFE、PLL、ADC 时钟
      ->
  初始化 HSADC
      ->
  配置 TXSLZR
      ->
  配置 CAPCOM 和 ACQCMN 触发路由
      ->
  CCUH_StartNsuTrigger(2, 500000)

  最后一步将 NSU channel 2 安排在未来 500 µs 产生事件。

  因此 channel 2 的作用是：

  延迟启动整条采集时序

  它相当于一个启动同步脉冲：

  系统初始化完成
      |
      | 等待 500 µs
      v
  NSU_TRG2 event
      |
      v
  启动/同步 Timer1
      |
      v
  产生采集有效窗口
      |
      v
  产生周期性 acq_trg

  这里的 500 µs 不是 ADC 采样间隔，而是系统启动延迟。

  ———

  # 三、NSU event channel 2 如何转换成 ACQCMN 的 acq_trg

  完整路径如下：

  NSU channel 2 compare event
          |
          | 输出 NSU_TRG2
          v
  ACQCMN Timer1 Event0 输入选择
          |
          v
  CAPCOM1 / Timer1
          |
          | 产生 TMR1_COUT0
          v
  ACQCMN acquisition trigger source mux
          |
          v
  ACQ trigger counter
          |
          | 每约 5 µs 产生一个 acq_trg
          v
  ACQCMN trigger distribution
          |
          +--> TXSLZR
          +--> HSADC
          +--> HSREF

  下面按代码逐步展开。

  ———

  # 四、第一步：NSU 产生 NSU_TRG2

  CCUH_StartNsuTrigger() 使用 NSU 的自由运行纳秒计数器：

  uint32_t cur_cntr_val_l =
      NSU_REG->bin_l_nanosec_counter_rd;

  uint32_t cur_cntr_val_u =
      NSU_REG->bin_u_nanosec_counter_rd;

  然后计算目标时间：

  new_cntr_val_l = cur_cntr_val_l + (aDelayNs / 10UL);

  当前：

  aDelayNs = 500000 ns
  aDelayNs / 10 = 50000 tick

  将目标时间写入 channel 2：

  NSU_REG->nsuevtgenchan[2].evtgen_ch0_cmptime_ns_l =
      new_cntr_val_l;

  NSU_REG->nsuevtgenchan[2].evtgen_ch0_cmptime_ns_u =
      new_cntr_val_u;

  最后使能 channel 2：

  NSU_REG->evtgen_control |= 1UL << 2;

  当 NSU 当前计数器同时匹配：

  upper 32 bit
  lower 32 bit

  时，NSU 产生 channel 2 事件：

  NSU event channel 2

  ACQCMN 中将这个事件命名为：

  NSU_TRG2

  ———

  # 五、第二步：ACQCMN 将 NSU_TRG2 接到 Timer1 Event0

  在 CCUH_SetupAcqTrigger() 中：

  regVal = ACQCMN_REG->tmr1_event0_src_ctrl &
           ~ACQCMN_TMR1_EVENT0_SRC_CTRL_NSU_TRG2_MASK;

  ACQCMN_REG->tmr1_event0_src_ctrl =
      regVal |
      (4UL << ACQCMN_TMR1_EVENT0_SRC_CTRL_NSU_TRG2_SHIFT);

  然后：

  ACQCMN_REG->tmr1_event0_src_sel |=
      ACQCMN_TMR1_EVENT0_SRC_SEL_NSU_TRG2_EN_MASK;

  对应寄存器字段：

  #define ACQCMN_TMR1_EVENT0_SRC_CTRL_NSU_TRG2_MASK  (0xFUL)
  #define ACQCMN_TMR1_EVENT0_SRC_SEL_NSU_TRG2_EN_MASK (0x1UL)

  当前配置结果：

  TMR1_EVENT0_SRC_CTRL.NSU_TRG2 = 4
  TMR1_EVENT0_SRC_SEL.NSU_TRG2_EN = 1

  代码注释说明 4 的含义是：

  NSU_TRG2 falling edge

  所以实际路由是：

  NSU_TRG2 falling edge
          |
          v
  ACQCMN Timer1 Event0

  这里不需要 GPIO 参与，也不需要把某个 GPIO 物理连接到另一个 GPIO。它是 ACQCMN 内部硬件信号路由。

  ———

  # 六、第三步：Timer1/CAPCOM1 根据 Event0 生成行时序

  同一个函数先配置 CAPCOM1：

  CCUH_CapComConfigure(
      1UL,
      CCUH_CAPCOM_PRE_SCALAR,
      lineTimeUs,
      1UL);

  默认：

  #define CCUH_CAPCOM_PRE_SCALAR (100UL)

  默认应用参数：

  aTrigInterval = 5 us
  aNumSamples   = 2000
  aLineGap      = 1000 us

  计算：

  activeLineTimeUs = aTrigInterval * aNumSamples;
  lineTimeUs       = activeLineTimeUs + aLineGap;

  结果：

  activeLineTimeUs = 5 × 2000
                   = 10000 µs
                   = 10 ms

  lineTimeUs = 10000 + 1000
             = 11000 µs
             = 11 ms

  CAPCOM1 的时钟配置是：

  100 MHz / 100 = 1 MHz

  所以：

  1 CAPCOM tick = 1 µs

  自动重装周期：

  ARR = lineTimeUs - 1
      = 11000 - 1
      = 10999

  因此 CAPCOM1 的行周期约为：

  11000 × 1 µs = 11 ms

  ———

  ## CAPCOM1 的 compare 窗口

  代码：

  uint32_t startTime = 1UL;
  uint32_t endTime = startTime + activeLineTimeUs;

  默认值：

  startTime = 1
  endTime   = 1 + 10000 = 10001

  然后：

  CCUH_CapComCompareConfigure(
      1UL,
      startTime,
      endTime,
      1UL,
      0UL);

  也就是：

  CCR0 A compare = 1 µs
  CCR0 B compare = 10001 µs
  A_EN_XOR       = 1
  B_EN_XOR       = 0

  代码设计意图是让 TMR1_COUT0 在每行中的有效采集窗口内处于 active：

  行周期：0 ~ 11000 µs

  0       1                            10001       11000
  |-------|==============================|-----------|
          <------ active window -------> <--- gap --->

  有效窗口长度：

  10001 - 1 = 10000 µs = 10 ms

  行间 gap：

  11000 - 10001 = 999 µs

  从概念上按配置值看是约 1 ms；由于 compare/counter 使用减一编码，实际边界可能存在一个计数 tick 的偏差。

  CAPCOM1 的 COUT0 输出被使能：

  CCUH_CapComOutputEnable(1UL, 0UL, 0UL, 0UL);

  该函数设置：

  CAPCOM1.OUT_CTRL bit0 = 1
  CAPCOM1.CTRLA.CCT_EN = 1

  所以：

  CAPCOM1 channel 0 / sub-channel A
          |
          v
  TMR1_COUT0

  ———

  # 七、第四步：ACQCMN 将 TMR1_COUT0 作为 acq_trg 源

  代码：

  regVal = ACQCMN_REG->acq_trg_src_ctrl0 &
           ~ACQCMN_CAPCOM_CMN_SRC_CTRL_TMR1_COUT0_MASK;

  ACQCMN_REG->acq_trg_src_ctrl0 =
      regVal |
      (0UL << ACQCMN_CAPCOM_CMN_SRC_CTRL_TMR1_COUT0_SHIFT);

  然后：

  ACQCMN_REG->acq_trg_src_sel |=
      ACQCMN_ACQ_TRG_SRC_SEL_TMR1_COUT0_EN_MASK;

  相关字段：

  #define ACQCMN_CAPCOM_CMN_SRC_CTRL_TMR1_COUT0_MASK (0xf0000UL)
  #define ACQCMN_CAPCOM_CMN_SRC_CTRL_TMR1_COUT0_SHIFT (16UL)

  #define ACQCMN_ACQ_TRG_SRC_SEL_TMR1_COUT0_EN_MASK (0x10UL)
  #define ACQCMN_ACQ_TRG_SRC_SEL_TMR1_COUT0_EN_SHIFT (4UL)

  当前含义：

  TMR1_COUT0 的 source select 编码 = 0
  TMR1_COUT0 路由使能 = 1

  代码注释明确说明：

  Select Timer1 COUT0 as the source for the main acquisition trigger (acq_trg).

  因此内部连接为：

  TMR1_COUT0
      |
      v
  ACQCMN acq_trg source mux
      |
      v
  acq_trg trigger counter

  再次强调，这里走的是 ACQCMN 内部连线，不是：

  TMR1_COUT0 -> GPIO61 -> 外部导线 -> GPIO输入

  GPIO61 只是可选的观测输出。

  ———

  # 八、第五步：ACQCMN 的 acquisition trigger counter 产生真正的周期触发

  代码：

  uint32_t acqTrigInterCycles =
      (aTrigInterval * 10000UL + 16UL) / 32UL;

  注释说明：

  acquisition trigger interval 使用 3.2 ns cycle

  换算关系：

  1 µs = 1000 ns
  1 counter tick = 3.2 ns

  counter ticks
  = aTrigInterval × 1000 / 3.2
  = aTrigInterval × 10000 / 32

  加 16 是为了四舍五入。

  ———

  ## 默认 5 µs 的计算

  aTrigInterval = 5 µs

  因此：

  acqTrigInterCycles
  = (5 × 10000 + 16) / 32
  = 50016 / 32
  = 1563

  然后写入：

  PROG_CNT = acqTrigInterCycles - 1
           = 1562

  代码：

  ACQCMN_REG->acq_trg_cntr_ctrl =
      regVal |
      ((acqTrigInterCycles - 1UL) <<
       ACQCMN_ACQ_TRG_CNTR_CTRL_PROG_CNT_SHIFT);

  最后使能：

  ACQCMN_REG->acq_trg_cntr_ctrl |=
      ACQCMN_ACQ_TRG_CNTR_CTRL_ACQ_TRG_CNTR_EN_MASK;

  寄存器定义：

  #define ACQCMN_ACQ_TRG_CNTR_CTRL_PROG_CNT_MASK (0xffffUL)
  #define ACQCMN_ACQ_TRG_CNTR_CTRL_ACQ_TRG_CNTR_EN_MASK (0x10000UL)

  所以当前默认结果是：

  PROG_CNT = 1562
  实际周期计数 = 1562 + 1 = 1563 ticks

  实际时间：

  1563 × 3.2 ns
  = 5001.6 ns
  = 5.0016 µs

  因此：

  目标周期：5.0000 µs
  实际周期：5.0016 µs
  误差：+1.6 ns

  实际频率约为：

  1 / 5.0016 µs ≈ 199.936 kHz

  目标频率：

  1 / 5 µs = 200 kHz

  ———

  # 九、所以真正的采集触发周期是谁决定的

  当前系统有三种不同的时间概念，不能混淆。

  ## 1. NSU channel 2 延迟

  当前时间 + 500 µs

  由：

  CCUH_StartNsuTrigger(2UL, 500000UL)

  决定。

  它是启动延迟，不是采样周期。

  ## 2. 行周期

  11 ms

  由：

  lineTimeUs = aTrigInterval * aNumSamples + aLineGap

  以及 CAPCOM1 的 ARR 决定。

  默认：

  5 µs × 2000 + 1000 µs = 11 ms

  ## 3. 单次 acquisition trigger 周期

  约 5.0016 µs

  由：

  ACQCMN_REG->acq_trg_cntr_ctrl.PROG_CNT

  决定。

  这才是每次 TXSLZR、HSADC、HSREF acquisition event 之间的周期。

  可以总结为：

   时间参数                             当前值    作用
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   NSU channel 2 delay                  500 µs    系统初始化完成后，延迟多久开始第一行时序
  ──────────────────────────────  ─────────────  ──────────────────────────────────────────
   acquisition trigger interval    5 µs 目标值    两次采集之间的间隔
  ──────────────────────────────  ─────────────  ──────────────────────────────────────────
   实际 ACQ counter period           5.0016 µs    3.2 ns counter 量化后的真实周期
  ──────────────────────────────  ─────────────  ──────────────────────────────────────────
   active line time                      10 ms    一行中允许产生采集触发的有效时间
  ──────────────────────────────  ─────────────  ──────────────────────────────────────────
   line period                           11 ms    整行重复周期
  ──────────────────────────────  ─────────────  ──────────────────────────────────────────
   line gap                            约 1 ms    两行之间的空闲时间

  ———

  # 十、ACQCMN 到底是什么

  ACQCMN 是：

  Acquisition Common

  即：

  采集公共模块

  在代码中使用的寄存器类型是：

  ACQCMN_RDBType

  寄存器基地址：

  #define ACQ_COMMON_BASE ((uintptr_t)(0xE0600000UL))

  应用层通过：

  #define ACQCMN_REG \
      ((volatile ACQCMN_RDBType *)ACQ_COMMON_BASE)

  访问它。

  底层驱动名称也是：

  ACQCMN driver

  初始化位置：

  ACQCMN_DrvInit(hsacq_cmn_id);

  见 app/src/bcm/BCM8915X_BareMetal_helper.c:107。

  ———

  ## ACQCMN 的主要职责

  ACQCMN 不是某一路 ADC，而是采集子系统的公共控制与互联模块，主要负责：

  - 连接 NSU event；
  - 连接 CAPCOM/CCU Timer0、Timer1；
  - 选择 acquisition trigger source；
  - 产生 acquisition trigger counter；
  - 向 HSADC、HSREF、TXSLZR 等模块分发触发；
  - 配置触发输出 delay、pulse width、polarity；
  - 提供 acquisition status 输出；
  - 管理部分采集同步和轴/位置相关功能。

  可以把它看成：

  采集系统的触发交换机 + 定时器接口 + 触发分频器

  当前使用到的 ACQCMN 功能包括：

  NSU_TRG2
      ->
  Timer1 Event0

  Timer1 COUT0
      ->
  acq_trg source

  acq_trg counter
      ->
  周期性 acquisition trigger

  acq trigger outputs
      ->
  TXSLZR / HSADC / HSREF

  ———

  # 十一、ACQCMN 不等于 HSADC

  这几个模块的层次不同：

  NSU
      = 提供全局纳秒时间基准和事件生成

  CAPCOM / CCU
      = 提供行周期、compare window、COUT 输出

  ACQCMN
      = 连接和调度采集触发

  HSADC
      = 执行 ADC 采样并保存采样数据

  TXSLZR
      = 响应 TX trigger，输出 ToF 发射 pattern

  HSREF
      = 响应参考触发，执行参考采样/同步功能

  完整关系：

  NSU：什么时候启动一行
      |
      v
  CAPCOM：一行持续多久、有效窗口多长
      |
      v
  ACQCMN：在有效窗口内多久发一次 acquisition trigger
      |
      +--> TXSLZR：输出发射脉冲
      +--> HSADC：采集 ADC 数据
      +--> HSREF：采集参考信号

  ———

  # 十二、当前代码下的完整时间线

  以系统初始化完成的时刻记为 T0。

  T0
  |
  | 配置完成
  |
  +---- 500 µs ----> NSU channel 2 产生 NSU_TRG2
                           |
                           v
                    Timer1 Event0
                           |
                           v
                    CAPCOM1 行时序同步
                           |
                           +---- 约 1 µs ----> active window 开始
                           |
                           +---- 每约 5.0016 µs ----> acq_trg
                           |                            |
                           |                            +--> TXSLZR
                           |                            +--> HSADC
                           |                            +--> HSREF
                           |
                           +---- 约 10 ms ----> active window 结束
                           |
                           +---- 约 1 ms ----> line gap
                           |
                           v
                      下一行，周期约 11 ms

  如果只看 NSU channel 2：

  T0 + 500 µs：产生一次事件

  如果看每次 ADC 采集：

  T0 + 500 µs + 若干硬件延迟
  之后每约 5.0016 µs 一次

  如果看每行：

  每约 11 ms 开始一行

  ———

  # 十三、关于“NSU event channel 2 是否会重复”的准确表述

  根据当前代码，最稳妥的结论是：

  当前软件只给 channel 2 写入一个比较时间点，
  没有配置软件重装或周期值，
  因此不能把它视为 500 µs 周期源。

  在实际硬件中，channel enable 是否在事件命中后自动清除、以及比较命中后是否可能保持事件状态，需要以 NSU TRM 的 event generator 行为定义为准。

  但是无论硬件在命中后如何维持 enable 状态，当前代码都没有更新下一个 compare time，因此：

  当前设计不能依赖 NSU channel 2 持续地产生 5 µs 周期事件。

  5 µs 周期明确是由：

  ACQCMN acq_trg counter

  负责。

  如果以后希望 NSU channel 2 自己周期触发，需要额外设计，例如：

  - 使用 NSU system tick；
  - 每次事件后软件重新写入下一个 compare time；
  - 使用另一个具有周期/重载功能的定时器；
  - 让 NSU 只负责行级同步，内部 ACQCMN 负责采样级周期。

  当前代码选择的是最后一种架构：

  NSU：一次性行启动同步
  ACQCMN：采集周期产生
  CAPCOM：行窗口和行周期管理

  ———

  ## 最终总结

  当前工程中的触发层次是：

  NSU channel 2
      = 一次性启动事件，约在 500 µs 后产生

  ACQCMN Timer1 Event0
      = 接收 NSU_TRG2 falling edge

  CAPCOM1
      = 建立约 11 ms 的行周期和约 10 ms 的 active window

  TMR1_COUT0
      = 被 ACQCMN 选为主 acq_trg source

  ACQCMN acq trigger counter
      = 将触发节拍变成约 5.0016 µs 一次

  TXSLZR / HSADC / HSREF
      = 响应 acq_trg，并叠加各自的输出 delay

  所以对你的问题最直接的回答是：

  NSU event channel2 当前没有 5 µs 或 11 ms 的周期。
  它只在启动后约 500 µs 产生一次同步事件。
  真正的 5 µs 采集触发周期由 ACQCMN 的 acq_trg counter 产生；
  真正的 11 ms 行周期由 CAPCOM1 产生。