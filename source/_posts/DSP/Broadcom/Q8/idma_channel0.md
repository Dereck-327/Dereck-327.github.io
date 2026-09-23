当前 LidarQ8App_0_1/lidar_q8_app_raw.c 的 Q8 处理流程，可以概括为：

  读取 A55 发布的共享配置
          ↓
  根据 Q8_CORE_ID 选择本 Q8 对应的 ADC/FFT 地址组
          ↓
  初始化本 Q8 的 iDMA channel 0
          ↓
  建立 FFT/ADC → 内部 DRAM 的 ping/pong descriptor
          ↓
  循环搬运多个采集块
          ↓
  内部 DRAM → A55 共享 SRAM buffer
          ↓
  等待搬运完成
          ↓
  通过 IPC channel 4 发送 Q8 buffer done Doorbell
          ↓
  等待 MCU1 应答
          ↓
  切换 ping/pong，继续下一批数据

  下面按照代码实际执行顺序说明。

  ———

  ## 1. 每个 Q8 image 是独立编译、独立运行的

  Q8 工程通过编译参数定义：

  -DQ8_CORE_ID=0
  -DQ8_CORE_ID=1
  -DQ8_CORE_ID=2
  -DQ8_CORE_ID=3

  对应生成四个独立的 Q8 image：

  q8_core_0.bin
  q8_core_1.bin
  q8_core_2.bin
  q8_core_3.bin

  代码中：

  #ifndef Q8_CORE_ID
  #define Q8_CORE_ID 0UL
  #endif

  因此，每个 Q8 image 内部的 Q8_CORE_ID 是固定常量。

  四个 Q8 并不是在同一个程序中分别使用 iDMA channel 0、1、2、3。实际情况是：

  Q8 core 0 程序：使用它自己的 IDMA channel 0
  Q8 core 1 程序：使用它自己的 IDMA channel 0
  Q8 core 2 程序：使用它自己的 IDMA channel 0
  Q8 core 3 程序：使用它自己的 IDMA channel 0

  这里的 IDMA_CHANNEL_0 是“当前 Q8 DSP 核内部的 iDMA channel 0”，不是四个 Q8 全局共享的一个 channel。

  因此四个 Q8 同时运行时，实际硬件关系是：

  Q8_0 的 IDMA channel 0
  Q8_1 的 IDMA channel 0
  Q8_2 的 IDMA channel 0
  Q8_3 的 IDMA channel 0

  它们分别属于不同的 Q8 DSP core，互不冲突。

  ———

  ## 2. 为什么只使用 IDMA_CHANNEL_0

  代码顶部虽然定义了：

  #define IDMA_CHANNEL_0 0
  #define IDMA_CHANNEL_1 1
  #define IDMA_CHANNEL_2 2
  #define IDMA_CHANNEL_3 3

  但当前工作模式只实际使用：

  IDMA_CHANNEL_0

  原因是当前设计中，一个 Q8 core 只处理一路 ADC/FFT 数据：

  #define NUM_CHANNELS_ACQ 1
  #define NUM_CHANNELS_FFT 1

  每个 Q8 core 的职责是：

  Q8_0 -> ADC/FFT group 0 -> Buffer0
  Q8_1 -> ADC/FFT group 1 -> Buffer1
  Q8_2 -> ADC/FFT group 2 -> Buffer2
  Q8_3 -> ADC/FFT group 3 -> Buffer3

  因此，一个 Q8 core 内部只需要一条 iDMA 数据搬运通道：

  当前 Q8 对应的 FFT 输出区
          ↓
  内部 DRAM ping/pong
          ↓
  A55 共享输出 buffer

  没有必要在同一个 Q8 core 内再启用 channel 1、2、3。

  之前官方代码中的多 channel 版本，是另一种架构：

  一个 Q8 core 内部处理四路 ADC
      channel 0 -> ADC0
      channel 1 -> ADC1
      channel 2 -> ADC2
      channel 3 -> ADC3

  而当前代码已经改为：

  四个 Q8 core 分摊四路 ADC
      Q8_0 -> ADC0
      Q8_1 -> ADC1
      Q8_2 -> ADC2
      Q8_3 -> ADC3

  所以不能同时把“一个 Q8 内部四个 iDMA channel 的官方版本”和“现在四个 Q8 分工版本”混在一起。

  当前代码中的这些对象：

  IDMA_BUFFER_DEFINE(fft_desc_buffer_1, ...);
  IDMA_BUFFER_DEFINE(fft_desc_buffer_2, ...);
  IDMA_BUFFER_DEFINE(fft_desc_buffer_3, ...);

  是为了保留官方代码结构和后续扩展空间，但当前单 Q8 单 stream 方案中没有实际使用。真正使用的是：

  fft_desc_buffer_0

  以及：

  acq_desc_buffer

  ———

  ## 3. Q8 如何选择自己的 ADC/FFT 地址

  代码定义了 ADC group 的步长：

  #define ADC_GROUP_STRIDE 0x00040000UL
  #define ADC_GROUP_BASE   (0x00600000UL + (Q8_CORE_ID * ADC_GROUP_STRIDE))

  因此四个 Q8 的地址基准如下：

   Q8 core    Q8_CORE_ID    ADC group base
  ━━━━━━━━━  ━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━
   Q8_0                0        0x00600000
  ─────────  ────────────  ────────────────
   Q8_1                1        0x00640000
  ─────────  ────────────  ────────────────
   Q8_2                2        0x00680000
  ─────────  ────────────  ────────────────
   Q8_3                3        0x006C0000

  FFT 读取区地址由：

  #define FFT_RD_BUF_OFFSET_0 0x00030000UL

  计算：

  fft_marker_0_0 =
      (volatile int16_t *)(ADC_GROUP_BASE + FFT_RD_BUF_OFFSET_0);

  所以各 Q8 实际读取地址分别是：

  Q8_0: 0x00630000
  Q8_1: 0x00670000
  Q8_2: 0x006B0000
  Q8_3: 0x006F0000

  当前日志中 Q8_0 打印：

  fft_buf=0x00630000

  这就是 Q8_0 对应的 FFT 输出区。

  注意，fft_marker_0_1、fft_marker_0_2、fft_marker_0_3 仍然定义在代码中：

  volatile int16_t *fft_marker_0_1;
  volatile int16_t *fft_marker_0_2;
  volatile int16_t *fft_marker_0_3;

  但在当前架构中它们没有被使用，因为它们代表同一个 ADC group 内的其他 FFT 通道，而不是另外三个 Q8 core。

  ———

  ## 4. 当前 4K FFT 的数据大小

  当前 A55 配置：

  ADCSampleSize: 4096

  含义是：

  每路 ADC 输入 4096 个实数采样
  4096 点 FFT
  IPC/UDP 传输 2048 个复数频点

  每个复数由两个 int16 组成：

  real      : 2 bytes
  imaginary : 2 bytes

  所以每个复数占 4 字节：

  #define XFER_SIZE (2 * 2 * 2048)

  结果为：

  2048 × 4 = 8192 bytes

  这就是 Q8 每次从硬件 FFT 区搬运的数据量。

  代码中的：

  #define XFER_SIZE (2*2*2048)

  可以拆成：

  2：real/imag 两个分量
  2：每个分量为 int16，占 2 字节
  2048：IPC 传输的复数频点数量

  ———

  ## 5. Q8 启动阶段

  main() 首先清零性能计数器：

  *q8_0_dbg_pm0 = 0UL;
  ...
  *q8_0_dbg_pm6 = 0UL;

  然后设置软件 magic：

  *q8_0_dbg_pm0 = SW_MAGIC_ID;

  清空两个内部 DRAM 缓冲区：

  memset((void*)dram0_ping, 0, sizeof(dram0_ping));
  memset((void*)dram0_pong, 0, sizeof(dram0_pong));

  内部 DRAM 定义为：

  ALIGN_16 SECTION_DRAM0_PING char dram0_ping[1024 * 64];
  ALIGN_16 SECTION_DRAM0_PONG char dram0_pong[1024 * 64];

  也就是：

  dram0_ping：内部 DRAM ping 缓冲区
  dram0_pong：内部 DRAM pong 缓冲区

  之后根据 A55 共享配置判断是否开启 FFT：

  if ((LIDAR_MODE_FFT == pSharedData->LiDARMode) ||
      (LIDAR_MODE_FMCW == pSharedData->LiDARMode)) {
      fft_en = 1UL;
  }

  然后初始化 Q8 IPC 日志，读取当前 Q8 对应的输出 buffer：

  uint32_t output_addr[4] = {
      pSharedData->Buffer0Address,
      pSharedData->Buffer1Address,
      pSharedData->Buffer2Address,
      pSharedData->Buffer3Address,
  };

  根据 Q8_CORE_ID 选择：

  output_sram = (volatile int16_t *)output_addr[Q8_CORE_ID];

  例如：

  Q8_0 -> Buffer0 = 0x00094040
  Q8_1 -> Buffer1 = 0x0009C040
  Q8_2 -> Buffer2 = 0x000A4040
  Q8_3 -> Buffer3 = 0x000AC040

  每个 Q8 只有一个稳定的 A55 输出 buffer。

  ———

  ## 6. iDMA 初始化

  初始化函数：

  static void initIDMA()
  {
      idma_init(IDMA_CHANNEL_0,
                0,
                MAX_BLOCK_16,
                16,
                0,
                0,
                NULL);
  }

  这里初始化的是当前 Q8 core 的 iDMA channel 0。

  随后注册完成和错误中断处理函数：

  idma_register_interrupts(
      IDMA_CHANNEL_0,
      (void*)idma_task_done_intr_handler,
      (void*)idma_task_err_intr_handler);

  当前两个回调为空：

  void idma_task_err_intr_handler(int ch) {}
  void idma_task_done_intr_handler(int ch) {}

  因为当前程序不是通过中断回调推进状态，而是主动轮询：

  idma_buffer_status(IDMA_CHANNEL_0)

  所以真正的数据流程由主循环中的状态轮询驱动。

  ———

  ## 7. FFT descriptor 的建立

  FFT 模式下执行：

  setup_adc_to_dram_desc();

  函数内部：

  idma_init_loop(
      IDMA_CHANNEL_0,
      fft_desc_buffer_0,
      IDMA_1D_DESC,
      NUM_DESCS_FFT,
      NULL,
      NULL);

  当前：

  #define NUM_DESCS_FFT 2

  然后建立两个 descriptor：

  idma_add_desc(
      fft_desc_buffer_0,
      (uint8_t *)dram0_ping,
      fft_marker_0_0,
      XFER_SIZE,
      0);

  idma_add_desc(
      fft_desc_buffer_0,
      (uint8_t *)dram0_pong,
      fft_marker_0_0,
      XFER_SIZE,
      0);

  因此 descriptor 链表为：

  descriptor 0:
      source = 硬件 FFT buffer
      destination = dram0_ping
      size = 8192 bytes

  descriptor 1:
      source = 硬件 FFT buffer
      destination = dram0_pong
      size = 8192 bytes

  在下一批处理开始时，descriptor loop 会重新初始化。

  ———

  ## 8. 为什么 FFT 也需要 ping/pong

  这里的 ping/pong 有两个作用。

  ### 8.1 满足官方 iDMA loop 的工作方式

  当前版本使用：

  #define NUM_DESCS_FFT 2

  官方 FFT 路径也是建立两个 descriptor：

  FFT -> dram0_ping
  FFT -> dram0_pong

  在当前芯片的 iDMA loop 实现中，单 descriptor loop 可能长期处于 busy 状态，导致：

  while (idma_buffer_status(cur_chan) == num_descs) {
  }

  一直不退出。

  之前单 descriptor 版本的日志是：

  status=1 descs=1 chans=1

  之后没有任何 Q8 done。

  恢复两个 descriptor 后，日志变成：

  status=2 descs=2 chans=1

  并能够继续运行。

  因此，两个 descriptor 不只是为了性能，也是当前硬件/iDMA loop 正常推进所需的形式。

  ### 8.2 避免当前批次搬运时覆盖正在使用的数据

  如果只有一个内部 DRAM buffer，流程会变成：

  FFT -> dram
  dram -> A55 output
  下一次 FFT -> 同一个 dram

  这样下一次 FFT 可能在上一次 dram -> A55 output 尚未完成时覆盖内部 DRAM，造成数据竞争。

  使用两个 buffer 后：

  FFT -> dram0_ping
  dram0_ping -> A55 output

  同时或随后：

  FFT -> dram0_pong
  dram0_pong -> A55 output

  当前代码通过 buf_type 选择最终搬运哪个内部 buffer：

  int buf_type = BUF_PING;

  每次完成一批之后：

  buf_type = 1 - buf_type;

  切换关系是：

  第 1 批：使用 dram0_ping
  第 2 批：使用 dram0_pong
  第 3 批：使用 dram0_ping
  第 4 批：使用 dram0_pong
  ...

  ———

  ## 9. 第一阶段：FFT/ADC → 内部 DRAM

  主循环入口：

  while (1) {
      setup_adc_to_dram_desc();

      num_acqs = 0;
      cur_chan = 0;

  FFT 模式下调度两个 descriptor：

  idma_schedule_desc_fast(
      IDMA_CHANNEL_0,
      NUM_DESCS_FFT);

  也就是
