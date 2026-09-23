• 这次输出进一步确认：板端运行的 MCU1 固件不是刚才编译出来的带 trace 的版本，或者 MCU1 没有被重新启动，仍然在运行旧固件。

  依据是：

  received IPC log doorbell test command 0x6c6f6700

  这三个是旧代码中的测试 doorbell。新代码进入 LogIpc_Init() 后，至少应该先出现：

  received MCU1 log trace 0x6c670100

  但是当前完全没有任何 0x6c670xxx，所以还没有进入新版本的 LogIpc_Init() trace 路径。

  为什么 /tmp/log/mcu1.log 仍为空

  当前 MCU1 的启动过程很可能是：

  1. 旧版本 MCU1 发送第一个测试 doorbell。
  2. elog_init() 调用 elog_port_init()。
  3. log IPC 初始化失败或旧固件卡住。
  4. 旧代码继续发送另外两个测试 doorbell。
  5. MCU1 没有写入有效的 Channel 6 control block。
  6. A55 始终看到：

  magic=0x00000000

  因此 A55 不会读取 MCU1 ring，/tmp/log/mcu1.log 为空是必然结果。

  必须注意 MCU1 不一定会因为 Ctrl-C 自动停止

  你执行：

  ^C

  只停止了 A55 端的 bcm8915x_ipc_log。MCU1 可能仍然在运行。下一次启动 A55 时，如果 MCU0 的 loader 没有真正复位 MCU1，板上仍可能是旧 MCU1 固件，所以会继续看到旧的 0x6c6f6700。

  建议按下面顺序重新验证。

  1. 先确认构建机上的文件：

  cd /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1

  sha256sum \
    BCM8915X_MCU_OS/build/out/mcu1/bin/bcm8915x_mcu1.bin \
    bcm8915x_app/build/obj/bcm8915x_ipc_log

  当前这两个新文件是：

  BCM8915X_MCU_OS/build/out/mcu1/bin/bcm8915x_mcu1.bin
  bcm8915x_app/build/obj/bcm8915x_ipc_log

  2. 重新复制到板端，确保复制的是上述文件，而不是之前某个目录下的旧文件：

  scp -P 2200 \
    BCM8915X_MCU_OS/build/out/mcu1/bin/bcm8915x_mcu1.bin \
    hjk@192.168.10.23:/data/JK_test/bcm8915x_mcu1.bin

  scp -P 2200 \
    bcm8915x_app/build/obj/bcm8915x_ipc_log \
    hjk@192.168.10.23:/data/JK_test/bcm8915x_ipc_log

  3. 板端确认文件 hash：

  cd /data/JK_test

  sha256sum bcm8915x_mcu1.bin bcm8915x_ipc_log

  板端 bcm8915x_mcu1.bin 的 hash 必须和构建机对应文件完全一致。

  4. 你的配置中：

  Image_File:
    MCU1: bcm8915x_mcu1.bin

  这是相对路径。processor.c 直接执行：

  load_image(config->mcu1_image, ...);

  它不会自动把路径转换成 config.yaml 所在目录。因此必须保证启动程序时的当前目录就是 /data/JK_test：

  cd /data/JK_test
  ./bcm8915x_ipc_log

  建议临时把配置改成绝对路径，避免实际加载了其他目录里的旧文件：

  Image_File:
    MCU1: /data/JK_test/bcm8915x_mcu1.bin
    Q8_Core0: /data/JK_test/lidar_q8_core0.bin
    Q8_Core1: /data/JK_test/lidar_q8_core1.bin
    Q8_Core2: /data/JK_test/lidar_q8_core2.bin
    Q8_Core3: /data/JK_test/lidar_q8_core3.bin

  5. 最重要的是让 MCU1 真正复位后再启动。

  优先采用整板重启，确保 MCU1 不再执行旧代码。重启后再运行：

  cd /data/JK_test
  rm -f /tmp/log/mcu1.log
  ./bcm8915x_ipc_log

  正常情况下，新的输出应该包含类似：

  received MCU1 log trace 0x6c670100
  received MCU1 log trace 0x6c670104
  received MCU1 log trace 0x6c67010c
  received MCU1 log trace 0x6c670118
  received MCU1 log trace 0x6c670130
  received MCU1 log trace 0x6c670134
  received MCU1 log trace 0x6c670140

  其中：

  0x6c670100 进入 LogIpc_Init()
  0x6c670104 IPCH_GetMsgPtr() 成功
  0x6c67010c IPCH_GetMsgSize() 成功
  0x6c670118 读取到 A55 发布的五个 log buffer 配置
  0x6c670130 control block 初始化完成
  0x6c670134 MCU1 ring 初始化完成
  0x6c670140 LogIpc_Init() 成功

  如果重启并确认 hash 一致后仍然只有：

  0x6c6f6700

  那就说明 A55 loader 实际没有启动新 bin，需要继续检查 MCU0 的 LOADER_INIT_PROCESSOR 启动流程，尤其是 MCU1 已运行状态下重复发送启动命令是否会被忽略。根据当前现象，现在首先要解决的是固件部署/MCU1 重启问题，还没有进入 log IPC 地址和 ring 内容的实际验证阶段。


# Q8 dma

• 可以，但需要先澄清：当前代码并不是“每个 Q8 只使用一个 IDMA channel”。

  在 FFT 模式下，当前实现已经使用了 4 个 IDMA channel 做 ADC/FFT 缓冲搬运：

  idma_schedule_desc_fast(IDMA_CHANNEL_0, NUM_DESCS_FFT);
  idma_schedule_desc_fast(IDMA_CHANNEL_1, NUM_DESCS_FFT);
  idma_schedule_desc_fast(IDMA_CHANNEL_2, NUM_DESCS_FFT);
  idma_schedule_desc_fast(IDMA_CHANNEL_3, NUM_DESCS_FFT);

  对应关系是：

   IDMA channel    ADC/FFT 通道     内部 DRAM 区域
  ━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━
   channel 0       FFT channel 0    dram0_ping + 0x0000
  ──────────────  ───────────────  ─────────────────────
   channel 1       FFT channel 1    dram0_ping + 0x2000
  ──────────────  ───────────────  ─────────────────────
   channel 2       FFT channel 2    dram0_ping + 0x4000
  ──────────────  ───────────────  ─────────────────────
   channel 3       FFT channel 3    dram0_ping + 0x6000

  每个 ADC 通道搬运 8192 字节，也就是：

  4096 个 ADC 实数采样
      ↓
  4096 点 FFT
      ↓
  2048 个复数频点
      ↓
  8192 字节

  四个 channel 合起来组成一个 Q8 的 0x8000 字节输出 buffer。

  目前只使用 IDMA channel 0 的是第二阶段：

  内部 DRAM 0x8000 字节
      ↓
  A55/MCU1 共享 IPC buffer 0x8000 字节

  当前代码为：

  idma_schedule_desc_fast(IDMA_CHANNEL_0, NUM_DESCS_ACQ_SRAM);

  它一次性搬运整个 Q8 的 0x8000 字节：

  #define XFER_SIZE          8192
  #define DRAM_TO_SRAM_SIZE  (4 * XFER_SIZE)  /* 0x8000 */

  所以实际流程是：

  阶段一：四个 IDMA channel 并行

  IDMA0: FFT ADC0 → DRAM[0x0000, 0x2000)
  IDMA1: FFT ADC1 → DRAM[0x2000, 0x4000)
  IDMA2: FFT ADC2 → DRAM[0x4000, 0x6000)
  IDMA3: FFT ADC3 → DRAM[0x6000, 0x8000)

  阶段二：当前只使用 IDMA0

  IDMA0: DRAM[0x0000, 0x8000) → IPC buffer[0x0000, 0x8000)

  这样设计的原因是，第二阶段的数据在 DRAM 中已经是连续的 0x8000 字节，一次搬运比拆成四次更简单，也避免了重新初始化 IDMA channel 1~3 时遇到任务状态问题。此前出现过：

  status=-9
  IDMA_ERR_TASK_IN_ERROR

  这说明某些 IDMA channel 在完成第一阶段 ping/pong descriptor 后，被重新配置或重新调度时状态机没有回到可接受状态。此时如果简单把：

  idma_schedule_desc_fast(IDMA_CHANNEL_0, NUM_DESCS_ACQ_SRAM);

  改成四个 channel 并行调度，可能再次触发错误。

  如果确实希望第二阶段也使用四个 IDMA channel，可以实现，但应采用独立的输出 descriptor，而不是直接复用 FFT descriptor。安全的结构应是：

  IDMA0:
      DRAM + 0x0000 → IPC buffer + 0x0000，长度 0x2000

  IDMA1:
      DRAM + 0x2000 → IPC buffer + 0x2000，长度 0x2000

  IDMA2:
      DRAM + 0x4000 → IPC buffer + 0x4000，长度 0x2000

  IDMA3:
      DRAM + 0x6000 → IPC buffer + 0x6000，长度 0x2000

  需要新增四组独立 descriptor，例如：

  IDMA_BUFFER_DEFINE(out_desc_buffer_0, 1, IDMA_1D_DESC);
  IDMA_BUFFER_DEFINE(out_desc_buffer_1, 1, IDMA_1D_DESC);
  IDMA_BUFFER_DEFINE(out_desc_buffer_2, 1, IDMA_1D_DESC);
  IDMA_BUFFER_DEFINE(out_desc_buffer_3, 1, IDMA_1D_DESC);

  然后分别建立：

  idma_init_loop(IDMA_CHANNEL_0, out_desc_buffer_0,
                 IDMA_1D_DESC, 1, NULL, NULL);
  idma_init_loop(IDMA_CHANNEL_1, out_desc_buffer_1,
                 IDMA_1D_DESC, 1, NULL, NULL);
  idma_init_loop(IDMA_CHANNEL_2, out_desc_buffer_2,
                 IDMA_1D_DESC, 1, NULL, NULL);
  idma_init_loop(IDMA_CHANNEL_3, out_desc_buffer_3,
                 IDMA_1D_DESC, 1, NULL, NULL);

  并添加四个 descriptor：

  idma_add_desc(out_desc_buffer_0,
                (uint8_t *)output_sram + 0x0000,
                (uint8_t *)dram_src + 0x0000,
                0x2000, 0);

  idma_add_desc(out_desc_buffer_1,
                (uint8_t *)output_sram + 0x2000,
                (uint8_t *)dram_src + 0x2000,
                0x2000, 0);

  idma_add_desc(out_desc_buffer_2,
                (uint8_t *)output_sram + 0x4000,
                (uint8_t *)dram_src + 0x4000,
                0x2000, 0);

  idma_add_desc(out_desc_buffer_3,
                (uint8_t *)output_sram + 0x6000,
                (uint8_t *)dram_src + 0x6000,
                0x2000, 0);

  然后同时调度：

  idma_schedule_desc_fast(IDMA_CHANNEL_0, 1);
  idma_schedule_desc_fast(IDMA_CHANNEL_1, 1);
  idma_schedule_desc_fast(IDMA_CHANNEL_2, 1);
  idma_schedule_desc_fast(IDMA_CHANNEL_3, 1);

  最后必须等待四个 channel 都完成：

  while (idma_buffer_status(IDMA_CHANNEL_0) > 0 ||
         idma_buffer_status(IDMA_CHANNEL_1) > 0 ||
         idma_buffer_status(IDMA_CHANNEL_2) > 0 ||
         idma_buffer_status(IDMA_CHANNEL_3) > 0) {
  }

  但这里有几个重要限制：

  1. 四个 channel 的目标地址必须分别是 IPC buffer 的四个 0x2000 分区，不能让四个 channel 写同一个地址。
  2. 四个 output descriptor 最好与 FFT/ADC descriptor 分开，不能简单复用 fft_desc_buffer_0~3。
  3. 输出搬运完成之前不能发送 MCU1 doorbell。
  4. MCU1 收到 doorbell 后，仍然按照一个 Q8 的完整 0x8000 buffer 处理，不需要修改 IPC 协议。
  5. A55 的 buffer 布局也不需要改变，仍然是每个 Q8 一个 0x8000 buffer。
  6. 四通道并行搬运不一定更快。当前数据已经连续，channel 0 一次搬 0x8000 在带宽上通常更高效；四通道方案的主要价值是让四个物理搬运通道都参与工作，而不是改变数据格式。

  因此当前建议是：

  - FFT 采集阶段：继续使用 IDMA channel 0~3，这部分已经正确；
  - DRAM 到 IPC 阶段：优先保留 channel 0 一次性搬 0x8000；
  - 如果为了硬件利用率或验证目的必须四通道搬运，则增加独立 output descriptor，并分别搬运四个 0x2000 分区；
  - 不要只把一个 schedule 调用复制成四个调用，否则很容易再次出现 IDMA_ERR_TASK_IN_ERROR。

  当前代码位置是：

  - FFT 四通道调度：LidarQ8App_0_1/lidar_q8_app_raw.c:295
  - DRAM 到 IPC 的单 channel 搬运：LidarQ8App_0_1/lidar_q8_app_raw.c:359
  - 当前四个 FFT descriptor 配置：LidarQ8App_0_1/lidar_q8_app_raw.c:140

  所以答案是：能用四个，但当前已经在第一阶段使用了四个；第二阶段是否改成四个，需要额外增加独立的输出 descriptor 和状态管理，不能简单替换一行调度代码。