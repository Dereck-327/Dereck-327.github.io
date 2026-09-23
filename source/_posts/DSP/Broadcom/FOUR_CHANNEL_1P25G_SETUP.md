# BCM8915X 四路独立 1.25G ADC/Q8 配置说明

本文说明将原来的“四路交叉采样 5G”工程改为“四路独立 1.25G 采样”的完整配置方法。适用工程如下：

```text
MCU1 RTOS:  LidarRTOSapp_0_1/apps/lidar_rtos_app
Q8 DSP:     LidarQ8App_0_1
A55:        A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files
Bootloader: BCM8915X_BL_REL_1.5_src
```

这份说明只使用工程代码、`.claude/skills/bcm8915x-docs/SKILL.md`、
`doc/Technical_ReferenceManual.txt` 和 bootloader reference manual 中已经确认的内容。文末会单独列出尚未完成的硬件或工具链验证。

## 1. 目标配置

目标数据路径为：

```text
ADC0 -> ADC group 0 -> Q8_0 -> bufferInfo[0]
ADC1 -> ADC group 1 -> Q8_1 -> bufferInfo[1]
ADC2 -> ADC group 2 -> Q8_2 -> bufferInfo[2]
ADC3 -> ADC group 3 -> Q8_3 -> bufferInfo[3]
                              |
                              +-> MCU1 等待四路完成
                              +-> MCU1 合并为一条消息发送给 A55
                              +-> A55 校验四路地址并发送四个 UDP 数据包
```

四个 Q8 使用不同的：

- Q8 core ID：0、1、2、3；
- ADC group/read buffer；
- Q8 image load address；
- bootloader core mask；
- A55 发布给 MCU1/Q8 的输出 buffer；
- BUF_DONE 消息中的发送者标识。

单个 Q8 完成不能结束一次采集。一次采集序列只有在 Q8_0、Q8_1、Q8_2、Q8_3 都报告相同 sequence 后，MCU1 才向 A55 发布一条聚合消息。

本次实现涉及的主要文件：

```text
MCU1:
  LidarRTOSapp_0_1/apps/lidar_rtos_app/include/ipc_helper.h
  LidarRTOSapp_0_1/apps/lidar_rtos_app/src/acq_task.c
  LidarRTOSapp_0_1/apps/lidar_rtos_app/src/ipc_msg.c

Q8:
  LidarQ8App_0_1/lidar_q8_app.h
  LidarQ8App_0_1/lidar_q8_app_raw.c
  LidarQ8App_0_1/Makefile
  LidarQ8App_0_1/linker_scripts/rigel_lsp_q0..q3/

A55:
  A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/include/config.h
  A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/src/cmd.c
  A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/src/config.c
  A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/src/dsp_data_resp.c
  A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/src/ipc_resp.c
  A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/config.ini

Bootloader:
  BCM8915X_BL_REL_1.5_src/init/bcm8915x/scripts/m7_flash_img.yaml
```

## 2. 参考依据

### 2.1 TRM 依据

`doc/Technical_ReferenceManual.txt` 中使用到的章节和行号如下。行号是当前工作区文本版手册的行号，手册版本变化后应重新搜索确认。

| 结论                                                             | TRM 位置                                                                   |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------- |
| 8K、1.25G、4 通道的最小 trigger-to-trigger duration 为 6569.6 ns | §19.1.3.3，当前文本约 362171-362262 行                                    |
| ADC group 地址为`0x00600000 + N * 0x40000`                     | §19.1.4，当前文本约 362455-362581 行                                      |
| `SAMPLING_MODE=00` 为 1.25G，`SAMPLING_MODE=10` 为 5G        | §19.9.1.2.7，当前文本约 369546-369592 行                                  |
| 1.25G 模式每通道支持最多 8K samples                              | §19.1.3，当前文本约 362054-362058 行                                      |
| Q8/DSP 与 ADC group 是一一对应的数据路径                         | §12.1，当前文本约 107403-107408 行；§12.5.4，当前文本约 109243-109247 行 |
| ADC buffer 读完后需要访问`RD_DONE_ADDR` 完成 buffer 状态转换   | §19.1.3.1、§19.9.1.2.10                                                  |

由 TRM 的地址公式得到本方案的 Q8/ADC 映射：

| Q8   | core ID | ADC group | ADC group base | 采集 read buffer |                        FFT read buffers |
| ---- | ------: | --------: | -------------: | ---------------: | --------------------------------------: |
| Q8_0 |       0 |         0 | `0x00600000` |     `+0x20000` | `+0x30000/+0x34000/+0x38000/+0x3C000` |
| Q8_1 |       1 |         1 | `0x00640000` |     `+0x20000` | `+0x30000/+0x34000/+0x38000/+0x3C000` |
| Q8_2 |       2 |         2 | `0x00680000` |     `+0x20000` | `+0x30000/+0x34000/+0x38000/+0x3C000` |
| Q8_3 |       3 |         3 | `0x006C0000` |     `+0x20000` | `+0x30000/+0x34000/+0x38000/+0x3C000` |

注意：TRM 的系统地址空间总览还列出了 ADC 外设窗口的 256 KiB 区域；Q8 代码实际访问采集数据时应遵循 §19.1.4 的 acquisition buffer 映射和动态 read-buffer 规则，不能只根据总览表推导 Q8 read address。

### 2.2 IPC 依据

bootloader reference manual 的 IPC message exchange 章节说明：发送端先写共享消息，再写 doorbell；接收端处理消息后清除 doorbell；一个 doorbell channel 同时只能保留一个 message，发送端必须等待 channel 清零后再发下一条消息。当前文本约 47978-48010 行。

因此本方案让四个 Q8 共享 channel 4，但每次发送前等待 channel 4 清零；MCU1 收到消息后先 acknowledge channel 4，再校验消息内容，避免非法、重复或过期消息卡住后续采集。

## 3. MCU1 RTOS 配置

修改目录：

```text
LidarRTOSapp_0_1/apps/lidar_rtos_app
```

### 3.1 ADC 变为四路独立 1.25G

`src/acq_task.c` 的默认配置为：

```c
numChannels   = 4;
adcSampleFreq = LIDAR_ADC_SAMPLE_FREQ_1_25G;
adcSampleSize = 8 * 1024;
adcSelect     = 0xF;       /* ADC0..ADC3 */
```

启动时调用 `HSADCH_FullInit()` 和 `HSADCH_ConfigFirFilter()`，传入四路选择值，使 ADC0、ADC1、ADC2、ADC3 同时初始化。不要只初始化 ADC0，也不要把 Q8_1..Q8_3 指向 ADC0 的 read buffer。

### 3.2 采集间隔

TRM 表 3418 对 8K samples、1.25G、4 channels 给出的最小值是：

```text
6569.6 ns = 6.5696 us
```

工程使用整数微秒配置，因此设置为：

```text
ACQIntervalUs = 7
```

代码默认值也为 `7UL`。这是基于 TRM 最小值向上取整后的保守值，不是从原来的 5G 配置直接沿用。

### 3.3 统一 BUF_DONE 消息

Q8 到 MCU1 的 channel 4 使用一个统一 command，core ID 和 sequence 编码在 command 中：

```text
base       = 0xE6100000
core_id    = bits [9:8]
sequence   = bits [19:10]
doorbell   = command | 0x3
```

对应宏位于 `include/ipc_helper.h` 和 Q8 的 `lidar_q8_app.h`：

```c
IPC_CMD_BUF_DONE(core, seq)
IPC_CMD_BUF_DONE_DOORBELL(core, seq)
```

sequence 是 10 bit，不是 12 bit。原因是 command base 的 bit 20 及以上必须保持固定；bit [10:19] 可提供 1024 个序号，代码使用 modulo 1024 的半范围比较判断新旧消息。

该格式把原本需要分别处理 BUF_DONE、BUF_DONE2、BUF_DONE3 的路径统一为一个解析器：

```text
core_id=0 -> Q8_0 / bufferInfo[0]
core_id=1 -> Q8_1 / bufferInfo[1]
core_id=2 -> Q8_2 / bufferInfo[2]
core_id=3 -> Q8_3 / bufferInfo[3]
```

### 3.4 四路完成状态和 frame/line 规则

`src/ipc_msg.c` 维护：

```c
uint32_t completeMask;
uint32_t completeSeq[4];
uint32_t aggregateChanged;
```

处理规则：

1. 收到 channel 4 消息后先清除/ack doorbell。
2. 校验 command 固定部分、core ID 和对应 `bufferInfo[coreId].addr`。
3. 记录 `completeSeq[coreId]`，并设置 `completeMask |= 1U << coreId`。
4. 只有 `completeMask == 0xF` 且四个 sequence 全相等时，才生成 MCU1 到 A55 的一条聚合消息。
5. 聚合发送成功后才递增 `lineIndex`；达到 `vResolution` 后将 line 清零并递增 `frameNum`。
6. 单个 Q8 完成、重复消息、旧 sequence 或 sequence 不一致，都不能推进 frame/line，也不能结束本次采集。

MCU1 到 A55 的 header 从单地址改为四个地址：

```c
typedef struct {
    uint32_t cmdId;
    uint32_t metaDataAddr;
    uint32_t dataAddress[4];
    uint32_t reserved[2];
} IPCH_LidarHeaderType;
```

收到 core `n` 的完成消息时，MCU1 使用：

```c
LIDAR_SHARED_CTX->config.bufferInfo[n].addr
```

填入 `dataAddress[n]`。这样 Q8 写入地址、MCU1 发布地址和 A55 验证地址来自同一个四路 buffer 配置。

## 4. Q8 配置

修改目录：

```text
LidarQ8App_0_1
```

### 4.1 编译期 core ID

Q8 工程使用编译期宏：

```c
Q8_CORE_ID = 0, 1, 2, 3
```

`lidar_q8_app.h` 限制该值必须为 0～3。每次生成镜像都必须显式传入对应的 core ID，不能用同一个默认 core 0 镜像启动四个 Q8。

### 4.2 ADC/iDMA 读取地址

`lidar_q8_app_raw.c` 使用：

```c
#define ADC_GROUP_STRIDE    0x00040000UL
#define ADC_GROUP_BASE      (0x00600000UL + Q8_CORE_ID * ADC_GROUP_STRIDE)
#define ADC_RD_BUF_OFFSET   0x00020000UL
```

FFT 模式的四个 read buffer offset 为：

```text
0x30000, 0x34000, 0x38000, 0x3C000
```

因此每一个 Q8 的 iDMA descriptor 都从自己的 `ADC_GROUP_BASE` 读取。TRM 明确说明 Q8/DSP 的 iDMA 只能读取对应 ADC group，使用其他 group 会违反硬件数据路径约束。

### 4.3 输出 buffer

Q8 从共享配置中读取：

```c
Buffer0Address
Buffer1Address
Buffer2Address
Buffer3Address
```

然后按 `Q8_CORE_ID` 选择唯一输出地址：

```c
output_addr[Q8_CORE_ID]
```

内部 DRAM 仍然使用 ping/pong 作为处理流水线的临时源；对外的 global SRAM output buffer 按 Q8 core 独立分配，不能让四个 Q8 共用一个 output address。

### 4.4 Q8 完成消息

每个 Q8 完成一次 transfer 后发送：

```c
IPC_CMD_BUF_DONE_DOORBELL(Q8_CORE_ID, sequence)
```

发送前等待 channel 4 为零，写入 `IPC_RX_REG` 的 acknowledge 信息，再写 channel 4 doorbell。这样 MCU1 可以从同一条消息识别发送者，不需要猜测是哪个 Q8 的 BUF_DONE。

### 4.5 Q8 image 与链接地址

当前 A55 端约定如下：

| Q8   | image 文件                 |   load address | core mask | linker LSP                      |
| ---- | -------------------------- | -------------: | --------: | ------------------------------- |
| Q8_0 | `/data/Q8/q8_core_0.bin` | `0x00100000` |   `0x1` | `linker_scripts/rigel_lsp_q0` |
| Q8_1 | `/data/Q8/q8_core_1.bin` | `0x00110000` |   `0x2` | `linker_scripts/rigel_lsp_q1` |
| Q8_2 | `/data/Q8/q8_core_2.bin` | `0x00120000` |   `0x4` | `linker_scripts/rigel_lsp_q2` |
| Q8_3 | `/data/Q8/q8_core_3.bin` | `0x00130000` |   `0x8` | `linker_scripts/rigel_lsp_q3` |

四个 linker LSP 的 SRAM reset/vector 段分别从上述地址开始。image 不能把四个 Q8 都链接到 `0x00100000`，否则启动时会互相覆盖。

## 5. A55 配置

修改目录：

```text
A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files
```

### 5.1 配置文件

`config.ini` 的关键项为：

```ini
NumChannels = 4
ACQIntervalUs = 7
Q8Core0_ImageFile = /data/Q8/q8_core_0.bin
Q8Core1_ImageFile = /data/Q8/q8_core_1.bin
Q8Core2_ImageFile = /data/Q8/q8_core_2.bin
Q8Core3_ImageFile = /data/Q8/q8_core_3.bin
```

`src/config.c` 中的共享配置常量也必须保持：

```c
#define LIDAR_CONFIG_SAMPLE_FREQ 0  /* 1.25G */
#define LIDAR_CONFIG_SAMPLE_SIZE 8192
```

其中 `0` 是 MCU1/Q8 共享配置定义的 1.25G 枚举值；A55 发布共享配置时不能把它改回原 5G 值。

所有四个 Q8 image 都参与所有 LiDAR mode；Q8 image 的选择依据是 core，不是 mode。

### 5.2 载入和启动四个 Q8

A55 的 `src/cmd.c` 使用四个独立 image slot：

```text
Q8_0: 0x00100000, core mask 0x1
Q8_1: 0x00110000, core mask 0x2
Q8_2: 0x00120000, core mask 0x4
Q8_3: 0x00130000, core mask 0x8
MCU1: 0x00040000
```

`cmd_init()` 依次检查、载入四个 Q8 image，然后载入 MCU1 image。 `cmd_start()` 使用不同的 core mask 启动四个 Q8，不能重复使用同一个 mask。

### 5.3 四路共享数据 buffer

A55 的 DSP data response handler 在 channel 3 的 payload 中分配四个不重叠的 32 KiB slot，每个 slot 前保留 metadata：

```text
slot 0: page_size + 0 * 0x8000 + metadata_size
slot 1: page_size + 1 * 0x8000 + metadata_size
slot 2: page_size + 2 * 0x8000 + metadata_size
slot 3: page_size + 3 * 0x8000 + metadata_size
```

因此 channel 3 的 payload 至少需要：

```text
page_size + 4 * 0x8000 = 0x21000   (page_size=0x1000 时)
```

bootloader YAML 中 channel 3 已设置：

```yaml
MsgMaxSize: 0x22000
```

A55 初始化时把四个地址发布到共享配置的 `buffer0_address` 到 `buffer3_address`。收到 MCU1 的聚合 header 后，A55 严格要求：

```c
dsp_hdr->data_addr[core] == dsp_buf_addr[core]
```

四路地址全部匹配后才复制 metadata 并向 Host 发送数据。当前 Host ABI 保持不变，A55 会发送四个 UDP packet，而不是把四路数据拼成一个 UDP packet。

## 6. Bootloader IPC YAML

文件：

```text
BCM8915X_BL_REL_1.5_src/init/bcm8915x/scripts/m7_flash_img.yaml
```

必须保证：

```yaml
- ChannelId: 3
  TargetCPU: A55
  SrcCPU: [MCU_1]
  PrivilegedWrite: false
  MsgMaxSize: 0x22000

- ChannelId: 4
  TargetCPU: MCU_1
  SrcCPU: [Q8_0, Q8_1, Q8_2, Q8_3]
  PrivilegedWrite: false
```

channel 3 需要容纳一条 MCU1 到 A55 的消息以及四路数据 buffer；channel 4 必须把四个 Q8 都列为合法 source。只增加 MCU1 或只增加 Q8_0 都不足以支持四路完成同步。

## 7. 构建流程

### 7.1 MCU1

先按原工程流程构建 bare-metal drivers，再构建 RTOS application：

```bash
cd <bare-metal-drivers>
make clean
make all

cd <workspace>/LidarRTOSapp_0_1/apps/lidar_rtos_app
export TOOLCHAIN_PATH=/opt/gcc-arm-none-eabi-4_9-2015q3
export PATH="$TOOLCHAIN_PATH/bin:$PATH"
make clean
make all
```

产物应为：

```text
out/lidar_rtos_app.elf
out/lidar_rtos_app.bin
```

实际工具链路径不同于 README 中的默认值时，用 `TOOLCHAIN_PATH` 覆盖。MCU1 的编译验证使用 `compile_commands.json` 中的源文件，并通过了 `-Wall -Werror` 的修改文件检查。

### 7.2 Q8 Linux 工具链

Q8 处理器配置包和工具链位置为：

```text
Processor configuration:
  /opt/RI-2023.11-linux/q8_1024_tie_sp_prod1_f/

Xtensa tools:
  /opt/RI-2023.11-linux/XtensaTools/bin/

Compiler:
  /opt/RI-2023.11-linux/XtensaTools/bin/xt-clang
```

配置包中的 `misc/hostenv.mk` 明确要求 Xtensa tools 在 `PATH` 中。Linux 环境首先设置：

```bash
export Q8_CONFIG=/opt/RI-2023.11-linux/q8_1024_tie_sp_prod1_f
export Q8_TOOLS=/opt/RI-2023.11-linux/XtensaTools
export PATH="$Q8_TOOLS/bin:$PATH"
export XTENSA_CORE=q8_1024_tie_sp_prod1_f
export XTENSA_SYSTEM="$Q8_CONFIG/config"
```

该 processor package 的安装记录原本指向发布机路径。若目标目录可写，先执行配置包安装/注册：

```bash
"$Q8_CONFIG/install" \
    --xtensa-tools "$Q8_TOOLS" \
    --no-default
```

当前工作区中 `/opt` 下的包不可写，直接执行会因为无法创建 `misc/install-status` 失败。可以复制到可写临时目录做注册测试：

```bash
Q8_WORK=$(mktemp -d /tmp/q8-package.XXXXXX)
cp -a "$Q8_CONFIG" "$Q8_WORK/"
mkdir -p "$Q8_WORK/registry"

"$Q8_WORK/q8_1024_tie_sp_prod1_f/install" \
    --xtensa-tools "$Q8_TOOLS" \
    --registry "$Q8_WORK/registry" \
    --no-default

export XTENSA_SYSTEM="$Q8_WORK/registry"
```

每一个 Q8 镜像必须使用不同的 `Q8_CORE_ID` 和 linker LSP。Xplorer 生成的原始顶层 `LidarQ8App_0_1/Makefile` 仍然硬编码了 Windows `cmd` 和旧 Windows 工程路径，当前 Linux 下不能直接执行：

```text
make Q8_CORE_ID=0
```

Linux 侧应使用 Xplorer 生成的 Linux inner Makefile，或将原工程的编译/链接规则移植为 Linux 入口。规则中至少应保留以下参数：

```text
-DQ8_CORE_ID=0..3
-mcoproc
-LNO:simd
-fswp-max-unroll=32
-mlsp=linker_scripts/rigel_lsp_q0..q3
-lidma
```

编译输出需分别重命名为：

```text
q8_core_0.bin
q8_core_1.bin
q8_core_2.bin
q8_core_3.bin
```

建议在具备 license 后逐 core 构建并检查 ELF：

```bash
for core in 0 1 2 3; do
    make Q8_CORE_ID="$core" \
         LSP="linker_scripts/rigel_lsp_q$core"
done
```

上面的 `make` 是构建入口示例；当前仓库的 Windows 包装 Makefile 尚未提供可直接执行的 Linux inner target，不能把它当作本工作区已经验证通过的命令。

Xtensa 编译器启动时需要 `XT_XCC_FUSA` license。当前主机实测返回：

```text
License checkout failed: Cannot connect to license server system.
Feature:       XT_XCC_FUSA
Server name:   127.0.0.1
FlexNet Licensing error:-15,570
```

因此当前工作区已验证 processor package 注册流程，但尚未完成 Q8 C 编译和四个最终 bin 的生成。必须先提供可用 license server/license file，再进行四路构建。

### 7.3 A55

```bash
cd <workspace>/A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files
make -B \
    CFLAGS='-Wall -Wextra -I./include -I../../uiolib/files/include' \
    LDLIBS='-L../../uiolib/files/build -luio -lpthread'
```

该命令已完成编译和链接验证。生成物位于当前 A55 app 的 `build/` 目录；构建产生的 `build/`、`compile_flags.txt` 等现有文件不要与源码修改混淆。

### 7.4 Bootloader YAML

```bash
cd <workspace>/BCM8915X_BL_REL_1.5_src
python3 oneui/scripts/m7_flash_img.py \
    -i init/bcm8915x/scripts/m7_flash_img.yaml \
    -o /tmp/lidar_m7_flash_test.bin
```

YAML 解析和官方脚本生成测试 image 已通过。脚本可能同时在 `/tmp` 生成 `.hex`、`.bin`、`.log` 文件。

## 8. 上电后的运行顺序

1. A55 读取 `config.ini`，检查四个 Q8 image 和 MCU1 image。
2. A55 把 Q8_0..Q8_3 分别写入 `0x00100000`、`0x00110000`、`0x00120000`、`0x00130000`，把 MCU1 写入 `0x00040000`。
3. A55 使用 core mask `0x1`、`0x2`、`0x4`、`0x8` 分别启动四个 Q8。
4. A55 初始化 channel 3 的四路 buffer，并把地址/大小发布到 `0x00190000` 的共享配置。
5. MCU1 读取配置，初始化 ADC0..ADC3 为 1.25G、8K samples，并启动采集触发。
6. 每个 Q8 从自己的 ADC group 读取数据，写自己的 output buffer，然后上报带 `core_id` 和 sequence 的统一 BUF_DONE。
7. MCU1 收齐同一 sequence 的四路消息后，把四个 `dataAddress[]` 放入一条 MCU1 到 A55 的聚合消息。
8. A55 验证四个地址，复制 metadata，并按现有 Host ABI 发送四个 UDP packet。
9. 只有第 7 步聚合消息发送成功后，MCU1 才推进 line/frame 计数。

## 9. 验证清单

### 静态配置

- [ ] `NumChannels=4`。
- [ ] `adcSampleFreq=LIDAR_ADC_SAMPLE_FREQ_1_25G`，不是 5G。
- [ ] `adcSelect=0xF`，ADC0..ADC3 全部初始化。
- [ ] `ACQIntervalUs=7`，满足 TRM 的 6.5696 us 最小值。
- [ ] Q8_0..Q8_3 使用 core ID 0..3。
- [ ] Q8_0..Q8_3 使用 ADC group 0..3。
- [ ] Q8_0..Q8_3 使用不同 linker LSP 和不同 image load address。
- [ ] core mask 分别为 `0x1/0x2/0x4/0x8`。
- [ ] A55 的四个 `BufferXAddress` 不相同且不重叠。
- [ ] channel 3 `MsgMaxSize >= 0x21000`，当前设置为 `0x22000`。
- [ ] channel 4 的 source 包含 Q8_0、Q8_1、Q8_2、Q8_3。

### IPC 和同步

- [ ] BUF_DONE command 固定字段为 `0xE6100000`。
- [ ] MCU1 能解析 core ID 0、1、2、3。
- [ ] sequence 在四路之间一致。
- [ ] `completeMask` 必须达到 `0xF` 才允许聚合发送。
- [ ] 单路完成不会推进 frame/line。
- [ ] 四路聚合发送失败时不推进 frame/line。
- [ ] Q8 发送前等待 channel 4 清零。
- [ ] MCU1 收到 Q8 消息后 acknowledge channel 4。
- [ ] A55 检查 `data_addr[core] == dsp_buf_addr[core]`。

### 构建和硬件

- [X] MCU1 修改文件通过 `-Wall -Werror` 检查。
- [X] A55 app 完成编译和链接。
- [X] bootloader YAML 通过官方脚本解析和生成测试 image。
- [X] Q8 processor package 可在临时 registry 注册。
- [ ] Q8 license server 可用。
- [ ] Q8_0..Q8_3 四个最终 bin 完成 Linux 编译。
- [ ] 四个 Q8 image 在真实硬件上分别启动。
- [ ] ADC0..ADC3、四路 iDMA、channel 4 doorbell 和 channel 3 payload 完成板级联调。
- [ ] 使用示波器/trace 或 Q8 debug counter 复核四路采集的 frame/line 同步。

## 10. 已知限制和注意事项

1. 当前 Q8 对外每个 core 使用一个独立 output SRAM buffer，内部 DRAM 使用 ping/pong；没有实现独立的 buffer release/backpressure IPC。若 A55 消费速度低于采集速度，需要增加 buffer 生命周期或流控协议。
2. A55 到 Host 仍发送四个 UDP packet；“合并发送给 A55”指 MCU1 到 A55 的单条四地址聚合 IPC，不表示 Host 侧变为单包协议。
3. sequence 只有 10 bit，回绕处理依赖半范围判断；如果未来修改 command base 或位分配，MCU1 和 Q8 必须同时更新。
4. `IPC_CHAN_DSP_TO_MCU1` 是四个 Q8 共享的 channel，不能让四个 Q8 不等待 doorbell 清零而连续写入。
5. Q8 顶层 Makefile 是 Windows/Xplorer 生成的包装器；Linux 工具链路径已经确认，但由于当前 license server 不可用，四个 Q8 最终 image 还没有完成本机实编译验证。
6. 现有工程的 `A55_system/yocto/rigel/recipes-apps/lidar_a55_app/files/build/` 等目录是构建产物，不应作为源码修改回滚或删除。

## 11. 故障定位

| 现象                                         | 优先检查                                                                              |
| -------------------------------------------- | ------------------------------------------------------------------------------------- |
| MCU1 只收到一条 Q8 完成就发送                | 检查`completeMask == 0xF` 和四个 `completeSeq[]` 比较逻辑                         |
| MCU1 收不到 Q8_2/Q8_3                        | 检查 channel 4 YAML source、`Q8_CORE_ID`、command bits [9:8]                        |
| 四个 Q8 读到相同 ADC 数据                    | 检查`ADC_GROUP_BASE` 是否使用 `Q8_CORE_ID * 0x40000`，以及对应 LSP/image 是否正确 |
| Q8 image 启动互相覆盖                        | 检查`0x00100000/0x00110000/0x00120000/0x00130000` 和对应 linker LSP                 |
| A55 报 data address invalid                  | 检查 Q8 写地址、MCU1`bufferInfo[]`、A55 `dsp_buf_addr[]` 是否一致                 |
| channel 3 初始化失败                         | 检查 payload 是否至少`0x21000`，YAML 是否为 `MsgMaxSize: 0x22000`                 |
| Q8`make` 执行 `cmd` 失败                 | 当前顶层 Makefile 是 Windows wrapper，需要 Linux inner Makefile 或移植构建规则        |
| `xt-clang` 报 `XT_XCC_FUSA` license 错误 | 配置 Xtensa/FlexNet license server；processor package 注册本身不是该错误的原因        |
