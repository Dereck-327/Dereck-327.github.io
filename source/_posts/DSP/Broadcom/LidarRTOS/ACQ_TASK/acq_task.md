
# ACQ Task 

  NSU_DrvInit()
      ->
  HSADCH_FullInit()
      ->
  可选 FIR 配置
      ->
  ACQ_SetupTriggerDelays()
      ->
  TXSH_SetupTofPulse()
      ->
  CCUH_SetupAcqTrigger()
      ->
  CCUH_StartNsuTrigger()

  实际调用位置见 app/src/tasks/acq_task.c:176。

  把模块关系展开，大致是：

  NSU event channel 2
      |
      |  在未来 500 us 产生一次 NSU 事件
      v
  ACQCMN Timer1 Event0
      |
      v
  CAPCOM1 / TMR1_COUT0
      |
      v
  ACQCMN acq_trg
      |
      |  ACQ_SetupTriggerDelays() 为下列输出设置各自额外延迟
      |
      +---- delay = 1  ----> TXSLZR trigger output
      |
      +---- delay = 10 ----> HSADC trigger output
      |
      +---- delay = 10 ----> HSREF trigger output

  其中：

  - CCUH_StartNsuTrigger(2UL, 500000UL) 在当前时间之后约 500 µs 启动 NSU channel 2；
  - CCUH_SetupAcqTrigger() 将 Timer1 COUT0 选为主 acquisition trigger 来源；
  - ACQ_SetupTriggerDelays() 决定 acq_trg 分发给 TXSLZR、HSADC、HSREF 后，各模块实际收到触发的附加偏移。

  触发源路由可见 app/src/bcm/ccu_helper.c:153 至 app/src/bcm/ccu_helper.c:175。

## HSADCH_FullInit

![HSADCH_FullInit](./HSADCH_FullInit.md)

## TXSH_SetupTofPulse

![TXSH_SetupTofPulse](./TXSH_SetupTofPulse.md)

## CCUH_SetupAcqTrigger

![CCUH_SetupAcqTrigger](./CCUH_SetupAcqTrigger.md)

## Trigger Generator

![Trigger Generator](./trigger.md)