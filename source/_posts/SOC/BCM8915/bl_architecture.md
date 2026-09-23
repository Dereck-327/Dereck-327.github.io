# BCM8915x BootLoader 架构梳理

> 面向想理解「整个仓库怎么组织、各模块干什么、彼此怎么耦合」的开发者。
> 不含 `oneui/`（那是独立的 PC 端 GUI 配置/烧录工具，见文末附注）。
> 启动执行流的细节见 [`LEARNING_GUIDE.md`](LEARNING_GUIDE.md)，本文补的是**横向的模块地图与耦合关系**。

---

## 1. 一句话定位

这是跑在 **BCM8915x SoC 的 Cortex-M7 引导核**上的 BootLoader。它自身是一个带轻量 RTOS 的固件：上电 → 初始化底层（MPU/内存/DDR）→ 从 flash 安全加载各核镜像 → 放各核出复位 → 加载 M7 主固件并交权退场。

整个代码库按**分层 + 组件（component）**组织：每个组件是一个目录，用 `comp.mk` 声明自己的名字、类型、依赖和源文件。`build/` 的 Makefile 体系读取这些 `comp.mk`，按依赖关系把需要的组件编成静态库再链接成最终 ELF。

---

## 2. 顶层目录地图

| 目录 | 层次 | 职责 |
|------|------|------|
| `architecture/` | 基础设施层 | 编译器/错误码抽象、消息队列、服务运行时、分区表工具 —— 全仓库的地基 |
| `base/` | OS 层 | 轻量 RTOS 内核（任务/事件/闹钟）、ARMv7-M 架构相关、SVC 处理端 |
| `cpu/arm/` | CPU 抽象层 | Cortex-M 的 cache / MPU / 中断 / SVC 触发封装 |
| `system/drivers/` | 外设驱动层 | uart / gpio / dma / ddr / pcie / mcu / ipc / dbglog |
| `nvm/drivers/` | 存储层 | flash 全栈（服务→FTL→SFDP→SPI）、OTP / secure-OTP、patch |
| `crypto/` | 安全层 | PKA/SKA 硬件加速、随机数、密钥库、加解密服务、签名校验服务 |
| `init/` | 应用/编排层 | 芯片早期初始化、Boot Service 主逻辑、镜像 Loader、系统服务 |
| `build/` | 构建系统 | Makefile 片段、链接脚本、编译器规则、密钥、打包脚本 |
| `tools/` | 工具 | rdbgen（寄存器定义生成）、vcast（测试工具） |
| `prebuilt/` | 预编译产物 | 第三方/闭源库（DPFE、emFile FTL）与 RDB 头文件 |
| `oneui/` | PC 工具（不在本文范围） | 独立的 Python GUI，用于配置生成和烧录 |

---

## 3. 每个组件长什么样（统一约定）

理解一个组件前，先记住这套目录约定，一次学会到处适用：

| 子目录 | 含义 |
|--------|------|
| `inc/` | 对外公开头文件（其它组件 `#include` 的接口）。前缀即模块名 |
| `inc/osil/` | OS 集成层头文件（osil = OS Integration Layer） |
| `lib/` | 与 OS 无关的核心实现（纯算法/硬件寄存器操作） |
| `os/base/` | 有 RTOS 的实现：走 SVC / 消息队列 / 服务框架 |
| `os/none/` | 裸机实现：直接调用，不走 RTOS 机制 |
| `os/common/` | 两种 OS 共用的实现 |
| `config/` | 芯片/型号相关的配置数据 |
| `tests/` | 单元/集成/资格测试（`unit` `integration` `qualification` `probe`） |
| `doc/` | 需求追溯桩文件（`*_req.c`，安全认证用，阅读可忽略） |
| `comp*.mk` | **组件清单**：声明 `BRCM_COMP_NAME`、`BRCM_COMP_DEPENDS`（依赖）、`BRCM_COMP_TYPE`（lib/doc/test）、源文件、注册的任务/事件/闹钟 |

> **`os/base` vs `os/none` 是全仓库最重要的分叉**：同一个驱动往往两套实现，BootLoader 编 `base` 版（`OS = base`，见 `build/bootloader.mk`）。`base` 版里凡是特权操作都会通过 SVC 陷入。

命名前缀直接对应模块，看前缀就知道去哪找：
`BCM_`/`BASE_`=OS内核，`CPU_`/`CORTEX_MX_`=CPU抽象，`MSGQ_`/`SRVRT_`/`LWQ_`/`PTU_`=基础设施，`BOOTSRV_`/`LOADER_`/`SYSSRV_`=编排层，`FLASHSRV_`/`OTP*`=存储，`VERIFYSRV_`/`CIPHERSRV_`/`KSTSRV_`/`PKADRV_`/`SKADRV_`=安全，`DDRDRV_`/`MCU_`/`UART_`/`IPCDRV_`=驱动。

---

## 4. 分层架构总览

从下往上，上层依赖下层，下层不知道上层的存在：

```
┌────────────────────────────────────────────────────────────────┐
│  init/ —— 编排 / 应用层                                          │
│    bootsrv(主逻辑) · loader(镜像加载) · syssrv · systemtask       │
│    init/bcm8915x(芯片早期初始化 + 启动汇编 + SVC分发表)           │
└───────────┬───────────────────────┬────────────────┬────────────┘
            │                       │                │
┌───────────▼──────────┐ ┌──────────▼───────┐ ┌───────▼──────────┐
│ crypto/ —— 安全层     │ │ nvm/ —— 存储层    │ │ system/drivers/  │
│  verifysrv/ciphersrv  │ │  flashsrv         │ │  ddrdrv mcu uart │
│  kstsrv               │ │   ↓ftl ↓sfdp ↓spi │ │  gpio dma pcie   │
│  pka ska csrbg hsm    │ │  otp/secotp/pch   │ │  ipc dbglog      │
└───────────┬──────────┘ └──────────┬───────┘ └───────┬──────────┘
            └───────────────────────┼─────────────────┘
                        ┌───────────▼────────────┐
                        │ architecture/ 基础设施   │
                        │  srvrt(服务运行时)       │
                        │  msgq(消息队列) lwq ptu  │
                        └───────────┬────────────┘
            ┌───────────────────────┼────────────────────┐
┌───────────▼─────────┐  ┌──────────▼────────┐  ┌─────────▼────────┐
│ base/ —— RTOS内核    │  │ cpu/arm/ CPU抽象   │  │ architecture/    │
│  任务/事件/闹钟       │  │  cache/mpu/中断/SVC │  │ abstract 地基     │
│  ARMv7-M / SVC处理端 │  │  触发端            │  │ (编译器/错误码)   │
└─────────────────────┘  └───────────────────┘  └──────────────────┘
```

**贯穿所有层的一根主线是 SVC 系统调用**：`init/bcm8915x` 启动阶段把系统降到非特权线程态后，任何驱动/服务要做特权操作（cache 维护、MPU、跳转交权）都要走
`cpu/arm` 触发 `SVC #0` → `base` 的 `BASE_ARMv7mSVCHandler` 接住 → `init/bcm8915x/chip/common/svc_handlers.c` 的分发表按 ID 调具体特权函数。

**第二根主线是消息/服务框架**：`msgq` 提供异步消息，`srvrt` 在其上封装出「服务」概念（注册初始化/请求处理/拆除回调），存储和安全的所有 `*SRV_` 组件都是 srvrt 服务，靠 `systemtask` 跑的事件循环驱动。

---

## 5. 各层模块详解

### 5.1 architecture/ —— 基础设施层（全仓库地基）

| 组件 | 目录 | 干什么 | 关键接口 | 依赖 |
|------|------|--------|----------|------|
| **abstract** | `architecture/abstract` | 最底层地基：编译器抽象(`compiler.h`,`COMP_ASM`)、统一错误码(`bcm_err.h`,`BCM_ERR_*`)、CPU/cache/MPU 接口声明、消息类型(`bcm_msg.h`)、状态机(`bcm_sm.h`)、时间(`bcm_time.h`)、通用工具(`bcm_utils.h`) | 类型与宏为主 | 无（谁都依赖它） |
| **lwq** | `architecture/lwq` | 轻量无锁队列：整个队列状态压进一个 64-bit 字，最多 15 个 4-bit 索引，供 msgq 做底层索引存储 | `LWQ_Push/Pop/Peek/Length` | 仅 `bcm_err.h` |
| **msgq** | `architecture/msgq` | 异步消息队列：发送/接收/取消/完成通知、按 context 隔离命名空间 | `MSGQ_SendMsg` `MSGQ_RecvMsg` `MSGQ_CtxCreate` | lwq + osil |
| **srvrt** | `architecture/srvrt` | **服务运行时框架**：定义「服务」= 一张函数表(init/teardown/request/maintenance)；提供初始化、拆除、请求分发、事件循环 | `SRVRT_Init` `SRVRT_Teardown` `SRVRT_RequestService` `SRVRT_EventLoop` | msgq、**ipcdrv** |
| **ptu** | `architecture/ptu` | 分区表工具：解析/校验 flash 分区表头、分区项、辅助镜像信息（各核镜像在哪个 LUN/块，SYSC 系统配置从这读） | `PTU_GetAuxImgInfoById` 及各类型定义 | abstract |
| **utils** | `architecture/utils` | 测试框架与构建元信息（`testutils` 库）：测试用例注册、结果上报、版本信息 | `BCM_TestStart` `BCM_SetTestResult` | abstract、ptu |

> 耦合要点：`srvrt` 反向依赖了 `ipcdrv`（驱动层），因为服务请求可跨核，通过 IPC 送达——这是少数「基础设施依赖驱动」的例子。

### 5.2 base/ + cpu/arm/ —— OS 内核与 CPU 抽象

| 组件 | 目录 | 干什么 | 关键接口 | 依赖 |
|------|------|--------|----------|------|
| **base** | `base/` | 轻量抢占式 RTOS：任务(激活/终止/挂起)、事件(set/wait/clear)、计数器闹钟、ISR 钩子；`bcm_os.c` 的 `BCM_StartOS` 建任务起调度器；`lib/arch/armv7m/` 是 ARMv7-M 相关，含 **SVC 处理端** `BASE_ARMv7mSVCHandler` | `BASE_ActivateTask` `BASE_SetEvent/WaitEvent` `BASE_SetRelAlarm` `BCM_StartOS` `BCM_ExecHandover` | abstract、cpu、osil |
| **arm** | `cpu/arm/` | Cortex-M CPU 抽象：内存屏障、中断开关、cache 维护、MPU、周期计数、**SVC 触发端** `CPU_SvcRequest`(内联 `SVC #0`)。`os/base` 是走 SVC 的特权封装，`os/none` 是裸机版 | `CPU_SvcRequest` `CPU_MemoryBarrier` `CPU_GetPrivilegeLevel` `CORTEX_MX_*`(cache/mpu/中断) | abstract |

> 这两个组件 + `init/bcm8915x/chip/common/svc_handlers.c` 共同构成 SVC 三段式：**触发端(arm) → 接收端(base) → 分发表(init)**。

### 5.3 system/drivers/ —— 外设驱动层

| 驱动 | 干什么 | 关键接口 | 依赖 | 启动中的角色 |
|------|--------|----------|------|-------------|
| **mcudrv** | 芯片级控制：时钟、复位、PLL、低功耗；**`MCU_InitProcessor` 按核 ID 放各协处理核出复位**（MCU0/MCU1/A55/Q8，设入口点） | `MCU_Init` `MCU_ClkInit` `MCU_InitProcessor` `MCU_GetSecureBootStatus` | system、gpiodrv | ★核心：时钟与放核出复位 |
| **ddrdrv** (+ **dpfedrv**) | 初始化 DDR 与 PHY：`LlcInit` 配末级缓存并启动 DPFE（DDR PHY 固件引擎），`DpfeConfigure` 非阻塞下载 PHY 固件（轮询直到完成），**`DDRDRV_A55Init` 写 A55 reset vector 并放 A55 出复位** | `DDRDRV_LlcInit` `DDRDRV_DpfeConfigure` `DDRDRV_A55Init` | dpfedrv、mcu(取时间) | ★核心：DDR 初始化 + 拉起 A55 |
| **ipcdrv** | 跨核通信：请求/响应消息、服务注册回调、通道配置 | `IPCDRV_Init` `IPCDRV_SendMsg/RecvMsg` `IPCDRV_RegisterService` | mcudrv、bcm_msg | 被 srvrt 用作跨核传输 |
| **uartdrv** | 串口字节流收发（中断收、轮询查 TX），控制台/调试输出 | `UART_Init` `UART_Send` `UART_GetTxStatus` | osil、寄存器层 | 控制台输出 |
| **dbglog** (+ uart/jtag 后端) | 格式化日志 `BCM_Printf`，后端可插拔：UART 后端走串口，JTAG 后端非阻塞给调试器读 | `BCM_Printf` / `DBGLOG_BackendInit/SendBegin/Poll` | srvrt、uartdrv / jtag | 全程诊断输出 |
| **gpiodrv** | 引脚方向/电平/复用(alt func) 配置 | `GPIO_SetPinMode` `GPIO_SetAltFunc` `GPIO_SetPin/GetPin` | 近乎独立 | 外设引脚复用、放核信号 |
| **dmadrv** (dma_v2) | PL08x DMA 控制器：内存↔内存/外设搬运，多通道 | `DMA_Init` `DMA_StartXfer` `DMA_GetXferStatus` | 近乎独立 | 加速镜像/数据搬运 |
| **pciedrv** | PCIe 控制器 EP/RC 模式：BAR 映射、链路训练、配置空间读写 | `PCIE_EpInit` `PCIE_RcInit` `PCIE_GetLinkStatus` | 独立 | tandem boot / 设备枚举（可选） |

### 5.4 nvm/ —— 存储层（flash 全栈 + OTP）

flash 是一条清晰的**四级下沉栈**，上层只调 `flashsrv`：

```
flashsrv (服务:LUN/读写/健康, srvrt服务, 异步消息)
   │  brcmftl / emfileftl —— FTL 逻辑块↔物理块映射、磨损均衡
   │  brcmsfdp / emfilesfdp —— JEDEC SFDP 解析, 逻辑操作→芯片SPI命令序列
   ▼  flashspidrv —— 底层 SPI 协议(读/写/擦/状态, 1/2/4-lane)
```

| 组件 | 干什么 | 依赖 |
|------|--------|------|
| **flashsrv** | flash 高层服务：按名字查 LUN、读/写/裸读、设备信息，异步消息接口 | srvrt、mcudrv、brcmftl、emfileftl、emfilesfdp |
| **brcmftl / emfilelib / emfileftl** | 闪存转换层（FTL）：逻辑↔物理映射、磨损均衡；emfile 系为第三方 emFile 方案 | brcmsfdp / flashspidrv |
| **brcmsfdp / emfilesfdp** | SFDP：按 JEDEC 参数表把逻辑操作翻译成具体芯片的 SPI 命令 | flashspidrv |
| **flashspidrv** | 最底层 SPI 闪存协议驱动 | dmadrv |
| **otpdrv** | 一次性可编程存储读写（多版本布局 v1/v2/v4/v5），存设备密钥/熔丝 | 无 |
| **otpsrv** | OTP 的异步服务包装，解耦客户端与直接访问 | flashsrv、otpdrv |
| **secotpdrv** | 安全 OTP：在裸 OTP 上加机密性/认证层，存受保护密钥 | 无（底层用 otp） |
| **pchdrv** | patch 驱动：管理 flash 保留区里的补丁并在启动时应用 | 独立寄存器层 |

### 5.5 crypto/ —— 安全层（安全启动的核心）

分三档：**硬件驱动 → 调度/工具 → 高层服务**。

| 组件 | 档次 | 干什么 | 依赖 |
|------|------|--------|------|
| **crypto (abstract) / cryptotask** | 抽象 | 统一密钥/算法/哈希类型定义；`cryptotask` 跑异步 crypto 任务 | crypto、srvrt |
| **secutils** | 工具 | 大数运算库（Montgomery 模乘/模幂、bigint 比较加减），供 RSA 用 | 无（纯数学） |
| **pkadrv** | 硬件驱动 | 公钥加速器：RSA-2K/3K、ECC P256/P384、**Ed25519 验签**，异步 job 队列 | crypto、hsm_drv_sched、secutils |
| **skadrv** | 硬件驱动 | 对称密钥加速器：哈希、块加密、AEAD（CBC/CTR/GCM/CCM），基于 context | crypto、hsm_drv_sched |
| **csrbgdrv** | 硬件驱动 | NIST TRNG 真随机数（nonce） | 无 |
| **hsm_drv_sched** | 调度 | HSM job 调度库：pka/ska 的 job 队列与状态机 | lwq |
| **kstsrv** | 高层服务 | **密钥库**：取/注入/覆写/加载随机密钥，多后端(OTP/RAM/辅助镜像) | cryptotask、secutils、csrbgdrv、otpsrv |
| **ciphersrv** | 高层服务 | 批量加解密服务（AES-CBC/CTR），异步消息，内部调 skadrv | cryptotask、kstsrv、skadrv |
| **verifysrv** | 高层服务 | **Ed25519 签名校验服务**：对流式镜像分块摘要验签，安全启动认证的关键 | cryptotask、kstsrv、skadrv、pkadrv |

安全启动数据流：
- 验签：`verifysrv → pkadrv(Ed25519)`，公钥来自 `kstsrv → otpsrv/secotp`
- 解密：`ciphersrv → skadrv(AES)`，密钥来自 `kstsrv`
- 读镜像：`flashsrv → ftl → sfdp → spidrv`

### 5.6 init/ —— 编排 / 应用层（BootLoader 的「主程序」）

| 组件 | 目录 | 干什么 | 关键接口 | 依赖 |
|------|------|--------|----------|------|
| **bcm89150_init** | `init/bcm8915x` | 芯片相关一切的起点：`chip/common/base_startup.S` 复位入口、`base_early_init.c` 早期初始化(MPU/内存)、**`svc_handlers.c` 的 SVC 分发表 `CPU_SvcHandlerFuncTbl[]`**、链接脚本、`rom_entrypts`(ROM 入口桩) | `BCM8915X_EarlyInit` `BCM_OS_RESET_HANDLER` `BCM8915X_MpuSetup` | base、abstract、lwq、arm、secotpdrv、dbglog、flashsrv、bootsrv、syssrv |
| **bootsrv** | `init/bootsrv` | **整个 BL 最核心的编排逻辑** `BOOTSRV_TaskFunc`：读 SYSC 配置 → 初始化 DDR → 加载并放出 A55/MCU1 → PCIe/tandem → 加载 M7 主固件 → 拆运行时 → 交权 | `BOOTSRV_TaskFunc` `BOOTSRV_AlarmCallback` | ptu、mcudrv、ddrdrv、pciedrv、flashsrv、verifysrv、loader、syssrv、otpsrv |
| **loader** | `init/loader` | 镜像加载**流水线**：可插拔 stage 串联——flash 块读/线性读、Ed25519 验签、AES 解密、写目标内存，异步协程式推进 | `LOADER_PipeSetup*` `LOADER_PipeExecStart` `LOADER_PipeStageAdvance` `LOADER_SrvLoadFsImgReq` | abstract、ptu、verifysrv、flashsrv、ciphersrv |
| **syssrv** | `init/syssrv` | 轻量系统信息服务：版本号等元信息 | `SYSSRV_GetVersion` | systemtask |
| **systemtask** | `init/abstract` | 执行上下文：一个周期任务把 `SRVRT_EventLoop` 跑起来，驱动所有异步服务（loader/flash/verify…）协作推进 | `SYSTEM_TaskFunc` | srvrt |

> `bootsrv` 是耦合的汇聚点——它几乎依赖了每一层的关键组件，是「把各模块串成启动流」的那根线。`loader` 则把「数据来源(flash)」与「变换(验签/解密)」用流水线 stage 解耦，通过 srvrt 异步消息驱动。

---

## 6. 镜像加载与 flash 布局（M7/A55 镜像怎么喂给 BootLoader）

核心原则：**BootLoader 不写死镜像位置**。它先读一份 SYSC 系统配置拿到每个核镜像的坐标，再按 UUID 从 flash 文件系统读出、验签、放到内存。换镜像只需重新烧录 + 改配置，不用改 BL 代码。

### 6.1 运行时：BootLoader 怎么把镜像读出来喂给各核

**第 0 步 — 读 SYSC 系统配置**（`init/bootsrv/os/common/bootsrv.c:745`）

```c
PTU_GetAuxImgInfoById(PTU_AUX_IMG_ID_SYSC, 0UL, &auxImgInfo);
sysCfg = (const PTU_AuxImgSysCfgType *)auxImgInfo.ptr;
```

`PTU_AuxImgSysCfgType`（64 字节，`architecture/ptu/inc/ptu_aux_img_info.h:129`）带着每个核的坐标与开关：

```c
uint16_t a55AppLun;   uint16_t a55AppStartBlk;    // A55 镜像在哪个 LUN / 起始块
uint16_t MCU0AppLun;  uint16_t MCU0AppStartBlk;   // M7 主固件在哪
uint16_t MCU1AppLun;  uint16_t MCU1AppStartBlk;   // MCU1 在哪
uint32_t waitInBl;    // 非 0 则停在 BL 不交权（调试/烧录场景）
uint32_t pcieMode; uint32_t tandemBootEnable; ... // 其它系统开关
```

SYSC 数据放在 BL 镜像的 `.aux_img` 段（`init/bcm8915x/chip/common/firmware_common.ld:92`，`PTU_AuxImgHdr COMP_SECTION(".aux_img")`），内容由 oneui 的 `SYS:` 配置生成。**改配置改的是这份数据，不是固件逻辑。**

**第 1 步 — 按「LUN + 起始块 + UUID」加载各核镜像**

三个核共用 `BOOTSRV_LoadFsImg`（`bootsrv.c:285`）→ `LOADER_SrvLoadFsImgReq(lun, startBlk, uuid, ...)`：

| 核 | 触发点 | UUID | 加载后动作 |
|----|--------|------|-----------|
| **A55** | `BOOTSRV_BootA55(a55AppLun, a55AppStartBlk)` `bootsrv.c:357` | A55 专用 UUID | `DDRDRV_A55Init` 写 reset vector + 放 A55 出复位 |
| **MCU1** | `bootsrv.c:773` | `BOOTSRV_MCU1FwImgUuid` | `MCU_InitProcessor(MCU_PROCESSOR_ID_MCU1, entryPt)` 放出复位 |
| **M7 主固件 (MCU0)** | `bootsrv.c:799` | `BOOTSRV_MCU0FwImgUuid` | `SRVRT_Teardown()` → `BCM_ExecHandover(entryPt)` 交权退场 |

**第 2 步 — loader 内部按 UUID 查表 + 验签**（`init/loader/lib/loader_srv.c:2408`）

```c
for (i = 0; i < LOADER_UuidImgInfoCount; i++)
    if (memcmp(inParam->uuid, LOADER_UuidImgInfo[i].uuid, ...) == 0) {  // UUID → imgName + ptType
        FLASHSRV_ReadReqPost((uint8_t)inParam->lunNum, startBlk, ...);  // 从文件系统读
    }
...
if (memcmp(fsImgHdr->uuid, inParam->uuid, ...) != 0) { /* 拒绝：UUID 不匹配 */ }  // loader_srv.c:2450
```

读进来后走验签（A55 用 `KSTSRV_KEYHDL_A55_AUTH_KEY*` 密钥做 Ed25519 校验，`loader_srv.c:887`），验签失败就不放核出复位。

### 6.2 两条加载路径

| 路径 | 定位方式 | 谁用 | 特点 |
|------|----------|------|------|
| **文件系统镜像 (FS img)** | `LUN + 起始块 + UUID` | A55 / MCU1 / MCU0 | 灵活，走 FTL 文件系统 |
| **分区镜像 (partition)** | 分区类型 `PTU_PART_TYPE_FW` | M7 主固件的退路 | 传统方式，支持双副本 `ptCopy` |

当 SYSC 里 `MCU0AppLun` 未配置（≥ `FLASH_FTL_MAX_LUN_CNT`）时，M7 主固件退回走 `LOADER_SrvLoadPartitionReq(PTU_PART_TYPE_FW, ...)`（`bootsrv.c:811`），即传统 FW 分区。

### 6.3 烧录侧：镜像怎么进到 flash（oneui 负责）

flash 布局由 `oneui/gui/data/bcm8915x_bl_flash.yaml` 描述：

```yaml
BOOTLOADER:   BinFilePath: .../bcm89150_bl.bin   MaxSize: 0x3f000    # BL 镜像本身(含 SYSC)
LUN:          - Name: sda0  StartOffset: 0x100000  WearLevelEn: true  # 文件系统分区，各核镜像放这
SYS:          DbgLogIfcId: UART0  PCIeMode: None ...                  # → 生成 SYSC aux 配置
AUTHKEYS_A55: PemFilePath: .../ed25519_public_key_1.pem              # A55 验签公钥
DDR:          FwBinPath/McbBinPath: ...                              # DPFE 固件 + MCB 配置
```

oneui 脚本链：`img_signer.py`（Ed25519 签名）→ `flash_img_create.py` / `m7_flash_img.py`（打包成带 UUID 头的文件系统镜像）→ `loader_service.py` + `flasher_*.py`（通过 JTAG/QSPI 下载）。各核镜像签名后写进 LUN `sda0` 的对应起始块，`SYS:` 配置烧成 SYSC。

### 6.4 一图总览

```
oneui(flash.yaml)
   │ 打包 / 签名 / 烧录
   ▼
flash: [BL 镜像 (内含 SYSC .aux_img)] + [文件系统 LUN sda0: A55/MCU0/MCU1 镜像(带 UUID + 签名)]
                        │
上电 ──► BL 读 SYSC(.aux_img) 拿到 <LUN, 起始块>
     ──► 按 UUID 从 LUN 读镜像 ──► Ed25519 验签 ──► 放核出复位 / 交权
```

关键点：**位置配置驱动（SYSC）而非写死；身份靠 UUID 匹配而非固定地址；每个镜像带签名，验签通过才放核。**

---

## 7. 模块耦合关系（依赖图）

箭头 `A → B` 表示「A 依赖 B」（数据来自各 `comp.mk` 的 `BRCM_COMP_DEPENDS`）：

```
bcm89150_init ──┬─► bootsrv ──┬─► loader ──┬─► verifysrv ─► pkadrv ─► hsm_drv_sched ─► lwq
                │             │            ├─► ciphersrv ─► skadrv ──► hsm_drv_sched
                │             │            ├─► flashsrv ──┬─► brcmftl ─► brcmsfdp ─► flashspidrv ─► dmadrv
                │             │            │              └─► emfileftl ─► emfilelib
                │             │            └─► ptu ─► abstract
                │             ├─► ddrdrv ─► dpfedrv
                │             ├─► mcudrv ─► gpiodrv
                │             ├─► pciedrv
                │             ├─► syssrv ─► systemtask ─► srvrt ─► msgq ─► lwq
                │             └─► otpsrv ─► flashsrv
                ├─► secotpdrv
                ├─► dbglog ─► srvrt   (+ 后端 uartdrv / jtag)
                ├─► base ─► arm ─► abstract
                └─► verifysrv/kstsrv ─► otpsrv / secutils / csrbgdrv

srvrt ─► ipcdrv ─► mcudrv          (服务框架跨核传输)
kstsrv ─► cryptotask ─► crypto + srvrt
```

关键耦合结论：
1. **`abstract` 是万物之底**，几乎每条依赖链最终都落到它。
2. **`srvrt`（服务运行时）是安全 + 存储所有 `*SRV_` 组件的公共骨架**，而它自己又依赖 `ipcdrv`——这是唯一「基础设施反依赖驱动」的地方，因为服务请求可能跨核。
3. **`flashsrv` 是存储层的唯一入口**，其下四级栈对上完全隐藏。
4. **`bootsrv` 是纵向汇聚点**，`verifysrv/ciphersrv` 是安全横切点（被 loader 调用）。
5. **crypto 高层服务(verify/cipher/kst) 都建在 hsm 硬件驱动(pka/ska) + 密钥库(kst) 之上**，三层清晰。

---

## 8. 构建系统（build/）与产物目录

- **`build/main.mk` → `common.mk`**：入口，扫描各组件 `comp.mk`，按 `BRCM_ENABLED_COMP` 和依赖关系决定编哪些库。BootLoader 目标由 `build/bootloader.mk` + `build/bcm8915x/bcm8915x_bl.mk` 定义，入口符号 `BCM_OS_RESET_HANDLER`，`OS = base`。
- **`build/bcm8915x/bcm8915x.mk`**：列出 BootLoader 实际启用的组件清单（`BRCM_ENABLED_COMP += ...`）和基本依赖 `BRCM_CHIP_BASIC_DEPENDS`。想知道「哪些组件进了固件」看这里。
- **`build/rules/*_gnu_rules.mk`**：各架构(m7/arm64/r4/mips/x86)的编译链接规则；`build/compiler/*`：GNU/GHS/RVCT 编译器抽象。
- **`build/keys/`**：Ed25519 签名用的公私钥对（验签链的一端）。
- **`build/common/*.ld`、`init/bcm8915x/chip/common/*.ld`**：链接脚本，定内存布局。
- **产物目录**：`obj/`（中间 .o 和静态库）、`out/`（最终 ELF/bin/map）、`prebuilt/`（闭源预编译库：DPFE、emFile FTL 的 `.a`，以及 RDB 寄存器头）。

---

## 9. 配置生成体系（comp.mk → CSV → cgen 源码）

BootLoader 里有一大类 `.c` **不是手写的，而是构建时由 Python 脚本按各组件 `comp.mk` 的声明生成**：任务表、事件/闹钟/中断表、SVC 分发表、服务运行时配置（含跨核 IPC 表）。改这些行为要改 `comp.mk`，**不要改生成出来的 `.c`（每次 build 会被覆盖）**。

### 9.1 生成链

```
各组件 comp.mk 的 BRCM_COMP_REGISTER_* 声明
   │  build 汇总（brcm_generate_app_cfg，见 build/bootloader.mk）
   ▼
obj/<target>/app_cfg/csv/*.csv        中间配置（'|' 分隔，列头带类型注解）
   │  build/common/genappcfg.py 按依赖拓扑排序，逐个调对应生成脚本
   ▼
obj/<target>/app_cfg/cgen/{inc,src}/*.{h,c}    最终参与编译的配置源码
```

`genappcfg.py` 的入口约定（`app_cfg_top.csv`）：每种配置声明 `pymodule`(生成脚本) + `pyfunc`(函数) + `deps`(依赖哪张表先生成) + 产物头/源文件。关键约束：`syscfg["cpuid"]` **写死为 0**、`--cpu` 只接受 `m7`——即这套配置本就是**按 M7 单核**出的。

### 9.2 有哪些表、谁生成

| CSV（regcfg） | 生成脚本 : 函数 | 产物 | 内容 |
|------|------|------|------|
| `task_registry` | `base/scripts/gen_base_cfg.py:gen_task_cfg` | `base_task_cfg.c` `base_app_cfg.h` | 任务表（`BRCM_COMP_REGISTER_TASK`） |
| `event_registry` | 同上 `:gen_event_cfg` | `base_event_cfg.h` | 事件位（`REGISTER_EVENT`） |
| `alarm_registry` | 同上 `:gen_alarm_cfg` | `base_ctr_alarm_cfg.c` | 闹钟（`REGISTER_ALARM`） |
| `counter_registry` | 同上 `:gen_counter_cfg` | `base_counter_cfg.h` | 计数器 |
| `intr_registry` | 同上 `:gen_intr_cfg` | `base_intr_cfg.c` | 中断向量 |
| `svc_handler_registry` | 同上 `:gen_svc_handler_cfg` | `base_svc_cfg.c` | SVC 分发表 |
| `service_registry` | `architecture/srvrt/scripts/gen_srvrt_cfg.py:gen_srvrt_cfg` | `srvrt_cfg.c` | 服务运行时 + **跨核 IPC 表** |

### 9.3 服务注册：从 comp.mk 到 srvrt_cfg.c

每个 srvrt 服务在自己组件的 `comp.mk` 里用 `BRCM_COMP_REGISTER_SERVICE` 声明（例：`nvm/drivers/flash/flashsrv/comp.mk:99`）：

```makefile
BRCM_COMP_REGISTER_SERVICE  += FLASHSRV
FLASHSRV.srvid              := BCM_SRV_ID_FLASH
FLASHSRV.cpuid              := BCM_MSG_EP_CPU_ID_LOCAL   # 服务跑在哪个核
FLASHSRV.taskid             := FLASHSRV_Task             # 挂在哪个任务的事件循环
FLASHSRV.num_slots          := 1
FLASHSRV.func_tbl           := FLASHSRV_ServiceFuncTbl   # 服务函数表符号
```

这些汇总成 `service_registry.csv`，列头声明了每个字段的类型：

```
name|srvid|cpuid|taskid|num_slots|queue_depth|oneway|queue_flush_on_teardown
    |ipcch:list[struct[cpuid:int,chidx:int]]|grant_access:list[str]|func_tbl|declaration
```

`gen_srvrt_cfg.py` 据此生成 `srvrt_cfg.c` 里的 `SRVRT_EventLoopInfos[]`（每个任务的事件循环 + 其下服务的消息队列/waker/内存池）和 `BCM_MsgEpTbl`（服务 ID → 端点）。

### 9.4 跨核 IPC 表：为什么是空的、怎么配

`srvrt_cfg.c` 末尾的 `IPCDRV_GlobalCfg` / `IPCDRV_TxTbl` / `IPCDRV_RxTbl` **完全由服务的 `ipcch`（IPC 通道）和 `grant_access`（跨核授权）两个字段驱动**：

- `IPCDRV_GlobalCfg`（`gen_srvrt_cfg.py:403`）：遍历所有服务的 `ipcch`，无则不生成
- `IPCDRV_TxTbl`（:414）：某服务 `grant_access` 了一个**跑在别的 CPU** 上的服务才生成发送项
- `IPCDRV_RxTbl`（:452）：某 exec `len(ipcch) > 0` 才生成接收项

当前 BL 的 `service_registry.csv` 里 **8 个服务的 `ipcch`/`grant_access` 全空，`cpuid` 全 `LOCAL`（=M7 自己）**，所有服务在本核走本地 msgq，没有跨核 RPC → **三张表为空是正常结果，不是缺陷**。IPC 表是给「服务分布在多核」的场景准备的。

真要启用跨核 IPC，在对应服务 `comp.mk` 里补字段（不要改生成的 .c）：

```makefile
# 接收端：允许 cpuid=1 的核通过通道 0 向本服务发请求 → 生成 RxTbl + GlobalCfg
FLASHSRV.ipcch          := {cpuid=1,chidx=0}
# 发起端：某服务要调远程核上的 FLASHSRV → 生成 TxTbl
SOMESRV.grant_access    := FLASHSRV
```

脚本内置三条校验（`gen_srvrt_cfg.py:361-401`），不满足会 build 报错：
1. 服务不能给「自己所在 CPU」配 ipcch
2. 同一 `chidx` 不能连不同的 CPU 对
3. `grant_access` 远程服务时，发起核必须有对应 `ipcch` 通道，否则报 `No IPC channel found`

> 前提：这套 BL 配置 `cpuid` 写死 0、单核出。真正的多核 IPC 拓扑属于完整 SDK 里的多核 app，不是这个纯 BL。

---

## 10. 工具（tools/）

- **`tools/common/rdbgen`**：从寄存器描述(RDB/xlsx/xml)生成 C 头文件与 Python 库的代码生成器。`prebuilt/rdb/` 和各驱动引用的 `*_rdb.h` 就是它的产物。
- **`tools/common/vcast`**：VectorCAST 测试集成（覆盖率/资格测试）配套。

---

## 附注：oneui/（不在本文范围）

`oneui/` 是一个**独立于固件的 PC 端 Python GUI 工具**（`uv run oneui/gui/main.py` 启动），用于生成芯片配置、组织镜像、通过 JLink 烧录调试。它不参与固件编译链接，与上面所有固件模块不共享代码，只在「产出配置/镜像喂给 BootLoader」这一层面间接相关，故本文略去。

---

## 快速索引：我想改 X，该看哪

| 我想… | 去看 |
|-------|------|
| 改启动流程/加载顺序 | `init/bootsrv/os/common/bootsrv.c : BOOTSRV_TaskFunc` |
| 改镜像验签/解密 | `init/loader/lib/loader_pipe.c` + `crypto/drivers/verifysrv`、`ciphersrv` |
| 改各核镜像位置/flash 布局 | 见第 6 节；`oneui/gui/data/bcm8915x_bl_flash.yaml`（SYS/LUN），运行时 `bootsrv.c` 读 SYSC |
| 加/改一个特权操作 | `cpu/arm`(触发) + `base/lib/arch/armv7m`(接收) + `init/bcm8915x/chip/common/svc_handlers.c`(分发表) |
| 换 flash 芯片/调 flash | `nvm/drivers/flash/`（改栈的对应层：srv/ftl/sfdp/spidrv） |
| 改 DDR / 拉起 A55 | `system/drivers/ddrdrv`（`DDRDRV_A55Init`） |
| 放其它核出复位 | `system/drivers/mcu`（`MCU_InitProcessor`） |
| 加一个后台服务 | 用 `architecture/srvrt` 注册服务函数表，参考现有 `*SRV_`；服务注册字段见第 9 节 |
| 配跨核 IPC / 改任务事件表 | 见第 9 节；改对应组件 `comp.mk` 的 `REGISTER_SERVICE`/`ipcch`/`grant_access`，别改生成的 `srvrt_cfg.c` |
| 改日志输出 | `system/drivers/dbglog`（换后端 uart/jtag） |
| 改编译/链接/组件开关 | `build/bcm8915x/bcm8915x.mk` + 目标组件的 `comp.mk` |
