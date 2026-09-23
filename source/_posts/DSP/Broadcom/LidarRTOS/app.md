  # 一、BCM8915X_Main 之前发生了什么

  BCM8915X_Main 不是 C 运行时的 main —— 链接命令行 -e BCM_RESET_HANDLER（common.mk:47）把入口设成了汇编
  reset handler，crt0 的 __libc_init_array 那一整套从未执行。

  ### 向量表（exceptions.S:101-118）

  ```asm
  .section .vector_tbl,"a",%progbits
  CORTEX_MX_VECTOR_TBL:
      .word __main_stack__                  // [0] 初始 MSP
      VECTOR_FN(BCM_RESET_HANDLER)          // [1] Reset
      VECTOR_FN(BCM_NMI_ISR_LOOP)           // [2]
      ...
      VECTOR_FN(vPortSVCHandler)            // [11] SVCall  ← FreeRTOS
      VECTOR_FN(xPortPendSVHandler)         // [14] PendSV  ← FreeRTOS
      VECTOR_FN(xPortSysTickHandler)        // [15] SysTick ← FreeRTOS
      VECTOR_FN(default_irq_handler) × 240  // IRQ[0..239]
    ```

  mcu1.ld:34 把 .vector_tbl 放在 .text 最前，即 SRAM 0x01040000。

  三个 FreeRTOS handler 已正确挂上 —— 这是 CM7 port 能跑起来的前提，port.c 里的
  configCHECK_HANDLER_INSTALLATION 断言正是查这个。

  其余 240 个 IRQ 全是 b . 死循环。后面会看到，这不是隐患，因为这个应用一个 NVIC 中断都没开。

  ### Reset handler（startup.S:50-118）

  ```asm
  BCM_RESET_HANDLER:
      MPU->CTRL = 0                          ; 1. 关 MPU（复位默认已关，防热复位残留）
      SCB->VTOR = CORTEX_MX_VECTOR_TBL       ; 2. 重定位向量表到 SRAM
      MSP       = __main_stack__             ; 3. 栈指针 → DTCM 0x200039c8
      copy .data: SRAM(LMA) → DTCM(VMA)      ; 4.
      zero .bss: 0x20000000 .. 0x200035c4    ; 5.  ← DBGLOG_Buffer 在这里被清零
      SCB->CCR   = 0x10                      ; 6. DIV_0_TRP
      SCB->SHCSR = 0x70000                   ;    USG|BUS|MEMFAULT ENA
      BLX BCM8915X_Main                      ; 7.
  LOOP: b LOOP                               ;    Main 返回则挂死
```

### 三点值得注意

  - .data 的 LMA/VMA 分离（mcu1.ld:65 > DTCM_BSS AT > SRAM）。上电镜像只有 SRAM 一份，运行期才搬到 DTCM。
  - .extern SystemInit_Early 声明了却从没调用（startup.S:41）。时钟树由更早的 boot ROM / MCU0 配好，这里假定
  600 MHz 已就绪 —— 和 configCPU_CLOCK_HZ = 600000000UL 对应。
  - 第 6 步开的 MEMFAULTENA 马上会被 ARM_MPU_Disable() 清掉，再由 ARM_MPU_Enable()
  置回。无害，但确实是重复动作。


## 二、BCM8915X_Main 本体（main.c:87-103）

  ```c
  void BCM8915X_Main(void)
  {
      BCM8915X_MpuSetup();                                    // ①
      DBGLOG_PRINT("LidarRTOS App Starting...");              // ②
      CTXM7_DWTInit();                                        // ③
      BaseType_t ret = xTaskCreate(ACQ_Task, "Acquisition Task",
                                   512, NULL, 1, NULL);       // ④
      if (ret != pdPASS) {
          DBGLOG_PRINT("Acquisition task creation failed!");
      }
      vTaskStartScheduler();                                  // ⑤
      DBGLOG_PRINT("Scheduler exited unexpectedly");          // ⑥
  }
```

  反汇编（out/lidar_rtos_app.S:448 起，0x01040480）确认 ①②③ 全部被内联进 BCM8915X_Main，整个函数只剩三次真实
  bl：DBGLOG_Push、xTaskCreate、vTaskStartScheduler。

  ### ① MPU + D-Cache

  前面详述过。要点回顾：8 个 region，PRIVDEFENA=0（无背景区，越界即 MemManage），HFNMIENA=1。所有 region
  都是 non-cacheable，所以随后的 SCB_EnableDCache() 实际不产生任何缓存行。

  这一步必须最先做，因为它决定了后面每一次访存的合法性 —— 尤其是 LIDAR_SHARED_CTX（0x01190000）必须落在
  region 6（SRAM 0x01000000 + 2MB）里才不会 fault。

  ### ② 第一条日志

  写进 DTCM 0x20000000 的环形缓冲。如前所述，没有接收端，只能靠 JTAG dump。

  ### ③ DWT 周期计数器

  ```c
  CoreDebug->DEMCR |= TRCENA;      // 使能 trace 子系统
  DWT->LAR = 0xC5ACCE55;           // CoreSight 解锁（CM7 需要）
  DWT->CYCCNT = 0;
  DWT->CTRL |= CYCCNTENA;
```

  两个观察：

  - CTXM7_DWTInit() 被调用了两次 —— 这里一次，acq_task.c:263 又一次。第二次会把 CYCCNT 清零重来。目前没人用
  CTXM7_DWTGetCount()（IPC 时间戳走的是 NSU_ReadTimestamp64()，ipc_msg.c:96），所以无影响，但确实是冗余。
  - ctx_m7.h:27 用的是裸 inline（非 static inline）。C99
  语义下这不产生外部定义，一旦编译器决定不内联就会链接失败。map 里查不到 CTXM7_DWTInit 符号，说明 -Os
  下两处都内联了 —— 侥幸通过。改成 static inline 才是稳的。

  ### ④ 创建唯一任务

  ```c
  xTaskCreate(ACQ_Task, "Acquisition Task", 512, NULL, 1, NULL);
  ```

  - 栈深 512 word = 2048 字节，从 FreeRTOS heap 分配（heap_4.c，configSUPPORT_STATIC_ALLOCATION = 0）
  - 优先级 1，高于 idle(0)，低于 timer 任务(2)
  - 任务名 16 字符，正好卡在 configMAX_TASK_NAME_LEN = 16 —— 含 NUL 会截断成 "Acquisition
  Tas"。无功能影响，只影响调试显示

  失败分支只打一条日志，然后继续往下走去启动调度器。结果会是一个只有 idle 任务的空转系统。这里应该 trap
  才对。

  ### ⑤ vTaskStartScheduler()

  这一步内部依次：

  1. 创建 Idle 任务（128 word = 512 B，configMINIMAL_STACK_SIZE）
  2. 创建 Timer 任务（configUSE_TIMERS = 1，256 word = 1024 B，优先级 2）+ 长度 10 的命令队列
  3. xPortStartScheduler()：
    - 探测 NVIC 实际优先级位数（写 0xFF 读回），校验 configMAX_SYSCALL_INTERRUPT_PRIORITY
    - PendSV / SysTick 设为最低优先级
    - SysTick->LOAD = 600000000/1000 - 1 = 599999，即 1 ms tick
    - 使能 FPU（vPortEnableVFP）
    - svc 0 → vPortSVCHandler 恢复第一个任务上下文，从此不再返回

  ### ⑥ 不可达

  只有调度器启动失败（heap 不够）才会走到。


## 三、内存账本（都从 map 文件核过）

### DTCM 16 KB — mcu1.ld:17

  ```text
  ┌─────────────────────────┬────────────┬─────────────────┐
  │          区域           │    地址    │      大小       │
  ├─────────────────────────┼────────────┼─────────────────┤
  │ DBGLOG_Buffer           │ 0x20000000 │ 0x1004 (4100 B) │
  ├─────────────────────────┼────────────┼─────────────────┤
  │ IPC_MsgContext          │ 0x20001004 │ 0x14            │
  ├─────────────────────────┼────────────┼─────────────────┤
  │ FreeRTOS 内核 bss       │ 0x20001018 │ ~0x554          │
  ├─────────────────────────┼────────────┼─────────────────┤
  │ ucHeap                  │ 0x2000156c │ 0x2000 (8 KB)   │
  ├─────────────────────────┼────────────┼─────────────────┤
  │ 其余 bss                │ …          │ …               │
  ├─────────────────────────┼────────────┼─────────────────┤
  │ __bss_end__             │ 0x200035c4 │                 │
  ├─────────────────────────┼────────────┼─────────────────┤
  │ MSP 栈顶 __main_stack__ │ 0x200039c8 │ 1 KB 主栈       │
  ├─────────────────────────┼────────────┼─────────────────┤
  │ 上限                    │ 0x20004000 │ 剩 1592 字节    │
  └─────────────────────────┴────────────┴─────────────────┘
  ```

  DTCM 只剩 1.5 KB 余量。而 DBGLOG_Buffer 一个人就吃掉 4 KB（25%）—— 那个从没被读过的缓冲区。把 DBGLOG_QSIZE
  从 128 降到 32 就能立刻释放 3 KB。

  ### FreeRTOS heap 8 KB — configTOTAL_HEAP_SIZE

  ```text
  ┌────────────────────────┬────────────────┐
  │         消费者         │      字节      │
  ├────────────────────────┼────────────────┤
  │ ACQ 任务栈 (512 w)     │ 2048           │
  ├────────────────────────┼────────────────┤
  │ Timer 任务栈 (256 w)   │ 1024           │
  ├────────────────────────┼────────────────┤
  │ Idle 任务栈 (128 w)    │ 512            │
  ├────────────────────────┼────────────────┤
  │ 3 × TCB                │ ~300           │
  ├────────────────────────┼────────────────┤
  │ Timer 命令队列 (10 项) │ ~200           │
  ├────────────────────────┼────────────────┤
  │ heap_4 块头开销        │ ~100           │
  ├────────────────────────┼────────────────┤
  │ 合计                   │ ~4.2 KB / 8 KB │
  └────────────────────────┴────────────────┘
  ```

  够用，一半余量。但 vApplicationMallocFailedHook 只打日志然后死循环 ——
  而日志没人读，所以现象就是「板子静默挂死」。

  ### 代码 SRAM

  .text + .rodata 在 0x01040000（256 KB 窗口）。MPU region 6 从 0x01000000 起 2 MB，覆盖到。


## 四、稳态：调度器起来之后

  ```text
  vTaskStartScheduler()
     └─ ACQ_Task (prio 1)  ─── 唯一有实际工作的任务
          ├─ 检查 LIDAR_SHARED_CTX->magic (0x01190000)
          │    └─ 不匹配则填默认配置（RAW 模式, 120°×30° FOV,
          │       2000 acq/line, 5G 采样, 8K 样本, 5 µs 触发间隔）
          ├─ CTXM7_DWTInit()            ← 第二次
          ├─ ACQ_Start()
          │    ├─ NSU_DrvInit
          │    ├─ HSADCH_FullInit(4 通道校准)
          │    ├─ HSADCH_ConfigFirFilter   （ACQ_FIR_FILTER_SEL=3 → 编译期跳过）
          │    ├─ ACQ_SetupTriggerDelays(TXSLZR=1, HSADC=10, HSREF=10)
          │    ├─ TXSH_SetupTofPulse
          │    ├─ CCUH_SetupAcqTrigger  ← 硬件定时器开始自主产生触发
          │    ├─ CCUH_StartNsuTrigger(ch2, 500 µs 后)
          │    └─ ACQ_SetupGPIOs(GPIO 61/48/47/46)
          └─ IPC_MsgProcess()  ─── while(1) 永不返回
               ├─ IPCH_RecvMsg(DSP→MCU1)   ← 忙轮询，无阻塞
               ├─ T2 = NSU_ReadTimestamp64()
               ├─ 按 BUF_DONE0/1 选 ping/pong 缓冲地址
               ├─ IPCH_AckMsg(DSP)
               ├─ 填 txPayload，IPCH_SendMsg(MCU1→A55)
               └─ lineIndex++ / frameNum++
```

  这里有个结构性观察：IPC_MsgProcess 是一个纯忙轮询的 while(1)，没有 vTaskDelay、没有队列阻塞、没有
  taskYIELD。ACQ 任务优先级 1 > idle 优先级 0，所以：

  - Idle 任务永远不会运行（没有 tickless、没有 WFI、没有 heap 回收）
  - Timer 任务优先级 2 更高，但没人用软定时器，它一直阻塞在命令队列上
  - 一个 NVIC IRQ 都没开（全代码库 grep 不到 NVIC_EnableIRQ）

  也就是说，运行期实际发生的异常只有 SysTick（1 kHz，纯粹增加 tick 计数，无人消费）和抢占用的
  PendSV（永不触发，因为没有第二个就绪任务）。

  FreeRTOS 在这个应用里是纯开销 —— 它提供的抢占、阻塞、定时器一个都没用上，换来 8 KB heap + ~4.5 KB 内核 bss
  + 每毫秒一次的 SysTick。当前形态下裸机 while(1) 完全等价。当然，这是 REF_SW_REL_0_1
  参考实现，脚手架大概是给后续加任务预留的。


## 五、启动路径上确实值得改的几处

### 按严重度排

  1. xTaskCreate 失败后不该继续。 main.c:96-98 打完日志就往下跑
  vTaskStartScheduler()，得到一个什么都不做的系统。改成和两个 hook 一致的 while(1) trap。

  2. configENABLE_MPU = 1 是无效设置。 FreeRTOSConfig.h:32 开了它，但 common.mk:88 选的 port 是
  GCC/ARM_CM7/r0p1（非 MPU 版本）。我 grep 过该 port 的 port.c / portmacro.h，完全没有引用 configENABLE_MPU
  或 portUSING_MPU_WRAPPERS。要真正的任务级 MPU 隔离得换 ARM_CM7_MPU port 并链接
  mpu_wrappers.c。现在这个设置只会误导读代码的人 —— MPU 保护是全局静态的，任务之间没有隔离。

  3. CTXM7_DWTInit() 调了两次。 删掉 main.c:93 那次即可（acq_task.c:263 那次离使用点更近）。

  4. ctx_m7.h 的裸 inline 改 static inline。 现在能链上是因为 -Os 恰好内联了；DEBUG=1（-Og）下就可能变成
  undefined reference。

  5. configSTART_RECURSIVE_MUTEX_TESTS      000     0（FreeRTOSConfig.h:109）—— 明显的手滑，宏体变成了 000
    0。该宏没被引用所以没炸。

# ● A55 app 不只是启动和配置，它还要实时接收和处理雷达数据。
     
结构是这样的：

A55 Linux app (user-space)
    ├─ IPC req (→ MCU0)
    │   ├─ cmd_init(mode)     ← 加载 Q8 固件、加载 MCU1 固件
    │   ├─ cmd_start()        ← 启动 MCU1、启动 Q8
    │   └─ cmd_stop()         ← 停止采集
    │
    └─ IPC resp (← MCU1)  [后台线程]
        ├─ 接收 IPC_CMD_LIDAR_DATA 消息
        ├─ 读 IPCH_LidarPayloadType 头 (timestamp, frame#, line#, format, samples)
        ├─ 读缓冲区地址，从共享 SRAM 搬数据
        └─ 处理（UDP 转发给 host GUI / 存文件 / 信号处理）

具体工作流（从 cmd.c 看）：

1. cmd_init(mode) :
→ IPCH_SendMsg(IPC_CHAN_A55_TO_MCU0, loader_init_proc)
 ├─ load Q8  firmware from /lib/firmware/rigel/q8.bin  → 0x00100000
 ├─ load MCU1 firmware from /lib/firmware/rigel/mcu1.bin → 0x00040000
 └─ 启动响应线程轮询 MCU1 IPC 通道

2. cmd_start() :
→ IPCH_SendMsg(IPC_CHAN_A55_TO_MCU0, start)
 └─ MCU0 启动 Q8 和 MCU1

3. 后台线程 ipc_resp_thread() 永不返回 :
while (1) {
   IPCH_RecvMsg(IPC_CHAN_MCU1_TO_A55, &msg)
   if (msg.cmd == IPC_CMD_LIDAR_DATA) {
       payload = msg.metaDataAddr
       buffer = payload->dataAddress  ← 在 shared SRAM 里
       // UDP 发给 host, 或写文件, 或存储
   }
}

也就是说，A55 app：
- ✅ 加载固件（从 /lib/firmware/rigel/）
- ✅ 启动 MCU1 和 Q8（通过 MCU0 bootloader IPC）
- ✅ 接收实时雷达数据（从 MCU1 来的 ping/pong 缓冲区）
- ✅ 处理数据（UDP 转发、文件保存等）
- ❌ 读取 MCU1/Q8 日志（如前所述，日志不在 IPC 通道里）

