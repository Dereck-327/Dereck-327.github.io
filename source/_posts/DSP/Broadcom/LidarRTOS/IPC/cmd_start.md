  ```c
start_proc(IPC_PROC_ID_DSP, 1, 0x00100000);
```

参数含义是：

| 字段 | 值 | 含义 |
| --- | --- | --- |
| proc_id | IPC_PROC_ID_DSP，即 3 | 要启动 DSP，也就是这里所说的 Q8 |
| core_mask | 1 | Q8 的 core mask |
| img_addr | 0x00100000 | Q8 固件已加载到的物理地址 |

  这里 Q8 使用的是：

```c
#define IPC_PROC_ID_DSP 3
```

  所以代码中没有单独定义 IPC_PROC_ID_Q8，而是用 IPC_PROC_ID_DSP 表示 Q8。

  ## 2. start_proc() 构造发给 MCU0 的 IPC 消息

  start_proc() 首先取得 IPC 共享 payload 区：

  uio_pld_mem_t pld_mem;

  if (ipc_req_get_pld(&pld_mem) != 0) {
        LOG_ERROR("START: failed to get IPC payload memory\n");
        return -1;
  }

  这个 payload 区是在程序启动时通过 MCU0 的 requestor channel 初始化的。初始化路径在 lidar_a55_app/files/src/main.c:82：

  config_get_ipc_chan_num(IPC_ROLE_REQUESTER, IPC_PROC_ID_MCU0,
                &req_chan_num);

  ipc_req_init(req_chan_num);

  当前 config.c 中该通道号固定为：

  #define IPC_CHAN_REQ_MCU0  2

  因此，A55 的启动请求走的是 mailbox channel 2，目标是 MCU0。

  然后，代码在栈上创建一个 32 字节的 bcm_msg：

```c
bcm_msg msg = { 0 };
```

  消息格式为：

```c
typedef struct s_bcm_msg {
    bcm_msg_header hdr; /* 4 bytes */
    bcm_msg_payload pld; /* 28 bytes */
} bcm_msg;
```

  其 payload 被解释成 Loader 命令需要的结构：

```c
typedef struct s_loader_init_proc_in {
    uint8_t  proc_id;
    uint8_t  core_mask;
    uint8_t  img_with_hdr;
    uint8_t  reserved;
    uint32_t img_addr;
    uint32_t flags[5];
} loader_init_proc_in;
```

  随后设置消息头：

  msg.hdr = BCM_MSG_HDR(BCM_MSG_HDR_MSGID_NONE,
                LOADER_SRV_FUNC_ID_INIT_PROCESSOR,
                BCM_SRV_ID_LOADER,
                IPC_PROC_ID_MCU0,
                BCM_MSG_HDR_REPLY_NONE);

  这几个字段非常关键：

  message ID = 0
  function ID = 0x06
  service ID  = 0x06，即 LOADER service
  processor ID = 0，即 MCU0
  reply = none

  换句话说，消息头表达的是：

  请 MCU0 的 Loader 服务执行 function 0x06：
  INIT_PROCESSOR

  消息头里的 processor ID 是 MCU0，而不是 MCU1/Q8。因为这里的 processor ID 指的是“IPC 消息的目标处理器”，真正要启动的处理器由 payload 中的 proc_id 指定。

  ## 3. 目标处理器写在 payload 中

  ldr_pld_ptr->proc_id = proc_id;
  ldr_pld_ptr->core_mask = core_mask;
  ldr_pld_ptr->img_addr = img_addr;

  启动 MCU1 时，payload 大致是：

  offset 0: proc_id      = 1
  offset 1: core_mask    = 1
  offset 2: img_with_hdr = 0
  offset 3: reserved     = 0
  offset 4: img_addr     = 0x00040000
  offset 8: flags[0..4]  = 0

  启动 Q8 时，payload 大致是：

  offset 0: proc_id      = 3
  offset 1: core_mask    = 1
  offset 2: img_with_hdr = 0
  offset 3: reserved     = 0
  offset 4: img_addr     = 0x00100000
  offset 8: flags[0..4]  = 0

  其中 img_with_hdr 没有显式设置，但因为：

```c
bcm_msg msg = { 0 };
```

  所以它的值为 0，表示当前发送的镜像被认为是不带额外镜像头的 raw image。flags 也全部保持为 0。

  ## 4. 把消息复制到 IPC 共享内存

  memcpy(pld_mem.virt_addr, &msg, sizeof(bcm_msg));

  这里的 pld_mem.virt_addr 是 uiolib 映射出来的 mailbox payload 虚拟地址。消息内容从 A55 的栈变量复制到 MCU0 能看到的共享 payload 区域。

  ## 5. 通过 doorbell 通知 MCU0

  if (ipc_req_send_recv(0) != 0)
        return -1;

  ipc_req_send_recv(0) 的实现位于 lidar_a55_app/files/src/ipc_req.c:64。

  因为传入的 cmd_id 是 0，所以函数先把共享 payload 的虚拟地址转换成物理地址：

  if (!cmd_id)
        cmd_id = uio_mbox_virt_to_phy(&dev_ctx, pld_mem.virt_addr);

  然后发送：

  uio_mbox_send_msg(&dev_ctx, (cmd_id | 1));

  因此，doorbell 的值是：

  共享 payload 物理地址 | 1

  最低位的 1 用来表示这是一个“地址形式”的 doorbell，而不是直接编码的 command。MCU0 收到 mailbox doorbell 后，根据这个物理地址找到共享内存中的 bcm_msg，读取：

  hdr
  payload.proc_id
  payload.core_mask
  payload.img_addr

  接着由 MCU0 的 Loader 服务处理 LOADER_SRV_FUNC_ID_INIT_PROCESSOR = 0x06，对指定目标处理器进行初始化/启动。

  ## 6. A55 等待 MCU0 返回结果

  发送 doorbell 后：

  status = uio_mbox_get_status(&dev_ctx, true);

  这里会阻塞等待 MCU0 的响应中断。收到响应后，start_proc() 从同一个共享 payload 中读取返回码：

  msg_ptr = (bcm_msg *)pld_mem.virt_addr;
  retcode = BCM_MSG_GET_RETCODE(msg_ptr);

  判断 MCU0 的 Loader 是否成功：

  if (retcode != 0) {
        LOG_ERROR("START: remote returned %d\n", retcode);
        return -1;
  }

  所以每个处理器的启动都具有同步语义：

  发送 MCU1 启动请求
      ↓
  等待 MCU0 响应
      ↓
  检查 retcode
      ↓
  只有成功后才发送 Q8 启动请求

  如果 MCU1 启动失败，Q8 请求不会发送；如果 Q8 启动失败，cmd_start() 返回错误，也不会打印 datapath started。

  ## 7. 固件是在 init 阶段预先装载的

  cmd_start() 本身不加载固件，只把固件地址告诉 MCU0。固件加载发生在 cmd_init() 中：

  if (load_image(q8_img, q8_size, Q8_IMG_LOAD_ADDR) != 0)
        ...

  Q8 镜像被复制到：

  0x00100000

  然后 MCU1 镜像被复制到：

  0x00040000

  对应代码：

  #define Q8_IMG_LOAD_ADDR   0x00100000
  #define MCU1_IMG_LOAD_ADDR 0x00040000

  load_image() 通过 /dev/mem 将文件内容 mmap 到目标物理地址：

  mem_fd = open("/dev/mem", O_RDWR | O_SYNC);

  map = mmap(NULL, size, PROT_WRITE, MAP_SHARED,
                mem_fd, load_addr);

  read(fd, map, size);

  因此完整时序是：

  init <mode>
      │
      ├─ 校验 Q8 镜像
      ├─ 校验 MCU1 镜像
      ├─ 将 Q8 镜像写入 0x00100000
      ├─ 将 MCU1 镜像写入 0x00040000
      ├─ 启动 A55 的 IPC responder
      └─ 发布共享配置
      │
      ├─ 发 INIT_PROCESSOR(proc_id=MCU1, img_addr=0x00040000) 给 MCU0

```text
A55 -> MCU0 Loader service -> 请求启动 MCU1/Q8
```

结合你补充的 MCU0 源码，可以把整个链路落实为：

```text
A55 cmd_start()
  │
  ├─ 准备 MCU1/Q8 的 INIT_PROCESSOR 消息
  ├─ 通过 uiolib 将 doorbell 写入 MCU0 IPC 通道
  │
  ▼
MCU0 IPC 硬件产生 TX interrupt
  │
  ▼
IPCDRV_IRQHandlerTx()
  │
  ├─ 从 doorbell 读取 A55 发来的消息地址
  ├─ 将 remote address 转换为 MCU0 local address
  ├─ 读取 bcm_msg.hdr，得到 service ID = LOADER
  ├─ 保存 RX pending message
  └─ 调用 IPC channel callback
  │
  ▼
SRVRT_IpcChannelCb()
  │
  └─ 唤醒 SYSTEM_Task
  │
  ▼
SRVRT_IPCPollInternal()
  │
  ├─ IPCDRV_RecvMsg(BCM_SRV_ID_LOADER, ...)
  ├─ 把消息放入 LOADER_SRV 消息队列
  └─ 唤醒 Loader executor
  │
  ▼
LOADER_SrvReqKickoff()
  │
  └─ 记录 funcId = INIT_PROCESSOR
  │
  ▼
LOADER_SrvReqPoll()
  │
  └─ 调用 LOADER_SrvInitProcessorSM()
  │
  ├─ MCU_ResetProcessor(procId, coreMask)
  └─ MCU_InitProcessor(procId, coreMask, entryPoint, flags)
  │
  ▼
MCU1/Q8 启动
  │
  ▼
IPCDRV_SendResp()
  │
  ▼
A55 收到 response，检查 retcode
```

  这里的关键点是：A55 发给 MCU0 的只是一个 LOADER 服务请求，MCU0 的 Loader 再调用 MCU 驱动去操作 MCU1/Q8 的 reset、vector 和 enable 寄存器。

  ## 1. A55 发送的消息

  A55 的 cmd_start() 依次调用：

  start_proc(IPC_PROC_ID_MCU1, CORE_MASK_MCU1,
                MCU1_IMG_LOAD_ADDR);

  start_proc(IPC_PROC_ID_DSP, CORE_MASK_Q8,
                Q8_IMG_LOAD_ADDR);

  因此两条请求分别是：

  请求 1：
    service  = LOADER
    function = INIT_PROCESSOR (0x06)
    target   = MCU0
    payload.proc_id   = MCU1 (1)
    payload.core_mask = 1
    payload.img_addr = 0x00040000

  请求 2：
    service  = LOADER
    function = INIT_PROCESSOR (0x06)
    target   = MCU0
    payload.proc_id   = Q8 (3)
    payload.core_mask = 1
    payload.img_addr = 0x00100000

  MCU0 侧同样定义了对应的 Loader payload：

  typedef struct sLOADER_SrvInitProcInType {
      uint8_t      procId;
      uint8_t      coreMask;
      uint8_t      imgWithHdr;
      uint8_t      reserved;
      uint32_t     imgAddr;
      uint32_t     flags[5];
  } LOADER_SrvInitProcInType;

  该定义位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/init/loader/lib/loader_srv_cmds.h:250

  A55 侧的本地结构与 MCU0 侧结构布局一致，所以 A55 填好的 payload 可以直接被 MCU0 Loader 解释。

  需要注意：

  msg.hdr = BCM_MSG_HDR(...,
                LOADER_SRV_FUNC_ID_INIT_PROCESSOR,
                BCM_SRV_ID_LOADER,
                IPC_PROC_ID_MCU0,
                BCM_MSG_HDR_REPLY_NONE);

  这里 header 中的 proc_id = IPC_PROC_ID_MCU0 表示“这条 IPC 消息发给 MCU0”。

  真正要启动谁，不在 header 中，而是在 payload 中：

  ldr_pld_ptr->proc_id = proc_id;

  也就是说：

  header.proc_id = MCU0       // 消息接收方
  payload.proc_id = MCU1/Q8   // Loader 要启动的目标处理器

  ## 2. A55 的 doorbell 如何到达 MCU0

  A55 的 ipc_req_send_recv(0) 最终会调用 uiolib：

  uio_mbox_send_msg(&dev_ctx, (cmd_id | 1));
  cmd_id = uio_mbox_virt_to_phy(&dev_ctx, pld_mem.virt_addr);

  之后写入：

  payload physical address | 1

  MCU0 IPC 驱动的发送路径使用同样的 doorbell 约定。在：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/system/drivers/ipc/lib/ipcdrv.c:612

  MCU0 侧发送消息时是：

  ret = BCM_Map2GlobalAddr(txTblElt->msgCpy, &globalAddr);
  if (BCM_ERR_OK == ret) {
      globalAddr |= IPC_DOORBELL_FULL_MASK;
      IPCDRV_Regs->doorbell[txTblElt->chIdx] = globalAddr;
  }

  A55 侧的 | 1 与 MCU0 侧的 IPC_DOORBELL_FULL_MASK 本质上都是将 doorbell 标识为“full/address form”，具体 mask 宏由两侧各自的 uiolib/IPC 头文件定义。

  ## 3. MCU0 IPC 驱动收到消息

  MCU0 端真正接收 A55 消息的是：

  void IPCDRV_IRQHandlerTx(void)

  位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/system/drivers/ipc/lib/ipcdrv.c:975

  它的处理逻辑是：

  const uint32_t txMask = IPCDRV_Regs->ipc_tx;

  for (chIdx = 0UL; chIdx < IPC_NUM_CHANNELS; chIdx++) {
      if ((txMask & (1UL << chIdx)) != 0UL &&
          IPCDRV_RxState.pendingStates[chIdx].origMsg == NULL) {

          const uint32_t msgRemoteAddr =
              IPCDRV_Regs->doorbell[chIdx] &
              ~IPC_DOORBELL_FULL_MASK;

          ...
      }
  }

  对 A55 发来的请求，主要做四件事。

  ### 3.1 从 doorbell 取消息地址

  const uint32_t msgRemoteAddr =
      IPCDRV_Regs->doorbell[chIdx] &
      ~IPC_DOORBELL_FULL_MASK;

  doorbell 的高位/标志位被清掉后，剩下的就是 A55 共享消息地址。

  ### 3.2 将 A55 地址转换为 MCU0 地址

  BCM_MapRemoteEpMemAddrToLocal(
      IPCDRV_RxState.pendingStates[chIdx].msgOrigEp.cpuID,
      msgRemoteAddr,
      &msgAddr);

  MCU0 地址转换实现位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/architecture/abstract/lib/bcm_msg.c:73

  BCM8915X 上的规则是：

  if (aRemoteMemAddr < 0x200000UL) {
      /* SRAM */
      localMemAddr = aRemoteMemAddr | 0x01000000UL;
  }

  所以 A55 使用的低地址：

  0x00040000 -> MCU0 local 0x01040000
  0x00100000 -> MCU0 local 0x01100000

  这也解释了为什么 A55 的固件装载地址是 0x00040000 和 0x00100000，而 MCU0 可以把这些地址映射到自己的 SRAM window 中访问。

  如果是 DRAM 地址：

  if ((0x80000000UL <= aRemoteMemAddr) &&
      (0xE0000000UL > aRemoteMemAddr)) {
      /* DRAM, no address translation needed */
  }

  则地址保持不变。

  ### 3.3 根据消息头识别 Loader 服务

  映射成功后：

  BCM_MsgType *const msg = (BCM_MsgType *const)msgAddr;
  const BCM_SrvIdType srvID =
      BCM_MSG_HDR_GET_SRV_ID(msg->hdr);

  const BCM_MsgEPType *const dstEp =
      BCM_MsgEpTbl.ep[srvID];

  A55 发出的 header 中：

  service ID = BCM_SRV_ID_LOADER = 0x06

  MCU0 于是将这条消息判断为 Loader service 请求。

  如果 service 不支持、没有 endpoint 或者 service 被 block，则执行：

  IPCDRV_SrvReqComplete(chIdx, msg, BCM_BOOL_TRUE);

  并返回错误。

  如果合法，则：

  IPCDRV_Regs->ipc_tx_msk &= ~(1UL << chIdx);

  IPCDRV_RxState.pendingStates[chIdx].origMsg = msg;
  IPCDRV_RxState.pendingStates[chIdx].readComplete =
      BCM_BOOL_FALSE;

  IPCDRV_ChannelInfo[chIdx].cbFunc(srvID);

  这里先关闭该 channel 的 TX interrupt mask，防止同一个 channel 在当前请求完成之前又接收下一条请求。

  ### 3.4 调用 IPC callback

  callback 是通过：

  IPCDRV_ChannelInfo[chIdx].cbFunc(srvID);

  调用的。

  在 Service Runtime 中注册的 callback 是：

  SRVRT_IpcChannelCb

  其实现位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/architecture/srvrt/os/common/srvrt_osil_common.c:1372

```c
void SRVRT_IpcChannelCb(BCM_SrvIdType aSrvId)
{
    if (aSrvId < BCM_SRV_ID_MAX) {
        BCM_WakerType waker =
            SRVRT_Srv2WakerTbl[aSrvId].waker;

        BCM_WakerWake(waker);
    }
}
```

  因此 IPC 中断处理函数本身并不直接执行 Loader，也不直接执行 MCU_InitProcessor()。它只负责把 Loader service 对应的 waker 唤醒。

  ## 4. srvrt_osil_common.c 如何把消息交给 Loader

  Service Runtime 初始化时会：

  ret = IPCDRV_Init();

  然后读取 PTU 中的 IPC channel 配置：

  ret = PTU_GetAuxImgInfoById(
      PTU_AUX_IMG_ID_IPCC,
      0UL,
      &auxImgInfo);

  再调用：

  enIpcSrvMask =
      SRVRT_SetupIpcChannel(
          ipcChanCfgEntry,
          SRVRT_IpcChannelCb);

  相关代码位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/architecture/srvrt/os/common/srvrt_osil_common.c:1400

  SRVRT_SetupIpcChannel() 对每个 PTU IPC channel 配置执行：

  IPCDRV_SetChanConfig(
      cfgIdx,
      chanCfg->targetCpuId,
      chanCfg->srcCpuIdMask,
      chanCfg->privilegedWrite);

  如果当前 CPU 是该 channel 的 target，再注册对应 service：

  IPCDRV_RegisterService(
      cfgIdx,
      srvIdMask,
      aCbFunc);

  这说明 MCU0 的实际 IPC channel 与 service 映射不是在 srvrt_osil_common.c 里硬编码完成的，而是来自 PTU 的 IPCC 配置。

  对于 Loader service，生成的 Service Runtime 配置中可以看到：

  .srvId = BCM_SRV_ID_LOADER,
  .taskId = SYSTEM_Task,
  .execId = 0UL,
  .funcTbl = &LOADER_SrvFuncTbl,

  也就是说：

  BCM_SRV_ID_LOADER
         │
         ▼
  SYSTEM_Task
         │
         ▼
  LOADER_SrvFuncTbl

  LOADER_SrvFuncTbl 的定义在：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/init/loader/lib/loader_srv.c:3490

  其中请求处理函数是：

  .srvReqKickoffFn = LOADER_SrvReqKickoff,
  .srvReqPollFn    = LOADER_SrvReqPoll,

  ## 5. SRVRT_IPCPollInternal() 把消息放入 Loader 队列

  被唤醒的 SYSTEM task 在 event loop 中调用：

  SRVRT_IPCPoll(execInfo);

  最终进入：

  SRVRT_IPCPollInternal(aSrvId)

  位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_BL_REL_1.5_src/architecture/srvrt/os/common/srvrt_osil_common.c:1271

  核心代码是：

  if (BCM_ERR_OK == IPCDRV_RecvMsg(
          aSrvId,
          &ipcMsg,
          &ipcMsgSrcEp)) {

      ret = SRVRT_MsgQueueAllocPoolElt(
          queue,
          &msgPoolIdx);

      if (BCM_ERR_OK == ret) {
          queue->msgBundlePool[msgPoolIdx].msg = ipcMsg;
          queue->msgBundlePool[msgPoolIdx].msgEp =
              ipcMsgSrcEp;

          SRVRT_MsgQueueListAdd(
              &queue->pendingListInfo,
              &queue->state->pendingListState,
              msgPoolIdx);
      }
  }

  IPCDRV_RecvMsg() 本身只是在 RX pending state 中找到刚才接收的消息：

  if ((rxState->origMsg != NULL) &&
      (aSrvID == BCM_MSG_HDR_GET_SRV_ID(
          rxState->origMsg->hdr)) &&
      (rxState->readComplete == BCM_BOOL_FALSE)) {

      *aMsg = rxState->origMsg;
      *aMsgOrigEp = &rxState->msgOrigEp;
      rxState->readComplete = BCM_BOOL_TRUE;
  }

  因此消息实际没有被重新拷贝到 MCU0 的 Loader buffer 中，Loader 使用的是 IPC 驱动映射后的原始共享消息指针。

  ## 6. Loader service 识别 INIT_PROCESSOR

  消息进入 Loader 队列后，Service Runtime 调用：

  LOADER_SrvReqKickoff()

  其中保存：

  LOADER_SrvCtx.msg = aMsg;
  LOADER_SrvCtx.msgSrcEp = aMsgEp;
  LOADER_SrvCtx.waker = aWaker;
  LOADER_SrvCtx.msgPld.pldPtr = &aMsg->pld;
  LOADER_SrvCtx.funcId =
      BCM_MSG_HDR_GET_FNC_ID(aMsg->hdr);

  相关代码位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/init/loader/lib/loader_srv.c:3200

  对于 A55 的请求：

  funcId = 0x06

  对应：

  LOADER_SRV_FUNC_ID_INIT_PROCESSOR

  在 LOADER_SrvReqPoll() 的映射表中：

  const LOADER_SrvFuncId2SmMapType smInfo[] = {
      ...
      {
          LOADER_SRV_FUNC_ID_INIT_PROCESSOR,
          &BCM_SM_INFO1(LOADER_SrvInitProcessorSM)
      },
  };

  所以最终调用：

  LOADER_SrvInitProcessorSM()

  ## 7. Loader 状态机具体做什么

  核心实现位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/init/loader/lib/loader_srv.c:2959

  ### 7.1 检查目标处理器

  static const char * const procName[MCU_PROCESSOR_ID_MAX+1UL] = {
      [MCU_PROCESSOR_ID_MCU1] = "MCU1",
      [MCU_PROCESSOR_ID_A55]  = "A55",
      [MCU_PROCESSOR_ID_Q8]   = "Q8",
  };

  然后检查：

  if ((inParam->procId > MCU_PROCESSOR_ID_MAX) ||
      (procName[inParam->procId] == NULL)) {
      ret = BCM_ERR_INVAL_PARAMS;
  }

  因此 payload 中的：

  procId = 1 -> MCU1
  procId = 3 -> Q8

  ### 7.2 解析镜像地址

  ret = BCM_MapRemoteEpMemAddrToLocal(
      LOADER_SrvCtx.msgSrcEp->cpuID,
      inParam->imgAddr,
      &localAddr);

  对于 A55 发来的地址：

  MCU1: 0x00040000 -> MCU0 local 0x01040000
  Q8:   0x00100000 -> MCU0 local 0x01100000

  imgWithHdr 当前由 A55 结构清零，因此通常为 0：

```c
bcm_msg msg = { 0 };
```

  对于 imgWithHdr == 0，Loader 不读取镜像头中的 entry point，而是直接使用：

  uint32_t entryPt = inParam->imgAddr;

  也就是说当前 A55 代码的启动入口地址就是 img_addr 本身：

  MCU1 entry point = 0x00040000
  Q8   entry point = 0x00100000

  如果以后 imgWithHdr 设置为非零，则 Loader 会从镜像头读取：

  fsImgHdr->hdrAuthPld.entryPoint

  并进行签名、entry point 范围等检查。

  ### 7.3 Reset 目标处理器

  状态机执行到 LOADER_SRV_STATE_INIT_PROC_EXEC 后：

  ret = MCU_ResetProcessor(
      inParam->procId,
      inParam->coreMask);

  MCU 驱动实现位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/system/drivers/mcu/lib/bcm8915x/mcu_drv.c:1478

  然后根据处理器类型执行不同操作。

  #### MCU1 reset

  if (aProcId == MCU_PROCESSOR_ID_MCU1) {
      if ((aCoreMask & 1UL) != 0UL) {
          MCU_REG_CLKNRST->cpu_dma_sys_reset_control |=
              RIG_CLKNRST_CDSRC_CPU_DMA_SYS_MCU1_RSTB_FRC_MASK;

          MCU_REG->misc_ctl &= ~MCU_MISC_CTL_U_MCU_EN_MASK;
          MCU_REG->misc_ctl |= MCU_MISC_CTL_U_MCU_WAIT_MASK;

          BCM_CpuNDelay(2000UL);
      }
  }

  即：

  1. 强制 MCU1 reset
  2. 清除 MCU enable
  3. 设置 MCU wait
  4. 延时

  #### Q8 reset

  } else if (MCU_PROCESSOR_ID_Q8 == aProcId) {
      uint32_t coreIdx;

      for (coreIdx = 0UL; coreIdx < 4UL; coreIdx++) {
          if (0UL != (aCoreMask & (1UL << coreIdx))) {
              MCU_SysResetReleaseQ8(
                  coreIdx,
                  BCM_BOOL_TRUE);
          }
      }
  }

  当前 coreMask = 1，因此只处理：

  Q8 core 0

  如果 coreMask = 0xF，则会处理 Q8 的 4 个 core。

  ## 8. 初始化并释放目标处理器

  Reset 成功后，Loader 调用：

  ret = MCU_InitProcessor(
      inParam->procId,
      inParam->coreMask,
      entryPt,
      inParam->flags);

  对应实现位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/system/drivers/mcu/lib/bcm8915x/mcu_drv.c:1966

  ### MCU1 的启动动作

  if (aProcId == MCU_PROCESSOR_ID_MCU1) {
      if ((aCoreMask & 1UL) != 0UL) {
          MCU_REG->u_initvtor = aEntryPt;
          MCU_REG->misc_ctl |= MCU_MISC_CTL_U_MCU_EN_MASK;
          MCU_REG->misc_ctl &= ~MCU_MISC_CTL_U_MCU_WAIT_MASK;

          MCU_REG_CLKNRST->cpu_dma_sys_reset_control &=
              ~RIG_CLKNRST_CDSRC_CPU_DMA_SYS_MCU1_RSTB_FRC_MASK;
      }
  }

  因此 MCU1 的启动顺序是：

  MCU_REG->u_initvtor = 0x00040000
  设置 MCU_EN
  清除 MCU_WAIT
  释放 MCU1 reset

  可以概括为：

  MCU1 vector = 0x00040000
  MCU1 enable = 1
  MCU1 wait   = 0
  MCU1 reset  = release

  ### Q8 的启动动作

  } else if (MCU_PROCESSOR_ID_Q8 == aProcId) {
      uint32_t coreIdx;

      for (coreIdx = 0UL; coreIdx < 4UL; coreIdx++) {
          if (0UL != (aCoreMask & (1UL << coreIdx))) {
              ret = MCU_SysInitQ8(
                  coreIdx,
                  aEntryPt);

              if (BCM_ERR_OK != ret) {
                  break;
              }
          }
      }
  }

  MCU_SysInitQ8() 位于：

  /home/hjk/workspace/11.bcm8915x/BCM8915X_LIDAR_REF_SW_REL_0_1/BCM8915X_BL_REL_1.5_src/system/drivers/mcu/lib/bcm8915x/mcu_drv.c:1793

  它会先调用：

  MCU_SysResetReleaseQ8(
      aCoreIdx,
      BCM_BOOL_FALSE);

  释放对应 Q8 core 的 reset，然后继续配置 Q8 core 的启动向量/相关寄存器。具体 reset 控制由 MCU_SysResetReleaseQ8() 根据 core index 选择对应的：

  Q8_TOP_APBSLAVE_RSTB_n
  Q8_TOP_CRSTB_n
  Q8_TOP_PRESETN_n

  等 reset control 位。

  所以对于当前代码：

  coreMask = 1
  entryPt  = 0x00100000

  实际含义是：

  初始化 Q8 core 0
  使用入口地址 0x00100000
  释放 Q8 core 0 的 reset
  开始执行 Q8 镜像

  ## 9. MCU0 如何返回 response 给 A55

  Loader 状态机完成后，如果返回值不是 BCM_ERR_BUSY：

  if (ret != BCM_ERR_OK) {
      BCM_MemSet(
          &LOADER_SrvCtx.msg->pld,
          0U,
          sizeof(LOADER_SrvCtx.msg->pld));
  }

  BCM_MSG_SET_RETCODE(
      LOADER_SrvCtx.msg,
      ret);

  LOADER_SrvCtx.msg = NULL;

  也就是说，MCU0 会把返回码写入原始 IPC message 的 payload。

  Service Runtime 随后完成消息时，会调用：

  IPCDRV_SendResp(aMsg);

  在 IPC 驱动内部：

  IPCDRV_SrvReqComplete(
      chIdx,
      rxState->origMsg,
      BCM_BOOL_FALSE);

  IPCDRV_SrvReqComplete() 会：

  aMsg->hdr |= BCM_MSG_HDR_REPLY_MASK;
  IPCDRV_Regs->doorbell[aChIdx] = 0x0UL;

  也就是：

  1. 设置消息 header 的 reply bit
  2. 清除 MCU0 侧 doorbell
  3. 清除 RX pending state
  4. 重新使能 channel 的 TX interrupt

  A55 侧等待：

  status = uio_mbox_get_status(&dev_ctx, true);

  收到 MCU0 的响应后，读取共享 payload 中的返回码：

  retcode = BCM_MSG_GET_RETCODE(msg_ptr);

  最终：

  retcode == 0

  表示对应目标处理器的 reset/init 操作成功。

  ## 10. 这段代码实际完成的启动顺序

  最终可以精确总结为：

  ### MCU1

  A55:
    payload.proc_id   = 1
    payload.core_mask = 1
    payload.img_addr  = 0x00040000

  MCU0 Loader:
    Map 0x00040000 -> MCU0 local 0x01040000
    使用 entryPt = 0x00040000

  MCU0 MCU driver:
    MCU_ResetProcessor(MCU1, 1)
      - assert MCU1 reset
      - clear MCU_EN
      - set MCU_WAIT

    MCU_InitProcessor(MCU1, 1, 0x00040000, flags)
      - u_initvtor = 0x00040000
      - set MCU_EN
      - clear MCU_WAIT
      - release MCU1 reset

  ### Q8

  A55:
    payload.proc_id   = 3
    payload.core_mask = 1
    payload.img_addr  = 0x00100000

  MCU0 Loader:
    Map 0x00100000 -> MCU0 local 0x01100000
    使用 entryPt = 0x00100000

  MCU0 MCU driver:
    MCU_ResetProcessor(Q8, 1)
      - assert Q8 core 0 reset

    MCU_InitProcessor(Q8, 1, 0x00100000, flags)
      - 初始化 Q8 core 0
      - 配置 entry point = 0x00100000
      - release Q8 core 0 reset

  并且 cmd_start() 是严格串行的：

  先启动 MCU1
    ↓
  等待 MCU0 返回成功
    ↓
  再启动 Q8 core 0
    ↓
  等待 MCU0 返回成功
    ↓
  打印 datapath started

  因此更准确的描述是：

  > cmd_start() 并不是通过 IPC 直接通知 MCU0 “启动 MCU1 和 Q8”这样的一条复合命令，而是连续发送两条 BCM_SRV_ID_LOADER / LOADER_SRV_FUNC_ID_INIT_PROCESSOR 请求。MCU0 IPC 驱动负责接收和路由消息，srvrt_osil_common.c 负责唤醒 SYSTEM_Task 并把消息投递给 Loader service，Loader 的
  > LOADER_SrvInitProcessorSM() 再分别调用 MCU_ResetProcessor() 和 MCU_InitProcessor()，最终由 MCU 驱动设置 MCU1/Q8 的入口地址、reset 和 enable 寄存器。


• 结论先说：

  当前这套代码下，四个 Q8 不能直接复用 A55 侧现有的“一份镜像 + 一个地址 + core_mask = 1”配置。

  从现有 MCU 驱动测试代码来看，推荐方式是：

  1. A55 将四个 Q8 镜像分别加载到四个不同的地址；
  2. 通过 MCU0 分别对 Q8 core0～core3 发送四次 INIT_PROCESSOR；
  3. 每次请求使用对应的 coreMask 和对应的 imgAddr。

  当前 cmd_start() 实际只启动了 Q8 core0。

  ## 1. 当前应用只启动一个 Q8

  A55 应用中目前定义的是：

  #define Q8_IMG_LOAD_ADDR   0x00100000
  #define CORE_MASK_Q8       1

  并且 cmd_init() 只加载一份 Q8 镜像：

  load_image(q8_img, q8_size, Q8_IMG_LOAD_ADDR);

  cmd_start() 只发送一次 Q8 启动请求：

  start_proc(IPC_PROC_ID_DSP, CORE_MASK_Q8,
                Q8_IMG_LOAD_ADDR);

  所以当前实际请求等价于：

  proc_id   = Q8
  core_mask = 0x1
  img_addr  = 0x00100000

  core_mask = 0x1 只表示：

  bit 0 = 1 -> 只启动 Q8 core0
  bit 1 = 0 -> 不启动 Q8 core1
  bit 2 = 0 -> 不启动 Q8 core2
  bit 3 = 0 -> 不启动 Q8 core3

  因此，当前 lidar_a55_app 并没有启动四个 Q8，只启动了 Q8 core0。

  ## 2. MCU0 确实支持四个独立的 Q8 core

  MCU0 驱动中有四组 Q8 CSR：

  Q8_RDBType * const MCU_REG_Q8[4] = {
      ((Q8_RDBType *)Q8_CSR_0_BASE),
      ((Q8_RDBType *)Q8_CSR_1_BASE),
      ((Q8_RDBType *)Q8_CSR_2_BASE),
      ((Q8_RDBType *)Q8_CSR_3_BASE),
  };

  也就是说，core0～core3 分别有自己的寄存器控制块。

  MCU_SysInitProcessor() 对 Q8 的处理是：

  } else if (MCU_PROCESSOR_ID_Q8 == aProcId) {
      uint32_t coreIdx;

      for (coreIdx = 0UL; coreIdx < 4UL; coreIdx++) {
          if (0UL != (aCoreMask & (1UL << coreIdx))) {
              ret = MCU_SysInitQ8(coreIdx, aEntryPt);
              if (BCM_ERR_OK != ret) {
                  break;
              }
          }
      }
  }

  因此，从驱动接口形式上看：

  MCU_InitProcessor(Q8, 0x1, entry, ...)

  表示初始化 core0；

  MCU_InitProcessor(Q8, 0x2, entry, ...)

  表示初始化 core1；

  MCU_InitProcessor(Q8, 0x4, entry, ...)

  表示初始化 core2；

  MCU_InitProcessor(Q8, 0x8, entry, ...)

  表示初始化 core3；

  MCU_InitProcessor(Q8, 0xF, entry, ...)

  表示依次初始化四个 core。

  ## 3. 现有测试代码明确使用四个不同地址

  MCU 驱动测试代码 MCU_TestQ8BootMask() 给出了目前最直接的参考实现：

  static const MCU_TestQ8CoreInfoType MCU_TestQ8Cores[] = {
      {
          .load_addr = 0x00100000,
          ...
      },
      {
          .load_addr = 0x00110000,
          ...
      },
      {
          .load_addr = 0x00120000,
          ...
      },
      {
          .load_addr = 0x00130000,
          ...
      }
  };

  四个 Q8 的镜像地址分别是：

   Q8 core      镜像地址
  ━━━━━━━━━  ━━━━━━━━━━━━
   core0      0x00100000
  ─────────  ────────────
   core1      0x00110000
  ─────────  ────────────
   core2      0x00120000
  ─────────  ────────────
   core3      0x00130000

  测试代码随后分别加载四个镜像：

  for (c = 0; c < 4; c++) {
      const MCU_TestQ8CoreInfoType *core =
          &MCU_TestQ8Cores[c];

      uint8_t * const dstPtr =
          (uint8_t *)(uintptr_t)
          (core->load_addr | 0x01000000UL);

      if (c == 0) {
          readBootImgData(..., "q8_image_core0.bin");
      } else if (c == 1) {
          readBootImgData(..., "q8_image_core1.bin");
      } else if (c == 2) {
          readBootImgData(..., "q8_image_core2.bin");
      } else if (c == 3) {
          readBootImgData(..., "q8_image_core3.bin");
      }
  }

  这段代码说明当前设计意图是：

  每个 Q8 core 有自己的镜像
  每个 Q8 core 有自己的 load address

  测试中使用的四个镜像文件也是独立的：

  q8_image_core0.bin
  q8_image_core1.bin
  q8_image_core2.bin
  q8_image_core3.bin

  所以，对于你这个 LiDAR 应用，不能只把同一份 lidar_q8_app.bin 写入 0x00100000，然后认为四个 Q8 会自动各自得到一份代码。

  ## 4. 测试代码也是分别启动四个 core

  测试代码不是一次调用：

  MCU_InitProcessor(Q8, 0xF, ...);

  而是：

  for (c = 0; c < 4; c++) {
      if ((aCoreMask & (1UL << c)) != 0) {
          ret = MCU_InitProcessor(
              MCU_PROCESSOR_ID_Q8,
              (1UL << c),
              MCU_TestQ8Cores[c].load_addr,
              opt);
      }
  }

  这非常关键。因为每个 core 使用不同的入口地址：

  core0 -> MCU_InitProcessor(Q8, 0x1, 0x00100000, ...)
  core1 -> MCU_InitProcessor(Q8, 0x2, 0x00110000, ...)
  core2 -> MCU_InitProcessor(Q8, 0x4, 0x00120000, ...)
  core3 -> MCU_InitProcessor(Q8, 0x8, 0x00130000, ...)

  这样每条请求都能携带不同的：

  coreMask
  imgAddr

  而 Loader 的 IPC payload 只有一个 imgAddr：

  typedef struct sLOADER_SrvInitProcInType {
      uint8_t  procId;
      uint8_t  coreMask;
      uint8_t  imgWithHdr;
      uint8_t  reserved;
      uint32_t imgAddr;
      uint32_t flags[5];
  } LOADER_SrvInitProcInType;

  因此，一条 INIT_PROCESSOR 消息只能表达：

  一组 coreMask + 一个 imgAddr

  不能表达：

  core0 -> addr0
  core1 -> addr1
  core2 -> addr2
  core3 -> addr3

  所以如果四个 core 使用不同镜像/不同入口地址，就必须发送多条请求。

  ## 5. “四个一起启动”分两种情况

  ### 情况一：四个 Q8 使用不同镜像或不同地址

  这种情况下应当：

  A55 load 四个镜像到不同地址
          │
          ├─ 请求 1：Q8 core0，addr = 0x00100000
          ├─ 请求 2：Q8 core1，addr = 0x00110000
          ├─ 请求 3：Q8 core2，addr = 0x00120000
          └─ 请求 4：Q8 core3，addr = 0x00130000

  每条请求都经过：

  A55
    -> MCU0 IPC driver
    -> SRVRT
    -> Loader service
    -> MCU_InitProcessor()
    -> MCU_SysInitQ8(coreN, addrN)

  A55 侧可以严格串行等待，也可以从协议层面设计异步批量请求，但当前 ipc_req_send_recv() 是同步等待 response 的，因此现有框架下自然是四次串行请求。

  建议的 A55 侧逻辑类似：

  start_proc(IPC_PROC_ID_DSP, 0x1, 0x00100000);
  start_proc(IPC_PROC_ID_DSP, 0x2, 0x00110000);
  start_proc(IPC_PROC_ID_DSP, 0x4, 0x00120000);
  start_proc(IPC_PROC_ID_DSP, 0x8, 0x00130000);

  不过正式修改时最好不要简单写死四次，而是定义每个 Q8 core 的镜像路径和加载地址数组。

  ### 情况二：四个 Q8 使用相同镜像和相同入口地址

  这种情况下，理论上可以只发送一条请求：

  proc_id   = Q8
  core_mask = 0xF
  img_addr  = 某一个公共入口地址

  因为 MCU0 驱动内部会执行：

  for (coreIdx = 0; coreIdx < 4; coreIdx++) {
      if (aCoreMask & (1UL << coreIdx)) {
          MCU_SysInitQ8(coreIdx, aEntryPt);
      }
  }

  所以 core_mask = 0xF 会覆盖四个 Q8 core。

  但是这有几个前提：

  - 四个 Q8 必须能够执行同一份镜像；
  - 四个 Q8 必须允许使用同一个 entry point；
  - 镜像必须正确处理每个 core 的身份；
  - 代码和数据不能因为四个 core 同时使用而产生冲突；
  - 镜像放置地址必须是四个 Q8 都可以访问的公共地址；
  - Q8 firmware 本身必须是按这种多核启动方式设计的。

  而当前测试代码没有采用这种模式。测试代码使用四份镜像、四个地址、四次初始化，因此不能假设当前 lidar_q8_app.bin 能直接用 core_mask = 0xF 启动四核。

  ## 6. core_mask = 0xF 并不是严格的并行启动

  即使使用：

  MCU_InitProcessor(MCU_PROCESSOR_ID_Q8, 0xF, entry, opt);

  底层也不是四个 core 同时完成初始化，而是：

  for (coreIdx = 0; coreIdx < 4; coreIdx++) {
      if (aCoreMask & (1UL << coreIdx)) {
          MCU_SysInitQ8(coreIdx, aEntryPt);
      }
  }

  也就是依次执行：

  初始化 core0
    ↓
  初始化 core1
    ↓
  初始化 core2
    ↓
  初始化 core3

  每个 MCU_SysInitQ8() 还包括：

  释放 Q8 外部 reset
  设置 altresetvec
  配置 general_ctrl1
  执行 DTCM 初始化
  执行 D-cache A 初始化
  执行 I-cache 初始化
  执行 D-cache B 初始化
  重启 core
  解除 stall

  因此：

  core_mask = 0xF

  表示“由一次 MCU 驱动调用覆盖四个 Q8 core”，并不表示四个 core 完全并行、同时释放 reset。

  如果每个 core 地址不同，仍然必须分别调用，因为一次调用只有一个 aEntryPt：

  MCU_SysInitQ8(coreIdx, aEntryPt);

  ## 7. 当前 Loader 层还有一个重要限制

  LOADER_SrvInitProcessorSM() 中最终执行：

  ret = MCU_ResetProcessor(
      inParam->procId,
      inParam->coreMask);

  if (BCM_ERR_OK == ret) {
      ret = MCU_InitProcessor(
          inParam->procId,
          inParam->coreMask,
          entryPt,
          inParam->flags);
  }

  这里的 entryPt 对整个 coreMask 只有一个值。

  因此：

  INIT_PROCESSOR(procId=Q8, coreMask=0xF, imgAddr=0x00100000)

  只能代表：

  Q8 core0 -> 0x00100000
  Q8 core1 -> 0x00100000
  Q8 core2 -> 0x00100000
  Q8 core3 -> 0x00100000

  它不能代表：

  Q8 core0 -> 0x00100000
  Q8 core1 -> 0x00110000
  Q8 core2 -> 0x00120000
  Q8 core3 -> 0x00130000

  后者需要四个 IPC 请求，或者需要扩展 Loader 的 payload 协议，让一条消息携带四个 entry address。但从现有代码和测试来看，没有必要扩展协议，四次请求即可完成。

  ## 8. 对当前 LiDAR 应用应如何修改

  当前配置只有一个 Q8 镜像字段：

  Q8ImageFileRaw = ./img/lidar_q8_app.bin
  Q8ImageFileFFT = ./img/lidar_q8_app.bin
  Q8ImageFileToF = ./img/lidar_q8_app.bin
  Q8ImageFileFMCW = ./img/lidar_q8_app.bin

  如果 LiDAR firmware 的设计确实要求四个 Q8 分别运行独立镜像，建议改成类似：

  Q8Core0ImageFileRaw = ./img/lidar_q8_core0_app.bin
  Q8Core1ImageFileRaw = ./img/lidar_q8_core1_app.bin
  Q8Core2ImageFileRaw = ./img/lidar_q8_core2_app.bin
  Q8Core3ImageFileRaw = ./img/lidar_q8_core3_app.bin

  或者如果四个 core 运行同一份二进制，也可以由 A55 读取同一文件四次，分别写入：

  0x00100000
  0x00110000
  0x00120000
  0x00130000

  但是否能直接复制同一份镜像，取决于 Q8 firmware 的链接地址、代码是否位置相关、各 core 的数据区是否独立，以及镜像是否包含按 core 区分的配置。不能只根据 MCU 驱动接口判断这一点。

  A55 侧的启动流程应大致变为：

  static const uint32_t q8_core_masks[4] = {
        1U << 0,
        1U << 1,
        1U << 2,
        1U << 3,
  };

  static const uint32_t q8_load_addrs[4] = {
        0x00100000,
        0x00110000,
        0x00120000,
        0x00130000,
  };

  初始化阶段：

  for (i = 0; i < 4; i++) {
        load_image(q8_img[i], q8_size[i], q8_load_addrs[i]);
  }

  启动阶段：

  for (i = 0; i < 4; i++) {
        start_proc(IPC_PROC_ID_DSP,
                   q8_core_masks[i],
                   q8_load_addrs[i]);
  }

  ## 9. 还需要注意地址空间和镜像大小

  测试代码中四个地址间隔为：

  0x00110000 - 0x00100000 = 0x10000

  也就是每个 core 的镜像区域间隔 64 KiB。

  而当前 A55 应用限制：

  #define Q8_IMG_MAX_SZ 0xC000

  即最大 48 KiB，小于 64 KiB 间隔，这与测试地址布局是匹配的。

  但如果正式 Q8 镜像大于 0x10000，或者包含额外 header/data，就可能覆盖下一个 core 的镜像区域，需要重新规划地址。

  另外，MCU0 的地址转换会将 A55 看到的低地址转换到 MCU0 SRAM window：

  A55/Q8 global address:
    0x00100000 -> MCU0 local 0x01100000
    0x00110000 -> MCU0 local 0x01110000
    0x00120000 -> MCU0 local 0x01120000
    0x00130000 -> MCU0 local 0x01130000

  所以四个地址必须落在 MCU0 BCM_MapRemoteEpMemAddrToLocal() 支持的地址范围内。

  ## 最终判断

  对你当前这套工程，最可靠的结论是：

  当前 cmd_start()：
    只启动 Q8 core0

  如果需要启动四个 Q8，并且四个 Q8 使用不同镜像或不同 load address：

  A55 侧加载四份镜像到不同地址
  MCU0 侧发送四次 INIT_PROCESSOR
  每次只带一个 core mask 和一个 img_addr

  推荐映射：

  core0: coreMask = 0x1, imgAddr = 0x00100000
  core1: coreMask = 0x2, imgAddr = 0x00110000
  core2: coreMask = 0x4, imgAddr = 0x00120000
  core3: coreMask = 0x8, imgAddr = 0x00130000

  如果四个 Q8 明确设计成运行同一份镜像、使用相同入口地址，则可以：

  A55 加载一份公共镜像
  MCU0 发送一次 coreMask = 0xF

  但这只是接口层面可行；现有 MCU 测试代码采用的是“四份镜像、四个地址、四次启动”，因此在没有确认 lidar_q8_app.bin 的多核设计之前，应按四个独立 core 分别加载、分别启动来实现。

─ Worked for 3m 30s ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

─ Conversation recap ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

  目标是确认四个Q8的启动方式；现有A55代码仅配置单镜像、单地址和core_mask=1，不能直接支持四核。需确认硬件拓扑及各Q8加载地址和启动掩码。