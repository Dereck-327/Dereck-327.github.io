# BCM8915x BootLoader

面向第一次接触本仓库的开发者。目标：用最短路径搞懂「这颗芯片是怎么从上电跑到应用固件的」，以及代码是怎么组织的。

---

## 0. 先建立大局观

BCM8915x 是一颗**多核 SoC**：

| 核 | 角色 |
|----|------|
| **Cortex-M7** | 引导核。**本仓库的代码就跑在它上面**（BootLoader） |
| **Cortex-A55** ×N | 应用核（跑大系统 / Linux 等） |
| **MCU0 / MCU1** | 其它 M 系列协处理核 |

BootLoader 的使命：上电后把自己跑起来 → 初始化 DDR → 依次从 flash 加载各个核的镜像并**放它们出复位**（release from reset）→ 最后加载 M7 主应用固件并**把控制权交出去（handover）**，自己退场。

一句话记住整条链（细节见第 2 节）：

```
上电 → 复位向量 → EarlyInit → 启动 RTOS → BOOTSRV_TaskFunc
   → 初始化 DDR → 加载并启动 A55 → 启动 MCU1 → 加载 M7 主固件 → 跳转交权
```

---

## 1. 目录地图

只列学习阶段最该关注的。其余（crypto/nvm/oneui/tools）用到再看。

| 目录 | 是什么 | 优先级 |
|------|--------|--------|
| `init/bcm8915x/chip/common/` | **启动汇编 + 芯片早期初始化 + SVC 分发表**。一切从这里开始 | ⭐⭐⭐ |
| `init/bootsrv/` | **Boot Service**：加载镜像、拉起各核的主逻辑 | ⭐⭐⭐ |
| `init/loader/` | Loader 服务：真正从 flash 分区/文件系统读镜像、校验 | ⭐⭐ |
| `base/` | OS 抽象层、RTOS 启动(`bcm_os.c`)、ARMv7-M 架构相关(`lib/arch/armv7m/`) | ⭐⭐ |
| `cpu/arm/` | CPU 抽象：cache、MPU、中断、SVC 请求封装 | ⭐⭐ |
| `system/drivers/` | 外设驱动（uart / gpio / dma / ddr / pcie / mcu …） | ⭐ 用到再看 |
| `architecture/` | 通用基础设施：消息队列(msgq)、轻量队列(lwq)、srvrt 运行时、ptu 分区表工具 | ⭐ |
| `build/` | Makefile 片段、链接脚本、编译器配置 | 编译报错时看 |

---

## 2. 推荐阅读顺序（跟着启动流程走）

按下面顺序读文件，就是跟着芯片真实的执行流走一遍。**不要一上来啃驱动**。

### 第 1 站：复位与启动汇编
`init/bcm8915x/chip/common/base_startup.S`

看 `BCM_OS_RESET_HANDLER`（复位入口）做了什么：
1. 设置向量表偏移寄存器 VTOR (`0xE000ED08`)
2. 涂抹（paint）栈和 BSS —— `0xA5A5A5A5` 是栈用量检测的水位标记
3. `MSR MSP` 设主栈 → `BCM8915X_EarlyInit`（芯片底层初始化：MPU、内存）
4. 使能中断、加 MPU 栈保护
5. `CORTEX_MX_SetThreadMode` **切到非特权线程模式 + PSP**（记住这一步，第 4 站会用到）
6. `BCM_StartOS` 把控制权交给 RTOS

### 第 2 站：RTOS 启动
`base/lib/bcm_os.c` 里的 `BCM_StartOS`

大致看 RTOS 怎么建任务、起调度器。**不用深挖**，知道它最终会调度运行 Boot Service 的任务函数即可。

### 第 3 站：核心 —— Boot Service 主逻辑 ⭐
`init/bootsrv/os/common/bootsrv.c` 的 `BOOTSRV_TaskFunc()`

这是整个仓库最该精读的一个函数。它顺序执行：
1. `PTU_GetAuxImgInfoById(...SYSC...)` 读系统配置（各核镜像在哪个 LUN、起始块、各种开关）
2. `BOOTSRV_InitDDR()` 初始化 DRAM 控制器
3. `BOOTSRV_BootA55()` → 加载 A55 镜像 → `DDRDRV_A55Init` 设 reset vector 并放 A55 出复位
4. `MCU_InitProcessor(...MCU1...)` 启动 MCU1
5. `BOOTSRV_PcieInit()` PCIe / tandem boot
6. 加载 M7 主固件 → `SRVRT_Teardown()` 拆运行时 → `BCM_ExecHandover(entry)` **跳转交权，不返回**

> 例外：`waitInBl` 为真（调试/烧录场景）时停在 bootloader，不跳转。

### 第 4 站：搞懂 SVC 系统调用机制 ⭐
这是本代码库最重要的一个「机关」，不懂它读驱动会处处卡壳。

- 触发端：`cpu/arm/lib/cpu_cortex.c` 的 `CPU_SvcRequest()` — 一行内联汇编 `SVC #0`
- 处理端：`base/lib/arch/armv7m/armv7m.c` 的 `BASE_ARMv7mSVCHandler()`
- 分发表：`init/bcm8915x/chip/common/svc_handlers.c` 的 `CPU_SvcHandlerFuncTbl[]`

**为什么需要它**：第 1 站结尾把系统降到了**非特权线程模式**。非特权代码不能直接碰特权资源（cache 维护、MPU、跳转交权等）。于是驱动/服务想做特权操作时，填一个请求结构体，用 `SVC #0` 陷入特权 Handler 模式，由分发表按 ID 调对应的特权服务函数，做完再返回非特权态。

链路：
```
非特权代码
  → CPU_SvcRequest(id, &io)        // r0=id, r1=&io, 执行 SVC #0
    → [硬件] 压栈帧, 切特权Handler模式, 跳向量表SVCall槽
      → BASE_ARMv7mSVCHandler(id, io)   // r0/r1 直接变成C参数
        → CPU_SvcCmdHandler → CPU_SvcHandlerFuncTbl[id](...)  // 查表调特权服务
      → 异常返回, 回到非特权线程态
```

> 例：`BCM_ExecHandover`（跳转到新固件）内部就是走这条 SVC 路径（`BCM_OS_CMD_EXEC_HANDOVER`），因为「替换掉自己」是特权操作。

### 第 5 站：镜像从哪来
`init/loader/lib/loader_srv.c` 的 `LOADER_SrvLoadFsImgReq` / `LOADER_SrvLoadPartitionReq`

看镜像是如何从 flash 分区 / 文件系统按 UUID 读出、校验、放到内存的。搭配 `architecture/ptu/`（分区表工具）一起看。

---

## 3. 读这类嵌入式代码的几个技巧

- **命名前缀即模块**：`BOOTSRV_` = boot service，`LOADER_` = loader，`CPU_` = cpu 抽象，`DDRDRV_` = DDR 驱动，`CORTEX_MX_` = Cortex-M 架构，`BCM_` = OS/公共。看前缀就知道去哪个目录找。
- **`os/base` vs `os/none`**：很多驱动有两套实现。`base` = 有 RTOS 的版本（走 SVC / 消息队列），`none` = 裸机版本。当前 bootloader 用哪套看编译配置。
- **`@trace #BRCM_SW...` 注释**：需求/设计追溯标签（安全认证用），阅读时可忽略，别被它们淹没。
- **`COMP_ASM` 等 `COMP_` 宏**：编译器抽象层，定义在 `architecture/abstract/inc/compiler.h`。`COMP_ASM` = `__asm volatile`。
- **`.venv`、`obj/`、`out/`、`.cache/`**：生成物 / 环境目录，搜索时跳过。

---

## 4. 动手：先让它编起来

（详见根目录 `README.md`，这里给最短路径）

```bash
uv venv && source .venv/bin/activate
uv pip install -r requirements.txt
make family=bcm8915x bl TOOLCHAIN_PATH=/opt/toolchains/arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi
```

编出来后：
- 用 `compile_commands.json` 配合 clangd，让编辑器能跳转定义（读代码神器）。
- 从 `BOOTSRV_TaskFunc` 开始，用「跳转到定义」一层层往下点，比通读快得多。

---

## 5. 关键点

1. 上电后第一条执行的代码在哪个文件、哪个符号？
2. 为什么启动最后要切到「非特权线程模式」？带来了什么约束？
3. 一个非特权驱动想做 cache flush（特权操作），要走什么机制？涉及哪 3 个文件？
4. `BOOTSRV_TaskFunc` 一共拉起了哪几个核？各自的镜像从哪读、怎么放出复位？
5. BootLoader 跳到 M7 主固件前做了哪两步收尾？为什么跳转要走 SVC？

<details>
<summary>参考</summary>

1. `init/bcm8915x/chip/common/base_startup.S` 的 `BCM_OS_RESET_HANDLER`。它是复位向量指向的符号，位于 `.reset_func` 段，芯片上电取复位向量后跳到这里。

2. 为了**权限隔离 / 故障域收窄**：应用与服务代码跑在非特权态，碰不到特权资源就无法误改关键硬件，配合 MPU 还能拦栈溢出。约束是——任何特权操作（cache 维护、MPU 配置、跳转交权、访问受限寄存器）都不能直接做，必须通过 `SVC #0` 陷入特权 Handler 模式代为执行。

3. 走 **SVC 系统调用**机制。三个文件：
   - `cpu/arm/lib/cpu_cortex.c` — `CPU_SvcRequest()` 触发 `SVC #0`
   - `base/lib/arch/armv7m/armv7m.c` — `BASE_ARMv7mSVCHandler()` 接住并分发
   - `init/bcm8915x/chip/common/svc_handlers.c` — `CPU_SvcHandlerFuncTbl[]` 按 ID 查表调具体特权服务
   （cache 具体命令封装在 `cpu/arm/os/base/cache_osil.c`，填 `sysReqID=CPU_SVC_DCA_ID` 后走上面这条链）

4. 三类核：
   - **A55 应用核**：`BOOTSRV_BootA55` → `LOADER_SrvLoadFsImgReq` 按 UUID 从 flash 文件系统加载 → `DDRDRV_A55Init` 把入口写进 A55 reset vector 并放出复位。
   - **MCU1**：加载 `MCU1` 镜像 → `MCU_InitProcessor(MCU_PROCESSOR_ID_MCU1, ..., entryPt, opt)` 设入口并释放。
   - **M7 主固件**（最后一步，见第 5 题）：加载 `MCU0`/FW 分区镜像，最终跳转交给它。
   镜像位置都来自 `SYSC` 系统配置里的 `LUN` + 起始块。

5. 两步收尾：`SRVRT_Teardown()`（拆掉 bootloader 的服务运行时）→ `BCM_ExecHandover(entryPt)`（跳转，不返回）。跳转走 SVC 是因为「替换掉当前正在运行的自己」（改栈、改 VTOR、丢弃上下文）是特权操作，而 `BOOTSRV_TaskFunc` 跑在非特权线程态，只能通过 `BCM_OS_CMD_EXEC_HANDOVER` 这个 SVC 命令陷入特权态完成。

</details>

---

## 5.5 启动时序图（上电 → 交权）

标注了每一步对应的 `文件 : 符号`，读代码时对照着走。

```
   ┌──────────────┐
   │  上电 / 复位  │  硬件从向量表取复位向量
   └──────┬───────┘
          ▼
 [1] BCM_OS_RESET_HANDLER            base_startup.S : BCM_OS_RESET_HANDLER
     · 设 VTOR 向量表偏移
     · paint 栈 / BSS (0xA5A5A5A5 水位)
     · MSR MSP 设主栈
          │
          ▼
 [2] BCM8915X_EarlyInit              init/bcm8915x/chip/common/  (MPU / 内存底层初始化)
          │
          ▼
 [3] CORTEX_MX_SetThreadMode         切到「非特权线程模式 + PSP」★ 之后特权操作都得走 SVC
          │
          ▼
 [4] BCM_StartOS                     base/lib/bcm_os.c : BCM_StartOS   (建任务 + 起 RTOS 调度器)
          │
          ▼  RTOS 调度运行 boot service 任务
 [5] BOOTSRV_TaskFunc                init/bootsrv/os/common/bootsrv.c : BOOTSRV_TaskFunc
     │
     ├─(a) 读系统配置 SYSC          PTU_GetAuxImgInfoById(...SYSC...)   → 各核镜像 LUN/块/开关
     │
     ├─(b) 初始化 DDR               BOOTSRV_InitDDR → DDRDRV_LlcInit / DpfeConfigure
     │                              成功 → memType |= MEM1 (可加载进 DRAM)
     │
     ├─(c) 启动 A55 应用核          BOOTSRV_BootA55
     │        · LOADER_SrvLoadFsImgReq  ── flash 文件系统按 UUID 加载 ──▶ entryPoint
     │        · BCM_Map2GlobalAddr      本地地址 → 全局地址
     │        · DDRDRV_A55Init          写 reset vector + 放 A55 出复位 ▶▶ A55 开跑
     │
     ├─(d) 启动 MCU1                加载 MCU1 镜像 → MCU_InitProcessor(MCU1,...) 释放 ▶▶ MCU1 开跑
     │
     ├─(e) PCIe / tandem boot        BOOTSRV_PcieInit
     │
     └─(f) 加载 M7 主固件并交权
              · LOADER 加载 MCU0/FW 分区镜像 → ptInfo.imgEntryPt
              · 若 waitInBl 为真 或 A55 占用 SRAM(MEM0) → "Skipping Application"（停在 BL）
              · 否则：
                   SRVRT_Teardown()          拆 bootloader 运行时
                   BCM_ExecHandover(entry)   ──┐
                                               │  内部走 SVC（特权操作）
                                               ▼
              [SVC 路径] CPU_SvcRequest("SVC #0")   cpu/arm/lib/cpu_cortex.c
                   → [硬件压栈帧, 切特权 Handler 模式, 跳向量表 SVCall 槽]
                   → BASE_ARMv7mSVCHandler          base/lib/arch/armv7m/armv7m.c
                   → CPU_SvcHandlerFuncTbl[EXEC_HANDOVER]   svc_handlers.c
                   → 完成 M7 自我替换跳转 ▶▶▶ 主固件接管，BootLoader 退场（不返回）
```

★ 记住第 [3] 步：正是这一步的非特权降级，导致后面 (c)/(d)/(f) 里所有特权动作都必须绕道 SVC。

---

## 6. 下一步深入方向（按兴趣挑）

- **加载与安全**：`init/loader/` + `crypto/`（镜像签名校验）
- **A55 拉起细节**：`system/drivers/ddrdrv/` 的 `DDRDRV_A55Init`（reset vector 寄存器操作）
- **交权细节**：`svc_handlers.c` 里 `BCM_OS_CMD_EXEC_HANDOVER` 对应的 handler，看 M7 如何自我替换跳转
- **RTOS 内核**：`base/` + `architecture/msgq`、`architecture/srvrt`
