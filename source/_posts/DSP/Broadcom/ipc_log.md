• 结论先说：ipc_memory_pool 中的 0x00080000 是设备树使用的物理/总线地址，但它不是手册中的 0x011D0000，两者不是同一片地址。

  1. ipc_memory_pool@00080000 的地址含义

  在 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/A55_system/u-boot/arch/arm/dts/bcm8915x.dtsi:27 中：

  reserved-memory {
      #address-cells = <2>;
      #size-cells = <2>;
      ranges;

      ipc_memory_pool: ipc_memory_pool@00080000 {
          reg = <0x00 0x00080000 0x0 0x80000>;
      };
  };

  按照 Device Tree 语义：

  起始地址 = 0x0000000000080000
  大小     = 0x00080000 = 512 KiB
  范围     = [0x00080000, 0x00100000)

  这里的 reg 是设备树中的物理/总线资源地址，不是虚拟地址。由于 reserved-memory 下使用了空的 ranges;，这里没有再经过父总线地址转换。

  但是它不等于：

  0x011D0000 - 0x011D7FFF

  当前工程实际存在两套 SRAM 地址视图：

   用途                                                         地址
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   A55、全局地址、Device Tree 视图                        0x00080000
  ─────────────────────────────────  ────────────────────────────────
   MCU1 本地 SRAM 视图                                    0x01080000
  ─────────────────────────────────  ────────────────────────────────
   手册中的 MCU 本地 SRAM IPC 区域           0x011D0000 - 0x011D7FFF
  ─────────────────────────────────  ────────────────────────────────
   上述 SRAM IPC 区域的全局别名       通常为 0x001D0000 - 0x001D7FFF

  工程代码可以证明前两者是别名关系：

  #define IPCH_MEM_POOL_BASE (0x01080000UL)

  见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/LidarRTOSapp_0_1/apps/lidar_rtos_app/src/ipc_helper.c:18。

  同一个文件中又定义了：

  0x01000000 - 0x011FFFFF  // MCU local SRAM
  0x00000000 - 0x001FFFFF  // SRAM global view

  并通过减去 0x01000000 做地址转换，见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/LidarRTOSapp_0_1/apps/lidar_rtos_app/src/ipc_helper.c:161。

  因此：

  MCU local  0x01080000 -> global 0x00080000
  MCU local  0x011D3000 -> global 0x001D3000

  所以不能把 DTS 中的 0x00080000 解释成 0x011D0000。

  当前 BL 配置也印证了这一点：

  IpcMemPoolBase : 0x01080000

  见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/init/bcm8915x/scripts/m7_flash_img.yaml:46。

  2. 当前 IPC pool 的实际布局不是固定的 0x011D 区域

  BL 脚本会根据 YAML 中各个 channel 的 MsgMaxSize 自动计算消息缓冲区偏移，见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/oneui/scripts/m7_flash_img.py:713。

  当前配置大致是：

  Channel 0: MsgMaxSize = 0x10000
  Channel 1: MsgMaxSize = 0x01000
  Channel 2: MsgMaxSize = 0x01000
  Channel 3: MsgMaxSize = 0x22000
  Channel 4: MsgMaxSize = 0
  Channel 5: MsgMaxSize = 0x01000

  按当前 YAML 顺序和 4 KiB 对齐计算：

  pool base                 = 0x00080000 global
  channel config             = pool + 0x00000000
  channel 0 payload          = pool + 0x00001000
  channel 1 payload          = pool + 0x00011000
  channel 2 payload          = pool + 0x00012000
  channel 3 payload          = pool + 0x00013000
  channel 5 payload          = pool + 0x00035000

  Linux 驱动也不是直接假设 payload 地址，而是读取 IPC 配置中的 msg_start_offset，然后计算：

  uio_mem->addr = res.start + chan_info.msg_start_offset;

  见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/A55_system/linux/drivers/mailbox/bcm-rigel-mbox-client.c:119。

  所以：

  ipc_memory_pool = 0x00080000 - 0x000FFFFF

  属于当前 IPC channel pool；它与：

  SRAM IPC = 0x011D0000 - 0x011D7FFF

  是两个不同的区域。

  3. 你规划的 0x011D3000 到 0x011D7FFF 需要特别小心

  你的地址布局本身满足：

  4 KiB 对齐
  每个 buffer 不重叠
  总大小 5 * 4 KiB = 20 KiB

  但是工程中的 BL linker script 已经把该区域定义成：

  SRAM_IPC_SCR:
      0x011D0000 - 0x011D7FFF

  见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/init/bcm8915x/chip/common/firmware.ld:53。

  更关键的是：

  jtag_ipc_scratch_size = 0x4000;

  并且：

  .sram_ipc_scr:
  {
      . = . + jtag_ipc_scratch_size;
  } > SRAM_IPC_SCR

  见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/init/bcm8915x/chip/common/firmware_common.ld:50。

  这意味着 BL/JTAG scratch 至少占用了：

  0x011D0000 - 0x011D3FFF

  因此你提出的：

  MCU1_LOG_ADDR = 0x011D3000

  覆盖了：

  0x011D3000 - 0x011D3FFF

  它与这段 JTAG IPC scratch 发生重叠。

  你规划的 Q8 区域：

  Q8 Core0: 0x011D4000 - 0x011D4FFF
  Q8 Core1: 0x011D5000 - 0x011D5FFF
  Q8 Core2: 0x011D6000 - 0x011D6FFF
  Q8 Core3: 0x011D7000 - 0x011D7FFF

  没有和这 16 KiB JTAG scratch 重叠，但仍然需要确认：

  - 当前启动模式是否使用 PCIe EP IPC；
  - JTAG/flash utility 是否会使用这片区域；
  - MCU1、Q8 linker script 是否会使用这片区域；
  - BL、ROM、工具链是否在运行期间清零或访问这片区域；
  - 是否会被其他 scratch、调试、测试代码使用。

  因此，当前地址方案不能直接批准使用。尤其是 MCU1_LOG_ADDR = 0x011D3000，需要先处理与 JTAG scratch 的冲突。

  另外，当前 MCU1 应用自己的 linker script 只定义了：

  SRAM : ORIGIN = 0x01040000, LENGTH = 256K

  见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/LidarRTOSapp_0_1/apps/lidar_rtos_app/mcu1.ld:15。

  它没有为 0x011Dxxxx 建立 log section，也没有对该区域做链接期保护。所以即使代码手工访问 0x011Dxxxx，也不代表这片内存已经被正式保留。

  4. 可以在 config.ini 中指定这些地址吗？

  从配置格式角度可以，但当前代码中这些配置项不会生效。

  你当前添加的：

  Q8_Core3_LOG_ADDR = 0x011D7000
  Q8_Core2_LOG_ADDR = 0x011D6000
  Q8_Core1_LOG_ADDR = 0x011D5000
  Q8_Core0_LOG_ADDR = 0x011D4000
  MCU1_LOG_ADDR = 0x011D3000

  在 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/config.ini:24 中存在，但：

  - config_t 没有这些字段；
  - config_apply_param() 没有解析这些 key；
  - config.h 没有对应 getter；
  - 当前 parser 对未知 key 静默忽略。

  相关代码见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/src/config.c:96 和 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/src/config.c:203。

  即使补上 A55 parser，config.ini 也只能改变 A55 的配置。它不会自动改变 MCU1/Q8 的日志写入地址。

  要让这套机制真正生效，至少需要：

  config.ini
      -> A55 parser
      -> A55 保存五个 log 地址
      -> A55 启动前把地址发布到共享配置 ABI
      -> MCU1/Q8 读取共享配置
      -> MCU1/Q8 使用这些地址写 ring buffer
      -> A55 映射并读取 ring buffer

  建议不要把地址命名成含义不明确的 *_LOG_ADDR，而是区分地址视图，例如：

  MCU1_LOG_ADDR_GLOBAL = 0x001D3000
  Q8_CORE0_LOG_ADDR_GLOBAL = 0x001D4000

  MCU1 侧再转换为 local address：

  global 0x001D3000 -> MCU local 0x011D3000

  如果地址固定，建议由 linker script 或统一的公共内存布局头文件作为唯一来源，A55 只读取这个固定布局。不要让 config.ini、MCU1 固件、Q8 固件各自维护三份可能不一致的地址。

  5. 日志应该复用 channel 3，还是使用独立 channel？

  建议：

  日志使用独立的 IPC notification channel。
  日志内容放在 shared-memory ring buffer。
  IPC channel 只发送“有日志可读”的通知。
  不要每条日志都触发 DMA 或发送一次完整 IPC message。

  当前 channel 分配是：

   Channel    当前用途
  ━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
         2    A55 -> MCU0
  ─────────  ────────────────────────────────────────────
         3    MCU1 -> A55，传输 LiDAR 数据
  ─────────  ────────────────────────────────────────────
         4    Q8 Core0..3 -> MCU1，发送 buffer-done 通知
  ─────────  ────────────────────────────────────────────
         5    A55 -> MCU1

  配置见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/init/bcm8915x/scripts/m7_flash_img.yaml:89。

  当前 channel 3 的主要业务是：

  Q8 -> channel 4 -> MCU1
  MCU1 -> channel 3 -> A55

  channel 3 是高频 LiDAR 数据路径。它使用 pointer message，A55 每次收到后处理数据，然后清除 doorbell。

  现有 A55 responder 对 command-form doorbell 还明确不支持：

  if (doorbell & IPC_DOORBELL_CMD_MASK) {
      LOG_ERROR("IPC RESP: command not supported\n");
      return;
  }

  见 /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/src/ipc_resp.c:68。

  所以如果日志复用 channel 3，不能只增加一个 command ID。还必须同时处理：

  - LiDAR data message 与 log message 的区分；
  - pointer message 与 command message；
  - 共享 channel 的 pending/ACK 状态；
  - 日志和 LiDAR payload 的 ownership；
  - 多个 Q8 core 同时通知；
  - A55 端不同消息类型的解析；
  - 日志不能阻塞 LiDAR data；
  - channel 忙时的日志合并和丢弃策略。

  这正是复用 channel 3 可能影响主业务效率的原因。

  更合理的设计是使用一个专用 log channel，例如 channel 6，但必须确认 channel 6 在实际 BL 配置中可用，然后同时修改：

  BL YAML IPC 配置
  A55 U-Boot DTS
  A55 Linux UIO/mapping
  A55 log reader
  MCU1/Q8 log producer

  不一定要为五个 producer 分配五个 IPC channel。可以采用：

  一个专用 log channel
  五个独立 ring buffer
  doorbell 内容携带 producer ID 或 pending bitmap

  例如：

  MCU1  -> MCU1 log ring  ->+
  Q8_0  -> Q8_0 log ring  ->+
  Q8_1  -> Q8_1 log ring  ->+-> 专用 log channel -> A55
  Q8_2  -> Q8_2 log ring  ->+
  Q8_3  -> Q8_3 log ring  ->+

  但因为一个 IPC channel 同时只能有一个未完成的 doorbell，多个 producer 共用时需要：

  - ring 中保存每个 producer 自己的 head/tail；
  - 共享一个 pending bitmap；
  - channel 忙时只置位 pending，不重复发送通知；
  - A55 收到通知后扫描五个 ring；
  - A55 清除 doorbell 前确认已读取 pending；
  - A55 周期性扫描，避免通知丢失导致日志永久滞留。

  6. 每个 4 KiB ring 不建议直接作为字符串缓冲区

  每个 4 KiB 区域建议至少包含：

  magic
  version
  producer_id
  buffer_size
  head
  tail
  sequence
  dropped_count
  records[]

  还需要定义：

  - record 格式；
  - timestamp 来源；
  - wrap-around 规则；
  - buffer 满时覆盖旧日志还是丢弃新日志；
  - A55 如何更新 tail；
  - M7/Q8 写入后的 memory barrier；
  - M7/Q8 D-cache clean；
  - A55 读取前的 cache invalidate 或 non-cacheable mapping；
  - reboot 后 ring 的初始化和 magic 检查。

  日志写入路径应类似：

  格式化一条日志
      -> 写入对应 producer 的 ring
      -> DSB/cache clean
      -> 必要时发送一次 log notification

  而不是：
      -> IPC 发送完整消息
      -> 等待 ACK

  最终建议

  1. ipc_memory_pool 的 0x00080000 是 global/bus physical address，大小 512 KiB，不是 0x011D0000。
  2. 0x011D0000 - 0x011D7FFF 是另一片 SRAM IPC/scratch 区域。
  3. 你提出的 MCU1_LOG_ADDR = 0x011D3000 与工程中的前 16 KiB JTAG IPC scratch 重叠，不能直接使用。
  4. MCU1/Q8 如果使用 local address，应将对应的 global address 明确记录为 0x001Dxxxx。
  5. config.ini 当前会静默忽略这些 log 地址，必须增加 parser、字段和 getter。
  6. config.ini 不能单独改变 MCU1/Q8 的写入位置，还需要固件 linker/启动代码或共享配置 ABI。
  7. 日志建议使用独立 channel，最好是一个专用通知 channel 加五个独立 shared-memory ring。
  8. 不建议把日志 command 直接塞入当前高频 LiDAR channel 3。


已按“Channel 3 保持主业务、Channel 6 独立日志通知、共享内存 ring buffer、A55 直接读取、不使用 DMA”的方案完成 MCU1 侧实现，并准备好了 A55/YAML 待合入内容。

  地址结论

  设备树中的：

  ipc_memory_pool: ipc_memory_pool@00080000 {
      reg = <0x00 0x00080000 0x0 0x80000>;
  };

  在设备树语义上表示 A55/Linux 侧的 reserved-memory 物理区域：

  起始地址: 0x00080000
  结束地址: 0x000FFFFF
  大小:     0x80000

  它不是固定的：

  0x011D0000 - 0x011D7FFF

  也不能把 Channel 6 的 log 地址直接写成 0x011D3000、0x011D4000 等固定地址。

  当前方案使用：

  Channel 6 message base =
      IPC memory pool base + bootloader 分配的 msgStartOffset

  MCU1 从 bootloader 生成的 IPC channel table 中读取 msgStartOffset，A55 通过 Channel 6 对应的 UIO payload 映射获得同一片内存。这样即使 bootloader 重新排列 Channel 3、Channel 6 的内存位置，协议仍然有效。

  已经完成的 MCU1 侧改动

  核心协议定义位于：

  shared/ipc_log_protocol.h:1

  Channel 6 的布局为：

  +0x0000 - +0x001F : 通用 IPC header 保留区
  +0x0020 - +0x005F : log control block

  +0x1000 - +0x1FFF : MCU1 ring
  +0x2000 - +0x2FFF : Q8 Core 0 ring
  +0x3000 - +0x3FFF : Q8 Core 1 ring
  +0x4000 - +0x4FFF : Q8 Core 2 ring
  +0x5000 - +0x5FFF : Q8 Core 3 ring

  每个 ring：

  ring 总空间: 4 KiB
  ring header: 128 bytes
  record 大小: 256 bytes
  record 文本: 240 bytes
  record 数量: 15
  实际可用槽位: 14

  MCU1 侧日志实现位于：

  app/src/logger/log_ipc.c:1

  实现内容包括：

  - Channel 6 动态地址发现。
  - MCU1 专属 ring 初始化。
  - Q8 ring 接口保留，但不会清空或初始化 Q8 ring。
  - producer-owned head。
  - consumer-owned tail。
  - dropped 丢弃计数。
  - cache clean/invalidate。
  - release/acquire 原子索引发布。
  - ring 满时丢日志，不阻塞 LiDAR 主业务。
  - 空 ring 转非空时发送一次 doorbell。
  - MCU1 周期性扫描 Q8 ring，兼容未来 Q8 logger。
  - 日志内容直接写入共享内存。
  - 不使用 DMA 搬运日志。

  动态 IPC helper 位于：

  app/src/bcm/ipc_helper.c:1

  新增和调整了：

  IPCH_GetMsgPtr()
  IPCH_GetMsgPayloadPtr()
  IPCH_GetMsgSize()
  IPCH_SendMsg()

  其中：

  IPCH_SendMsg(IPC_CHAN_LOG_TO_A55, 0UL);

  会根据 Channel 6 当前的 msgStartOffset 动态计算 doorbell pointer，不再使用固定 log 地址。

  mcu1.ld 中原先固定的 IPC_LOG linker region 已移除，避免把 Channel 6 错误地绑定到固定地址：

  app/mcu1.ld:48

  ELog 仍然保留，当前 MCU1 配置为 IPC-only：

  app/src/params_cfg.c:28

  elog_log_config_t elog_config =
  {
      .transport = ELOG_TRANSPORT_IPC,
  };

  同时调整了初始化顺序，使 MPU 和 D-cache 在 ELog/IPC logger 初始化前完成：

  app/src/app_system.c:115

  A55 侧待合入文件

  由于当前工作区权限只允许写入：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_MCU_OS

  不能直接修改外部的：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/A55_system/...

  所以 A55 修改已暂存到：

  pending_a55_lidar_app

  其中：

  - pending_a55_lidar_app/files/src/ipc_log_resp.c:1
  - pending_a55_lidar_app/files/include/ipc_log_resp.h:1
  - pending_a55_lidar_app/files/include/ipc_log_protocol.h:1
  - pending_a55_lidar_app/files/src/main.c:1
  - pending_a55_lidar_app/files/Makefile:1

  A55 实现行为：

  - 初始化 Channel 6 responder。
  - 使用 UIO payload 作为动态 Channel 6 base。
  - doorbell 仅作为“有日志”的通知。
  - 通过 uio_mbox_phy_to_virt() 验证 doorbell pointer。
  - 直接读取 UIO 映射内存。
  - 不使用 DMA。
  - 每 10 ms 扫描一次所有 producer ring。
  - 收取 MCU1、Q8_0、Q8_1、Q8_2、Q8_3 五种 producer。
  - 保留 Q8 接口，但 Q8 当前没有 logger producer 实现。
  - 消费完之后通过 uio_mbox_send_resp() 清除 doorbell 并重新使能中断。
  - 不影响 cmd.c 中已有的 Channel 3 LiDAR responder 线程。

  原有 Channel 3 responder 仍然由：

  cmd.c -> start_proc() -> pthread_create(ipc_resp_thread)

  创建。Channel 6 responder 由新的 main.c 独立创建，两者没有共用 doorbell 或 DMA 路径。

  YAML

  我检查了实际 YAML，当前 Channel 6 仍然是：

  - ChannelId: 6
    TargetCPU: A55
    SrcCPU: [MCU_1]
    MsgMaxSize: 0x8000

  需要改为：

  - ChannelId: 6
    TargetCPU: A55
    SrcCPU: [MCU_1, Q8_0, Q8_1, Q8_2, Q8_3]
    MsgMaxSize: 0x8000

  对应补丁位于：

  pending_a55_lidar_app/m7_flash_img.yaml.patch:1

  这里使用了实际 YAML 中的 MCU_1 命名，而不是 MCU1。

  config.ini 不需要增加：

  MCU1_LOG_ADDR
  Q8_Core0_LOG_ADDR
  Q8_Core1_LOG_ADDR
  Q8_Core2_LOG_ADDR
  Q8_Core3_LOG_ADDR

  这些固定地址配置应该删除或不添加，因为 ring 地址相对于动态 Channel 6 message base 固定，而 message base 由 bootloader 分配。

  DTS 检查

  你在：

  A55_system/u-boot/arch/arm/dts/bcm89158.dts

  中增加的节点：

  mbox_resp_log: mbox_resp_log {
      compatible = "brcm,rigel-mbox-client";
      mboxes = <&mailbox (MBOX_ACC_NORMAL_MASK | MBOX_TYPE_RESP | MBOX_ID(6))>;
      mbox-names = "mcu1-q8-log";
      memory-region = <&ipc_memory_pool>;
  };

  配置方向正确：

  - MBOX_TYPE_RESP 与 A55 接收 MCU1/Q8 日志相符。
  - MBOX_ID(6) 与 Channel 6 相符。
  - memory-region = <&ipc_memory_pool> 使 UIO 映射 bootloader IPC pool。
  - 不需要在 DTS 中指定 MCU1/Q8 各自的固定 log 地址。

  使用 C preprocessor 和 dtc 检查后可以成功生成 DTB。输出中只有原有 DTS 的格式 warning，例如 ipc_memory_pool@00080000 的 leading zero warning，没有 Channel 6 节点语法错误。

  另外，Yocto 的启动配置确实使用 U-Boot 生成的 bcm89158.dtb：

  IMAGE_BOOT_FILES:rigel = "...,bcm89158.dtb"
  UBOOT_DTB_BINARY = "bcm89158.dtb"

  因此当前工程没有发现需要额外同步的 BCM8915X Linux 板级 DTS 文件。

  验证结果

  已通过：

  make mcu1 V=1

  生成：

  build/out/mcu1/bin/bcm8915x_mcu1.elf
  build/out/mcu1/bin/bcm8915x_mcu1.bin
  build/out/mcu1/bin/bcm8915x_mcu1.img

  内存使用：

  DTCM_BSS: 1280 B / 16 KB
  SRAM:     72188 B / 256 KB

  还通过了：

  gcc -std=c11 -Wall -Wextra -Werror -pedantic

  共享协议头检查、A55 新增源码主机侧语法检查、ring ABI 运行时布局检查以及：

  git diff --check

  MCU1 构建中仍存在原有 linker warning：

  cannot find entry symbol BCM_OS_RESET_HANDLER;
  defaulting to 0x01040000

  这不是本次 Channel 6 日志改动引入的错误。

  A55 由于外部目录在当前权限下不可写，尚未执行最终 BitBake 构建；目前已完成主机侧严格语法检查。app/src/logger/log_uart.c 和 app/src/logger/dma_log.c 暂时保留，因为 MCU1 的 cli_uart.c 仍然依赖 LogUart_* 接口。运行配置已经是 IPC-only，因此它们不会作为日志输出后端初始化。

# success 

# IPC Log (MCU1 → A55, Channel 6)

MCU1 (FreeRTOS, `BCM8915X_MCU_OS`) produces log lines into a shared-memory ring;
A55 (Linux, `bcm8915x_app`) consumes them and writes `/tmp/log/mcu1.log`. The
same layout reserves rings for four Q8 cores, but only the MCU1 producer is
implemented today.

- Shared ABI: `BCM8915X_MCU_OS/shared/ipc_log_protocol.h`
  == `bcm8915x_app/include/logger/ipc_log_protocol.h` (keep byte-identical).
- MCU1 producer: `BCM8915X_MCU_OS/app/src/logger/log_ipc.c` (+ EasyLogger port
  `elog_port.c`, async drain `log_task.c` + `elog_async.c`).
- A55 consumer: `bcm8915x_app/src/logger/ipc_log_resp.c`,
  `ipc_log_file.c`; buffer publish in `bcm8915x_app/src/system.c`.

## Memory layout (Channel 6 payload, 0x8000 bytes)

Channel 6 is allocated by the bootloader. Both sides discover its message base
and size from the IPC channel config (no fixed address baked in).

```
offset   size    contents
0x0000   0x20    generic IPC message header (reserved)
0x0020   0x40    control block   (bcm8915x_ipc_log_control_t)
0x1000   0x1000  ring: MCU1      (bcm8915x_ipc_log_ring_t)
0x2000   0x1000  ring: Q8_0
0x3000   0x1000  ring: Q8_1
0x4000   0x1000  ring: Q8_2
0x5000   0x1000  ring: Q8_3
```

Each ring: 128-byte header (magic/version/producer/record_size/record_count,
then cache-line-isolated `head`, `tail`, `dropped`) followed by 15 records of
256 bytes. A record is `sequence(4) timestamp_ms(4) length(2) level(1)
flags(1) reserved(4) text[240]`. SPSC: producer owns `head`, consumer owns
`tail`; `head==tail` means empty.

## Startup handshake

1. A55 `ipc_log_resp_init()` opens the Channel 6 UIO device, mmaps the payload
   (`MAP_SHARED`, `O_SYNC`), and starts the receiver thread.
2. A55 `configure_log_buffers()` computes the five ring physical addresses
   (`payload_phys + ring_offset`) and publishes them into the shared LiDAR
   config (`logBufferInfo[5]`, at phys `0x00190000`, magic `LIDR`).
3. MCU1 `LogIpc_Init()` gets the Channel 6 base/size, reads `logBufferInfo` for
   its own ring (falling back to the fixed channel-base layout if the config
   isn't published yet), then initializes the control block and the MCU1 ring
   (writes magic last, after all immutable fields).
4. Steady state: EasyLogger formats a line → async ring (`elog_async.c`) →
   `LogTask` drains it → `LogIpc_Send()` appends a record and rings the Channel
   6 doorbell only on empty→non-empty. A55 wakes on the doorbell (also polls
   every 10 ms), copies records out, advances `tail`, writes the file.

Doorbell also carries command-form trace codes (`0x6C67xxxx`) used for
bring-up; A55 logs them as `received MCU1 log trace 0x....`.

## Cache / coherency

MCU1 maps the shared SRAM window (MPU region 6, `0x01000000`+2MB) as **Normal
non-cacheable** (`app_system.c`, TEX=1/C=0/B=0). The `SCB_Clean/Invalidate`
calls in `log_ipc.c` are therefore effectively no-ops but are kept so the code
stays correct if the region is ever made cacheable. Ordering is enforced with
`__atomic` acquire/release on `head`/`tail` plus `__DSB()` before the doorbell.

## Validation on both sides

Consumer treats a ring/control block as valid only if magic + version +
`record_size` + `record_count` (+ `producer_id`, + `channel_size`/`ring_size`/
`producer_count`/`ring_offset[]` for the control block) all match. Invalid
blocks are logged once (`IPC log ring ... invalid`, `IPC log control invalid`)
and skipped. A55 file output is configured via the `IPC_LOG:` section of the
YAML config (`PATH`, `MCU1_FILE`, `Q8_FILE_PATTERN`, `ENABLED`).

## Pitfall log

### 1. MCU1 `.data` initializers were dropped from the image (root cause of empty logs)

**Symptom:** A55 received a growing stream of records with correct
`sequence`/`timestamp` but `length=240` and all-zero `text`
(`[seq=N][t=Nms] <<empty>>`).

**Why it was misleading:** correct seq/timestamp proves the ring, addressing,
and cache path all work — those fields and `text` live in the same 256-byte
record copied as one unit. So the text was genuinely zero *in memory*; the bug
was upstream of the ring.

**Actual cause — build/linker, not logic:**
- The raw image is `objcopy --only-section=.text` (`Makefile:76`); `Startup.S`
  copies `.data` from `__data_load__` at boot.
- `app/mcu1.ld` had placed `.data` in a **separate** section
  (`> DTCM_BSS AT > SRAM`) whose LMA fell **past the end of `.text`**, so its
  initializer bytes were never in the `.bin`. Startup then copied garbage →
  every nonzero-initialized global booted wrong.
- In the logger, `buf_is_empty` (the only nonzero-init static in
  `elog_async.c`, `= true`) came up `false`, so `elog_async_get_buf_used()`
  reported the whole 10 KB async ring as "used" zeros and `LogTask` streamed
  empty records forever.

**Fix:** carry `*(.data*)` **inside** the `.text` output section in-place
(VMA==LMA) so it rides the loaded payload and the startup copy is a no-op. This
mirrors the contract already documented in `app/firmware_common.ld` (which also
cites FreeRTOS `uxCriticalNesting = 0xaaaaaaaa` as another victim of the same
trap). Verify after building: the `buf_is_empty` init byte (`01`) must be
present in the `.bin` at its symbol offset.

> Any linker script feeding this `.text`-only image must keep initialized data
> inside `.text`. Check `mcu0.ld` / Q8 scripts for the same pattern.

### 2. Two copies of `ipc_log_protocol.h`

The MCU1 and A55 headers must stay byte-identical (only whitespace may differ).
A struct/offset drift shows up as garbled fields, not empty text — different
failure mode from #1. `static_assert`s in the header guard sizes/alignment.

### 3. Doorbell BUSY is normal

`IPCH_SendMsg` returns `BCM_ERR_BUSY` while A55 still owns the previous
notification. The producer does not retry-block; A55's 10 ms poll and the next
log line's doorbell cover it. Don't add blocking retries in the log hot path
(`IpcLogDoorbellTraceSend` deliberately delays 20 ms and must stay off the hot
path).
