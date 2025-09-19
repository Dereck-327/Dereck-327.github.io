



#include <stdint.h>
#include "xil_types.h"
#include "xil_io.h"
#include "xil_assert.h"
#include "xparameters.h"
#include "stdio.h"
#include "sleep.h"
#include "xparameters.h"
#include "xparameters_ps.h"	/* defines XPAR values */
#include "xil_types.h"
#include "xil_assert.h"
#include "xil_io.h"
#include "xil_exception.h"
#include "xpseudo_asm.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xscugic.h"
#include "xemacps.h"		/* defines XEmacPs API */
#include "xemacps_ptp.h"
#include "xil_mmu.h"


#include "ttc/ttc_timer.h"
#include "ptp_slave.h"

#include "lwip/etharp.h"
#include "lwip/udp.h"


#define INTC_DEVICE_ID		XPAR_SCUGIC_SINGLE_DEVICE_ID
#define TSU_CLK_FREQ 	XPAR_PSU_ETHERNET_0_ENET_TSU_CLK_FREQ_HZ

#define EMACPS_DEVICE_ID	XPAR_XEMACPS_0_DEVICE_ID
#define EMACPS_IRPT_INTR	XPS_GEM0_INT_ID
#define TIMER_IRPT_INTR		XPAR_XTTCPS_1_INTR
/************************** TSU Definitions *****************************/

#define GEM_TSU_INC_CTRL 0xff0b01c4
#define GEM_CLK_CTRL 0x00FF180308

#define TSU_TIMER_INCR_GEM0 0xff0b01dc
#define TSU_TIMER_INCR_SUB_NS_GEM0 0xff0b01bc
#define TSU_TIMER_SEC_GEM0 0xff0b01d0
#define TSU_TIMER_NS_GEM0 0xff0b01d4
#define GEM_TSU_REF_CTRL 0xff5e0100

#define RX_BD_START_ADDRESS	0x10000000
#define TX_BD_START_ADDRESS	0x10010000



/************************** Variable Definitions *****************************/
volatile u8 SyncSent;
volatile u8 PDelayRespSent;
volatile u32 PTPSendPacket = 0;

XEmacPs_Ieee1588 *GlobalInstancePntr;
// XEmacPs Mac;
extern XEmacPs EmacPsInstance;
static XEmacPs* EmacPsInstancePtr = &EmacPsInstance;
u8 UnicastMAC[]  = {0x00, 0x0a, 0x35, 0x00, 0x01, 0x02};

XEmacPs_Ieee1588 IEEE1588ProtoHandler;


static TickHandlerArgs tickHandlerArgs;

/* Detected link speed goes here. */
int Link_Speed = 1000;
int PhyAddress;

u8 TxBuf[XEMACPS_MAX_VLAN_FRAME_SIZE_JUMBO]
							__attribute__ ((aligned(64)));
u8 RxBuf[XEMACPS_MAX_VLAN_FRAME_SIZE_JUMBO]
							__attribute__ ((aligned(64)));
// u8 RxBuf[XEMACPS_IEEE1588_NO_OF_RX_DESCS][XEMACPS_PACKET_LEN + 2]
// 							__attribute__ ((aligned(64)));

static u8 bd_space[0x100000] __attribute__ ((aligned (0x100000)));

u8 *RxBdSpacePtr;
u8 *TxBdSpacePtr;

XEmacPs_Bd BdTxTerminate __attribute__ ((aligned(64)));

XEmacPs_Bd BdRxTerminate __attribute__ ((aligned(64)));

XEmacPs_Config *EmacCfgPtr;




u8 SrcAddr[6] = {0x00,0x0A,0x35,0x01,0x02,0x09};
u8 DestnAddr[6] = {0x01,0x80,0xC2,0x00,0x00,0x0E};

extern struct netif server_netif;

/************************** Function Prototypes ******************************/

u8 XEmacPs_GetMsgType (u8 *PacketBuf);

static void Init_TSU(void);
static int Init_XEmac(void);

void XEmacPs_PHYSetup (XEmacPs *EmacPsInstancePtr);
int XEmacPs_SetupIntrSystem(XEmacPs *EmacPsInstancePtr, XTtcPs *TimerInstancePtr,
		u16 EmacPsIntrId, u16 TimerIntrId);
void XEmacPs_PtpTxInterruptHandler (XEmacPs_Ieee1588 *InstancePtr);
void XEmacPs_PtpErrorInterruptHandler (XEmacPs_Ieee1588 *InstancePtr,
						u8 Direction, u32 ErrorWord);
void XEmacPs_TimerInterruptHandler(XEmacPs_Ieee1588 *InstancePtr);
void XEmacPs_RunIEEE1588Protocol(XEmacPs *EmacInstance);
void XEmacPs_HandleRecdPTPPacket(XEmacPs_Ieee1588 *InstancePtr);
void XEmacPs_InitializeEmacPsDma (XEmacPs_Ieee1588 *InstancePntr);
void XEmacPs_InitializeProtocolData(XEmacPs_Ieee1588 *InstancePntr);
void XEmacPs_SetDfltTxFrms(XEmacPs_Ieee1588 *InstancePtr);
/*****************************************************************************/
/**
*
* This function extracts PTP messgae type information from the PTP packet.
*
* @param	PacketBuf contains the Ethernet packet.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
u8 XEmacPs_GetMsgType (u8 *PacketBuf)
{
	return PacketBuf[XEMACPS_MSGTYP_OFFSET] & XEMACPS_MSGTYP_MASK;
}

/****************************************************************************/
/**
*
* A function to increment sequence ID in a PTP frames. It first extracts the
* sequence ID and then increments and puts it back in the PTP frame.
*
* @param	PacketBuf contains the PTP buffer from which the portIdentity
*		is to be extracted.
*
* @return	The incremented sequence ID.
*
* @note 	None.
*
*****************************************************************************/
u16 XEmacPs_IncSequenceId(u8 *PacketBuf)
{
	u8 *ReadFromAddr = PacketBuf + XEMACPS_SEQID_OFFSET;
	u8 *WriteToAddr = PacketBuf + XEMACPS_SEQID_OFFSET;
	u16 SequenceId = 0;

	SequenceId = Xil_Ntohs (*((u16 *)ReadFromAddr));
	SequenceId = SequenceId + 1;

	*((u16 *)WriteToAddr) = Xil_Htons (SequenceId);

	return SequenceId;
}

/*****************************************************************************/
/**
*
* This function is initiates a PTP Tx. This is called from various places.
*
* @param	InstancePntr is a pointer to the instance of the
*		XEmacPs_Ieee1588.
* @param	PacketBuf is the buffer that contains the packet to be Txed
* @param	PacketLen is the length of the packet to be Txed.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
int XEmacPs_PtpTxPacket(XEmacPs_Ieee1588 *InstancePtr, u8 *PacketBuf,
							int PacketLen)
{
	int Status;
	XEmacPs_Bd *BdPtr;
	XEmacPs_BdRing *TxRingPtr;
	Xil_ExceptionDisable();

    while((XEmacPs_ReadReg(InstancePtr->EmacPsInstance->Config.BaseAddress,
		XEMACPS_TXSR_OFFSET)) & 0x08);

	TxRingPtr = &(XEmacPs_GetTxRing(InstancePtr->EmacPsInstance));
	Status = XEmacPs_BdRingAlloc(TxRingPtr, 1, &BdPtr);
	if (Status != XST_SUCCESS) {
		Xil_ExceptionEnable();
		return XST_FAILURE;
	}
	/*
	 * Fill the BD entries for the Tx1!!1`
	 */
	Xil_DCacheFlushRange((UINTPTR)((u32)PacketBuf), 128);

	XEmacPs_BdSetAddressTx (BdPtr, (UINTPTR)(PacketBuf));
	XEmacPs_BdSetLength(BdPtr, PacketLen);
	XEmacPs_BdClearTxUsed(BdPtr);
	XEmacPs_BdSetLast(BdPtr);
	dmb();
	dsb();

	Status = XEmacPs_BdRingToHw(TxRingPtr, 1, BdPtr);
	if (Status != XST_SUCCESS) {
		Xil_ExceptionEnable();
		return XST_FAILURE;
	}
	dmb();
	dsb();
	/*
	 * Start the Tx
	 */
	XEmacPs_Transmit(InstancePtr->EmacPsInstance);
	Xil_ExceptionEnable();
	return XST_SUCCESS;
}

/****************************************************************************/
/**
*
* A function to extract SequenceId information from a received PTP frame.
*
* @param	PacketBuf contains the PTP buffer from which the portIdentity
*		is to be extracted.
*
* @return	The extracted sequence ID.
*
* @note 	None.
*
*****************************************************************************/
u16 XEmacPs_GetSequenceId(u8 *PacketBuf)
{
	u8 *ReadFromAddr = PacketBuf + XEMACPS_SEQID_OFFSET;
	u16 SequenceId;

	SequenceId = Xil_Htons (*((u16 *)ReadFromAddr));
	return SequenceId;
}

/****************************************************************************/
/**
*
* A function to extract portIdentity information from a received PTP frame.
* This can be any portIdentity field (header portIdentity,
* requestingPortIdentity etc...)
*
* @param	PacketBuf contains the PTP buffer from which the portIdentity
*		is to be extracted.
* @param	portID is the address of type XEmacPs_PortIdentity is which
*		the extracted port identity is put.
*
* @return	None.
*
* @note 	None.
*
*****************************************************************************/
void XEmacPs_GetPortIdentity(u8 *PacketBuf, XEmacPs_PortIdentity *portID)
{
	XEmacPs_PortIdentity TempIdentity;
	u8 *ReadFromAddr = PacketBuf + XEMACPS_PORTIDENTITY_OFFSET;

	memcpy ((u8 *)&(TempIdentity.ClockIdentity[0]), ReadFromAddr, 8);
	TempIdentity.PortNumber = Xil_Ntohs (*(u16 *)(ReadFromAddr + 8));

	memcpy ((u8 *)portID, (u8 *)&TempIdentity,
					sizeof(XEmacPs_PortIdentity));

}

/*****************************************************************************/
/**
*
* Calculate clock configuration register values for indicated input clock
*
* @param	- Freq
*
* @return
*		- XST_SUCCESS to indicate success.
*		- XST_FAILURE to indicate failure
*
* @note		None.
*
******************************************************************************/
unsigned int XEmacPs_TsuCalcClk(u32 Freq)
{
	u64 Period_ns = (NS_PER_SEC * FP_MULT)/Freq;
	unsigned Retval;

	Period_ns = (NS_PER_SEC * FP_MULT)/Freq;
	Retval = Period_ns / FP_MULT;
	return Retval;
}

/****************************************************************************/
/**
*
* A function to compare two PortIdentity values.
*
* @param	Identity1 is the first sourcePortIdentity to be compared
* @param	Identity2 is the second sourcePortIdentity to be compared
*
* @return
*		- 1 if the two values are equal
*		- 0 if not equal
*
* @note 	None.
*
*****************************************************************************/
u32 XEmacPs_ComparePortIdentity(XEmacPs_PortIdentity Identity1,
				XEmacPs_PortIdentity Identity2)
{

	int Result = memcmp (&(Identity1.ClockIdentity[0]),
				&(Identity2.ClockIdentity[0]),
				8);
	if (Result != 0) {
		return 0;
	}

	if( Identity1.PortNumber != Identity2.PortNumber ) {
		return 0;
	}

	/* values are equal */
	return 1;
}

/****************************************************************************/
/**
*
* A function to compare two ClockIdentity values.
*
* @param	Identity1 is the first ClockIdentity to be compared
* @param	Identity2 is the second ClockIdentity to be compared
*
* @return
*		- 1 if the two values are equal
*		- 0 if not equal
*
* @note 	None.
*
*****************************************************************************/
u32 XEmacPs_CompareClockIdentity(
				XEmacPs_ClockIdentity Identity1,
				XEmacPs_ClockIdentity Identity2)
{
	int Result = memcmp (&(Identity1.ClockIdentity[0]),
				&(Identity2.ClockIdentity[0]),
				8);
	if (Result != 0) {
		return 0;
	}
	/* values are equal */
	return 1;
}



int InitTtcTimer()
{
	int Status;
	/*
	 * Connect the Intc to the interrupt subsystem such that interrupts can
	 * occur.  This function is application specific.
	 */


	Status = Setup_Ticker(XEmacPs_TimerInterruptHandler);
	if (Status != XST_SUCCESS) {
		return Status;
	}
	XTtcPs_EnableInterrupts(tickHandlerArgs.TtcPsTick, XTTCPS_IXR_INTERVAL_MASK);
	return Status;
}

void Init_TSU()
{
	u32 value = 0x00010601;
	Xil_Out32(GEM_TSU_REF_CTRL, value);
	value = 0x00010601 | 0x01000000;
	Xil_Out32(GEM_TSU_REF_CTRL, value);

	/* Nanosecond and sub-nanosecond increments per clock cycle */
	double ns_per_cycle = 1e9 / TSU_CLK_FREQ;
	u32 ns_int = (u32)ns_per_cycle;
	u32 sub_ns = (u32)((ns_per_cycle - ns_int) * (1 << 24)); // 24位小数精度
	xil_printf("write TSU_TIMER_INCR_GEM0 :: 0x%2.2x\r\nTSU_TIMER_INCR_SUB_NS_GEM0 :: 0x%2.2x\r\n", ns_int, sub_ns);
	Xil_Out32(TSU_TIMER_INCR_GEM0, ns_int);   // 0x04
	Xil_Out32(TSU_TIMER_INCR_SUB_NS_GEM0, sub_ns); // 0x00
	
	/*	clear tsu counter	*/
	Xil_Out32(TSU_TIMER_NS_GEM0, 0);
	Xil_Out32(TSU_TIMER_SEC_GEM0, 0);
}

/*****************************************************************************/
/**
*
* This function is the EmacPs Rx interrupt callback invoked from the EmacPs
* driver. Here we set the flag PtpNewPktRecd to true. This flag is checked for
* in the function XEmacPs_RunIEEE1588Protocol for further processing of
* packets.
*
* @param	InstancePntr is a pointer to the instance of the
*		XEmacPs_Ieee1588.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XEmacPs_PtpRxInterruptHandler(XEmacPs_Ieee1588 *InstancePtr)
{
  xil_printf("eth recv new ... \r\n");
	InstancePtr->PtpNewPktRecd = TRUE;
}


/*****************************************************************************/
/**
*
* This function finds out if a PHY is connected or not and if connected what
* is the PHY address of the connected PHY.
*
* @param	EmacPsInstancePtr is a pointer to the instance of the EmacPs.
*
* @return
*		- Detected PHY address if successful.
*		- 0 if no PHY is connected.
*
* @note		None.
*
******************************************************************************/
unsigned long XEmacPs_DetectPHY(XEmacPs *EmacPsInstancePtr)
{
	u16 PhyReg;
	int PhyAddr;

	for (PhyAddr = 31; PhyAddr >= 0; PhyAddr--) {
		XEmacPs_PhyRead(EmacPsInstancePtr, PhyAddr, PHY_DETECT_REG,
								&PhyReg);

		if ((PhyReg != 0xFFFF) && ((PhyReg & PHY_DETECT_MASK) ==
							PHY_DETECT_MASK)) {
#ifdef DEBUG_XEMACPS_LEVEL1
			xil_printf("In %s: Detected PHY address is %d \r\r\n",
							__func__, PhyAddr);
#endif
			return (u32) PhyAddr;
		}
	}
	return 0; /* default to zero */
}


/*****************************************************************************/
/**
*
* This function is the Tx Done interrupt callback invoked from the EmacPs
* driver.
* For some PTP packets, upon getting a Tx done interrupt some actions need
* to be taked. For example, when SYNC frame is successfully sent and Tx Done
* interrupt is received, the time stamp for the SYNC frame needs to be stored.
* For all such processing the function XEmacPs_PtpTxDoFurtherProcessing is
* invoked from here.
*
* @param	InstancePntr is a pointer to the instance of the
*		XEmacPs_Ieee1588.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XEmacPs_PtpTxInterruptHandler (XEmacPs_Ieee1588 *InstancePtr)
{
	unsigned int NumBds;
	unsigned int NumBdsToProcess;
	XEmacPs_Bd *BdPtr, *CurBdPtr;
	void *BufAddr;
	int BufLen;
	int Status;
	XEmacPs_BdRing *TxRingPtr;
	unsigned int *Temp;

	TxRingPtr = &XEmacPs_GetTxRing(InstancePtr->EmacPsInstance);

	NumBds = XEmacPs_BdRingFromHwTx( TxRingPtr,
		XEMACPS_IEEE1588_NO_OF_TX_DESCS, &BdPtr);
	if (NumBds == 0) {
			return;
	}
	NumBdsToProcess = NumBds;
	CurBdPtr=BdPtr;
	while (NumBdsToProcess > 0) {
		BufAddr = (void*)(INTPTR)(XEmacPs_BdGetBufAddr(CurBdPtr));
		BufLen = XEmacPs_BdGetLength(CurBdPtr);

		XEmacPs_PtpTxDoFurtherProcessing (InstancePtr, (u8 *)BufAddr);
		Temp = (unsigned int *)CurBdPtr;
		Temp++;
		*Temp &= XEMACPS_TXBUF_WRAP_MASK;
		*Temp |= XEMACPS_TXBUF_USED_MASK;

		CurBdPtr = XEmacPs_BdRingNext(TxRingPtr, CurBdPtr);
		NumBdsToProcess--;
		dmb();
		dsb();
	}
	Status = XEmacPs_BdRingFree( TxRingPtr, NumBds, BdPtr);
	if (Status != XST_SUCCESS) {
		return;
	}
	return;
}

/*****************************************************************************/
/**
*
* This function is the Error interrupt callback invoked from the EmacPs driver.
*
* @param	InstancePntr is a pointer to the instance of the
*		XEmacPs_Ieee1588.
* @param	Direction can be Rx or Tx
* @param	ErrorWord gives further information about the exact error type.
*
* @return	None.
*
* @note		This function needs to be revisited. Probably upon an error
*		we need to reset the EmacPs hardware and reinitialize the BDs.
*		However further study is needed. Whether for all errors we
*		need to reset or some errors can be ignored.
*
******************************************************************************/
void XEmacPs_PtpErrorInterruptHandler (XEmacPs_Ieee1588 *InstancePtr,
						u8 Direction, u32 ErrorWord)
{
	XEmacPs_Config *Cfg;
	int Status = XST_SUCCESS;
	unsigned int NSIncrementVal;

#ifdef DEBUG_XEMACPS_LEVEL1
	xil_printf("In %s: EMAC Error Interrupt, Direction is %d and ErrorWord is %x \r\r\n",
					__func__, Direction, ErrorWord);
	switch (Direction) {
	case XEMACPS_RECV:
		if (ErrorWord & XEMACPS_RXSR_HRESPNOK_MASK) {
			xil_printf("Receive DMA error\r\n");
		}
		if (ErrorWord & XEMACPS_RXSR_RXOVR_MASK) {
			xil_printf("Receive over run\r\n");
		}
		if (ErrorWord & XEMACPS_RXSR_BUFFNA_MASK) {
			xil_printf("Receive buffer not available\r\n");
		}
		break;
	case XEMACPS_SEND:
		if (ErrorWord & XEMACPS_TXSR_HRESPNOK_MASK) {
			xil_printf("Transmit DMA error\r\n");
		}
		if (ErrorWord & XEMACPS_TXSR_URUN_MASK) {
			xil_printf("Transmit under run\r\n");
		}
		if (ErrorWord & XEMACPS_TXSR_BUFEXH_MASK) {
			xil_printf("Transmit buffer exhausted\r\n");
		}
		if (ErrorWord & XEMACPS_TXSR_RXOVR_MASK) {
			xil_printf("Transmit retry excessed limits\r\n");
		}
		if (ErrorWord & XEMACPS_TXSR_FRAMERX_MASK) {
			xil_printf("Transmit collision\r\n");
		}
		if (ErrorWord & XEMACPS_TXSR_USEDREAD_MASK) {
			xil_printf("Transmit buffer not available\r\n");
		}
		break;
	}
#endif
	TtcPs_Stop();  // 使用的ttc
	XEmacPs_Stop(InstancePtr->EmacPsInstance);
	Xil_ExceptionDisable();
	PDelayRespSent = 0;
	SyncSent = 0;
	PTPSendPacket = 0;
	memset(RxBuf, 0, sizeof(RxBuf));
	Link_Speed = 1000;
	/* Initialize SCUTIMER */
	if (InitTtcTimer()  != XST_SUCCESS) while(1);
	/*
	 * Get the configuration of EmacPs hardware.
	 */
	Cfg = XEmacPs_LookupConfig(EMACPS_DEVICE_ID);

	/*
	 * Initialize EmacPs hardware.
	 */
	Status = XEmacPs_CfgInitialize(InstancePtr->EmacPsInstance, Cfg, Cfg->BaseAddress);
	if (Status != XST_SUCCESS) {
#ifdef DEBUG_XEMACPS_LEVEL1
		xil_printf("In function %s: XEmacPs_CfgInitialize failure \r\r\n",__func__);
#endif
	}

	/*
	 * Set the MAC address
	 */
	Status = XEmacPs_SetMacAddress(InstancePtr->EmacPsInstance,
					(unsigned char*)UnicastMAC, 1);
	if (Status != XST_SUCCESS) {
#ifdef DEBUG_XEMACPS_LEVEL1
		xil_printf("In function %s: XEmacPs_SetMacAddress failure \r\r\n",__func__);
#endif
	}

	XEmacPs_SetMdioDivisor(InstancePtr->EmacPsInstance, MDC_DIV_224);

	/*
	 * Detect and initialize the PHY
	 */
	XEmacPs_PHYSetup (InstancePtr->EmacPsInstance);
	sleep(1);

	/*
	 * Set the operating speed in EmacPs hardware.
	 */
	XEmacPs_SetOperatingSpeed(InstancePtr->EmacPsInstance, Link_Speed);
	sleep(1);

	/*
	 * Enable the promiscuous mode in EmacPs hardware.
	 */
	Status = XEmacPs_SetOptions(InstancePtr->EmacPsInstance, XEMACPS_PROMISC_OPTION);
	if (Status != XST_SUCCESS) {
#ifdef DEBUG_XEMACPS_LEVEL1
		xil_printf("In function %s: XEmacPs_SetOptions failure \r\r\n",__func__);
#endif
		return;
	}


	NSIncrementVal = XEmacPs_TsuCalcClk(XPAR_CPU_CORTEXA53_0_CPU_CLK_FREQ_HZ / 6);
	XEmacPs_WriteReg(InstancePtr->EmacPsInstance->Config.BaseAddress,
				XEMACPS_1588_INC_OFFSET,
				NSIncrementVal);

	/*
	 * Register Ethernet Rx, Tx and Error handlers with the EmacPs driver.
	 */
	Status = XEmacPs_SetHandler (InstancePtr->EmacPsInstance,
				XEMACPS_HANDLER_DMARECV,
				(void *)XEmacPs_PtpRxInterruptHandler,
				&IEEE1588ProtoHandler);
	Status |= XEmacPs_SetHandler (InstancePtr->EmacPsInstance,
				XEMACPS_HANDLER_DMASEND,
				(void *)XEmacPs_PtpTxInterruptHandler,
				&IEEE1588ProtoHandler);
	Status |= XEmacPs_SetHandler (InstancePtr->EmacPsInstance,
				XEMACPS_HANDLER_ERROR,
				(void *)XEmacPs_PtpErrorInterruptHandler,
				&IEEE1588ProtoHandler);
	if (Status != XST_SUCCESS) {
#ifdef DEBUG_XEMACPS_LEVEL1
		xil_printf("In function %s: XEmacPs_SetHandler failure \r\r\n",
							__func__);
#endif
	}
	/*
	 * Connect to the interrupt controller and enable interrupts in
	 * interrupt controller.
	 */
	Status = XEmacPs_SetupIntrSystem(InstancePtr->EmacPsInstance,
					tickHandlerArgs.TtcPsTick, EMACPS_IRPT_INTR,
					TIMER_IRPT_INTR);
	if (Status != XST_SUCCESS) {
#ifdef DEBUG_XEMACPS_LEVEL1
		xil_printf("In function %s: XEmacPs_SetupIntrSystem failure \r\r\n",
							__func__);
#endif
	}
	Xil_ExceptionEnable();
	/*
	 * Enable the timer interrupt in the timer module
	 */
	XTtcPs_EnableInterrupts(tickHandlerArgs.TtcPsTick, XTTCPS_IXR_INTERVAL_MASK);
	/*
	 * Start the PTP standalone state machine.
	 */
	XEmacPs_RunIEEE1588Protocol(InstancePtr->EmacPsInstance);
}

/*****************************************************************************/
/**
*
* This function configures the PHY with proper speed settings and set up the
* PHY to be used subsequently.
*
* @param	EmacPsInstancePtr is a pointer to the instance of the EmacPs.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XEmacPs_PHYSetup (XEmacPs *EmacPsInstancePtr)
{
	u16 Control = 0;
	u16 Status;
	u32 SlcrTxClkCntrl;

	u16 PhyReg0  = 0;
	/*
	 * Detect the connected PHY and get the PHY address.
	 */
	PhyAddress = XEmacPs_DetectPHY(EmacPsInstancePtr);
	Status  = XEmacPs_PhyRead(EmacPsInstancePtr, PhyAddress, 0, &PhyReg0);
	PhyReg0 &= (~IEEE_CTRL_AUTONEGOTIATE_ENABLE);
	Status  |= XEmacPs_PhyWrite(EmacPsInstancePtr, PhyAddress, 0, PhyReg0);

	Status  |= XEmacPs_PhyRead(EmacPsInstancePtr, PhyAddress, 0, &PhyReg0);
	Status  |= XEmacPs_PhyWrite(EmacPsInstancePtr, PhyAddress, 0,
						(PhyReg0 |PHY_R0_RESET));
	sleep(1);
	XEmacPs_PhyWrite(EmacPsInstancePtr,PhyAddress, 22, 2);
	Control |= PHY_REG21_100;
	XEmacPs_PhyWrite(EmacPsInstancePtr, PhyAddress, 21, Control);
	XEmacPs_PhyWrite(EmacPsInstancePtr, PhyAddress, 22, 0);

	/*
	 * Set the PHY speed and reset the PHY.
	 */
	Status  = XEmacPs_PhyRead(EmacPsInstancePtr, PhyAddress, 0, &PhyReg0);
	PhyReg0 = PHY_REG0_100;
	Status  |= XEmacPs_PhyWrite(EmacPsInstancePtr, PhyAddress, 0, PhyReg0);

	Status  |= XEmacPs_PhyRead(EmacPsInstancePtr, PhyAddress, 0, &PhyReg0);
	Status  |= XEmacPs_PhyWrite(EmacPsInstancePtr, PhyAddress, 0,
						(PhyReg0 |PHY_R0_RESET));
	sleep(1);
	*(volatile unsigned int *)(SLCR_UNLOCK_ADDR) = SLCR_UNLOCK_KEY_VALUE;
	SlcrTxClkCntrl = *(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR);
	SlcrTxClkCntrl &= EMACPS_SLCR_DIV_MASK;
	SlcrTxClkCntrl |= (XPAR_PSU_ETHERNET_0_ENET_SLCR_1000MBPS_DIV1 << 20);
	SlcrTxClkCntrl |= (XPAR_PSU_ETHERNET_0_ENET_SLCR_1000MBPS_DIV0 << 8);
	*(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR) = SlcrTxClkCntrl;
	*(volatile unsigned int *)(SLCR_LOCK_ADDR) = SLCR_LOCK_KEY_VALUE;
	sleep(1);
}

/*****************************************************************************/
/**
*
* This function is the timer ISR that is invoked every 500 mseconds. The Tx of
* PTP packets are initiated from here at appropriate intervals. When the PTP
* node is Master, the Tx of SYNC frame is triggered from here. Similarly
* Announce frame and PDelayReq frame Tx are triggered from here.
* When the PTP node is Slave, the PDelayReq frame Tx is triggered from here.
*
*
* @param	InstancePntr is a pointer to the instance of the
*		XEmacPs_Ieee1588.
*
* @return	None.
*
* @note		The intervals at which SYNC, ANNOUNCE and PDelayReq are
*		triggered are hard coded to 1 sec, 5 seconds and 4 seconds
*		respectively. When signalling frames are implemented
*		the hardcoded values can be replaced with proper signalling
*		frame values.
*
******************************************************************************/

void XEmacPs_TimerInterruptHandler(XEmacPs_Ieee1588 *InstancePtr)
{
	xil_printf("InstancePtr->PtpIsRunning :: %d\r\n", InstancePtr->PtpIsRunning);
	/*
	 * If PTP functions are marked as not running, then take no
	 * further action
	 */
	if (InstancePtr->PtpIsRunning == 1) {

		/*
		 * If the Link Partner is not Ieee1588 capable, then take no
		 * further action
		 */
		if (InstancePtr->PeerIeee1588v2Capable == 1) {
			/*
			 * If a Master, then initiate Sync Frames and
			 * Announce frames at the correct intervals
			 */
			if (InstancePtr->CurrentBmc.IAmTheRtcMaster == 1) {

				/*
				 * Master will initiate a Sync Frame when the
				 * SyncIntervalDuration (1 second) expires
				 * (SyncIntervalDuration is used to count/time
				 * the duration)
				 */
				if ((InstancePtr->SignallingFrameData.
					SyncIntervalDuration !=
					XEMACPS_PKT_TYPE_DISABLED) &&
					(InstancePtr->PtpCounters.
					CounterSyncInterval >=
					(InstancePtr->SignallingFrameData.
					SyncIntervalDuration - 1))) {
					XEmacPs_MasterSendSync(InstancePtr);
					InstancePtr->
					PtpCounters.CounterSyncInterval = 0;

				} else {
					InstancePtr->PtpCounters.
						CounterSyncInterval
						= InstancePtr->
						PtpCounters.CounterSyncInterval
						+ 1;
				}

				/*
				 * Master will initiate an Announce Frame when
				 * the AnnounceIntervalDuration (5 secs)
				 * expires (CounterAnnounceInterval is used to
				 * count/time the duration)
				 */
				if ((InstancePtr->SignallingFrameData.
					AnnounceIntervalDuration
					!= XEMACPS_PKT_TYPE_DISABLED) &&
					(InstancePtr->
					PtpCounters.CounterAnnounceInterval >=
					(InstancePtr->SignallingFrameData.
					AnnounceIntervalDuration - 1))) {

					XEmacPs_MasterSendAnnounce(InstancePtr);
					InstancePtr->
					PtpCounters.CounterAnnounceInterval
								= 0;

				} else {
					InstancePtr->
					PtpCounters.CounterAnnounceInterval
						= InstancePtr->PtpCounters.
						CounterAnnounceInterval + 1;
				}

			/*
			 * If a Slave, monitor Announce/Sync Packet reception
			 * from the Master
			 */
			} else {

				/*
				 * Timeout for Announce Packet reception:
				 * XEMACPS_ANNOUNCE_RECEIPT_TIMEOUT. The
				 * AnnounceIntervalDuration is stored with the
				 * GrandMaster BMCA data as it is captured
				 * from the last Announce frame that was
				 * received.
				 */
				if (InstancePtr->PtpCounters.
					CounterAnnounceInterval >=
					((InstancePtr->CurrentBmc.
					AnnounceIntervalDuration - 1) *
					XEMACPS_ANNOUNCE_RECEIPT_TIMEOUT) ) {

#ifdef DEBUG_LEVEL_TWO
					xil_printf("XEMACPS_ANNOUNCE_RECEIPT_TIMEOUT: Becoming GM! CounterAnnounceInterval = %d\r\r\n",
					InstancePtr->
					PtpCounters.CounterAnnounceInterval);
#endif
					InstancePtr->
					PtpCounters.CounterAnnounceInterval
								= 0;

#ifdef DEBUG_LEVEL_TWO
					/*
					 * No Announce received from GM for timeout interval:
					 * we become the master */
					xil_printf("\r\r\n*** Announce timeout : Call XEmacPs_BecomeRtcSlave() *** \r\r\n");
#endif
					XEmacPs_BecomeRtcSlave(InstancePtr);

				} else {
					InstancePtr->
					PtpCounters.CounterAnnounceInterval
					= InstancePtr->
					PtpCounters.CounterAnnounceInterval
					+ 1;
				}

				/*
				 * Timeout for Sync Packet reception:
				 * XEMACPS_SYNC_RECEIPT_TIMEOUT *
				 * The SyncIntervalDuration is stored with the
				 * Received Sync data as it is captured from
				 * the last Sync frame that was received.
				 */
				if( InstancePtr->PtpCounters.
							CounterSyncInterval >=
					((InstancePtr->LatestMDSyncReceive.
						SyncIntervalDuration - 1) *
					XEMACPS_SYNC_RECEIPT_TIMEOUT) ) {
#ifdef DEBUG_LEVEL_TWO
					xil_printf("\r\r\nXEMACPS_SYNC_RECEIPT_TIMEOUT: Becoming GM! CounterSyncInterval = %d\r\r\n",
					InstancePtr->PtpCounters.
							CounterSyncInterval);
					xil_printf("\r\r\nXEMACPS_SYNC_RECEIPT_TIMEOUT: SyncIntervalDuration = %d\r\r\n",
					InstancePtr->SignallingFrameData.
							SyncIntervalDuration);
#endif
					InstancePtr->
					PtpCounters.CounterSyncInterval = 0;
					/*
					 * No Syncs received from GM for timeout interval: we become
					 * the master
					 */
#ifdef DEBUG_LEVEL_TWO
					xil_printf("\r\r\n*** Sync Timeout : Call XEmacPs_BecomeRtcSlave() *** \r\r\n");
#endif
					XEmacPs_BecomeRtcSlave(InstancePtr);

				} else {
					InstancePtr->
					PtpCounters.CounterSyncInterval
					= InstancePtr->
					PtpCounters.CounterSyncInterval + 1;
				}
			}
		}

		/*
		 * Both Master and Slave will initiate a link delay
		 * measurement when the LinkDelayIntervalDuration (4 secs)
		 * expires (LinkDelayIntervalDuration is used to
		 * count/time the duration)
		 */
		if ((InstancePtr->SignallingFrameData.LinkDelayIntervalDuration
			!= XEMACPS_PKT_TYPE_DISABLED) &&
			(InstancePtr->PtpCounters.CounterLinkDelayInterval >=
			(InstancePtr->
			SignallingFrameData.LinkDelayIntervalDuration - 1))) {
			/*
			 * Check to see if we've received PDelayResp and
			 * PDelayRespFollowUp messages since the last
			 * PDelayReq was sent
			 */
			if( InstancePtr->StateMachineData.RcvdPDelayResp &&
			InstancePtr->StateMachineData.RcvdPDelayRespFollowUp ){

				InstancePtr->StateMachineData.LostResponses
									= 0;
			} else {
				InstancePtr->StateMachineData.LostResponses++;
			}

			if( InstancePtr->StateMachineData.LostResponses >=
					XEMACPS_ALLOWED_LOST_RESPONSES ) {
				/*
				 * The peer is no longer Ieee1588v2 Capable
				 */
				XEmacPs_ChangePeerIeee1588v2Capability
							(InstancePtr, 0);

#ifdef DEBUG_LEVEL_TWO
				xil_printf("\r\r\n** XEmacPs_PtpTimerInterruptHandler(): The peer is no longer ASCapable **");
				xil_printf("\r\r\n** XEmacPs_PtpTimerInterruptHandler(): StateMachineData.LostResponses >= %d **",
					XEMACPS_ALLOWED_LOST_RESPONSES);
#endif
				/* avoid potential overflow */
				InstancePtr->StateMachineData.LostResponses =
						XEMACPS_ALLOWED_LOST_RESPONSES;
			}

			XEmacPs_SendPDelayReq(InstancePtr);

			InstancePtr->StateMachineData.RcvdPDelayResp = 0;
			InstancePtr->StateMachineData.RcvdPDelayRespFollowUp
								= 0;
			InstancePtr->PtpCounters.CounterLinkDelayInterval
								= 0;

		} else {
			InstancePtr->PtpCounters.CounterLinkDelayInterval
			= InstancePtr->PtpCounters.CounterLinkDelayInterval
									+ 1;
		}

	} /* end of 'if (InstancePtr->PtpIsRunning == 1)' */

}

/*****************************************************************************/
/**
*
* This function sets up interrupts. It registers interrupt handlers and then
* enables them..
*
* @param	EmacPsInstancePtr is a pointer to the instance of the EmacPs.
* @param	TimerInstancePtr is a pointer to the instance of the SCUTimer.
* @param	EmacPsIntrId is the Interrupt ID for EmacPs and the value
*		used is taken from xparameters_ps.h.
* @param	TimerIntrId is the Interrupt ID for SCUTimer and the value
*		used is taken from xparameters_ps.h.
*
* @return
*		- XST_SUCCESS to indicate success.
*		- XST_FAILURE to indicate failure
*
* @note		None.
*
******************************************************************************/
int XEmacPs_SetupIntrSystem(XEmacPs *EmacPsInstancePtr, XTtcPs *TimerInstancePtr,
			u16 EmacPsIntrId, u16 TimerIntrId)
{
	int Status = XST_SUCCESS;
	#if 0
	XScuGic_Config *GicConfig;
	Xil_ExceptionInit();

	/*
	 * Initialize the interrupt controller driver so that it is ready to
	 * use.
	 */
	GicConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == GicConfig) {
		return XST_FAILURE;
	}
	Status = XScuGic_CfgInitialize(&IntcInstance, GicConfig,
	GicConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	/*
	 * Connect the interrupt controller interrupt handler to the hardware
	 * interrupt handling logic in the processor.
	 */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
			(Xil_ExceptionHandler)XScuGic_InterruptHandler,
			IntcInstancePtr);
	/*
	 * Connect the EmacPs device driver handler that will be called when an
	 * interrupt for the device occurs. The device driver handler performs
	 * the specific interrupt processing for the device.
	 */
	#endif
	Status = InterruptConnect(EmacPsIntrId,
			(Xil_InterruptHandler) XEmacPs_IntrHandler, //   
			(void *) EmacPsInstancePtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Connect the handler for timer interrupt that will be called when the
	 * timer.expires.
	 */
	Status = InterruptConnect(TimerIntrId,
			(Xil_ExceptionHandler) TickHandler,// XEmacPs_TimerInterruptHandler
			(void *)&tickHandlerArgs);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	
  // enable interrupts mac  @test
  XEmacPs_IntEnable(EmacPsInstancePtr, XEMACPS_IXR_ALL_MASK);
	/*
	 * Enable interrupts in the processor
	 */
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);

	return XST_SUCCESS;
}


int Init_XEmac(void)
{
  int Status = XST_SUCCESS;
	XEmacPs_Config *Cfg;
	unsigned int NSIncrementVal;

	/*
  * Get the configuration of EmacPs hardware.
	 */
	Cfg = XEmacPs_LookupConfig(EMACPS_DEVICE_ID);
	EmacCfgPtr = Cfg;
	/*
	 * Initialize EmacPs hardware.
	 */
	#if 1
	Status = XEmacPs_CfgInitialize(EmacPsInstancePtr, Cfg,
							Cfg->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	/*
	 * Set the MAC address
	 */
	Status = XEmacPs_SetMacAddress(EmacPsInstancePtr,
					(unsigned char*)UnicastMAC, 1);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	XEmacPs_SetMdioDivisor(EmacPsInstancePtr, MDC_DIV_224); // 设置管理数据输入输出 时钟分频系数
	// XEmacPs_SetMdioDivisor(EmacPsInstancePtr, MDC_DIV_128); // 设置管理数据输入输出 时钟分频系数
	/*
	 * Detect and initialize the PHY
	 */
	XEmacPs_PHYSetup (EmacPsInstancePtr);  
	sleep(1);

	/*
	 * Set the operating speed in EmacPs hardware.
	 */
	XEmacPs_SetOperatingSpeed(EmacPsInstancePtr, Link_Speed);
	sleep(1);
	/*
	 * Enable the promiscuous mode in EmacPs hardware.
	 */
	Status = XEmacPs_SetOptions(EmacPsInstancePtr, XEMACPS_PROMISC_OPTION);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	/* Enable jumbo frames for zynqmp */
	XEmacPs_SetOptions(EmacPsInstancePtr, XEMACPS_JUMBO_ENABLE_OPTION);

	#endif

	NSIncrementVal = XEmacPs_TsuCalcClk
				(XPAR_CPU_CORTEXA53_0_CPU_CLK_FREQ_HZ / 6);

	XEmacPs_WriteReg(EmacPsInstancePtr->Config.BaseAddress,
				XEMACPS_1588_INC_OFFSET,
				NSIncrementVal);  // 这里还会设置一下TSU  感觉上面的init tsu可以去掉
	/*
	* Register Ethernet Rx, Tx and Error handlers with the EmacPs driver.
	*/
	Status = XEmacPs_SetHandler (EmacPsInstancePtr,
				XEMACPS_HANDLER_DMARECV,
				(void *)XEmacPs_PtpRxInterruptHandler,
				&IEEE1588ProtoHandler);

	Status |= XEmacPs_SetHandler (EmacPsInstancePtr,
				XEMACPS_HANDLER_DMASEND,
				(void *)XEmacPs_PtpTxInterruptHandler,
				&IEEE1588ProtoHandler);
	Status |= XEmacPs_SetHandler (EmacPsInstancePtr,
				XEMACPS_HANDLER_ERROR,
				(void *)XEmacPs_PtpErrorInterruptHandler,
				&IEEE1588ProtoHandler);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

		/*
	 * Connect to the interrupt controller and enable interrupts in
	 * interrupt controller.
	 */
	
	Status = XEmacPs_SetupIntrSystem(EmacPsInstancePtr,
					tickHandlerArgs.TtcPsTick, EMACPS_IRPT_INTR,
					TIMER_IRPT_INTR);
	if (Status != XST_SUCCESS) {
    xil_printf("XEmacPs_SetupIntrSystem return error  %d", Status);
		return XST_FAILURE;
	}
	/*
	 * Enable the timer interrupt in the timer module
	 */
	// XTtcPs_EnableInterrupts(tickHandlerArgs.TtcPsTick, XTTCPS_IXR_INTERVAL_MASK);
  // test
  
}


/*****************************************************************************/
/**
*
* This function is used to populate the PTP Tx buffers. Some of the entries
* in a PTP Tx packet do not change with time and are constants over time for
* this PTP standalone example. Those entries are populated here.
*
* @param	InstancePntr is a pointer to the instance of the
*		XEmacPs_Ieee1588.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XEmacPs_SetDfltTxFrms(XEmacPs_Ieee1588 *InstancePtr)
{
	u8 TemplateFrm[50];
	u16 *TempPntr;
	XEmacPs_PortIdentity TempIdentity;
	u32 GmClkQuality = 0xF8FE4100;
	u32 *TempLongPntr;
	u8 OrgId[3] = {0x00, 0x80, 0xC2};
	u8 OrgSubType[3] = {0x00, 0x00, 0x01};

	/*
	 * Zero out the template frame. Template frame is used to clone all
	 * the Tx buffers.
	 */
	memset (&TemplateFrm[0],0,sizeof(TemplateFrm));
	memset (InstancePtr->SyncFrmToTx,0,sizeof(InstancePtr->SyncFrmToTx));
	memset (InstancePtr->FollowUpFrmToTx,0,
					sizeof(InstancePtr->FollowUpFrmToTx));
	memset (InstancePtr->PDelayReqFrmToTx,0,
					sizeof(InstancePtr->PDelayReqFrmToTx));
	memset (InstancePtr->PDelayRespFrmToTx,0,
				sizeof(InstancePtr->PDelayRespFrmToTx));
	memset (InstancePtr->PDelayRespFollowUpFrmToTx, 0,
				sizeof(InstancePtr->PDelayRespFollowUpFrmToTx));
	memset (InstancePtr->SignallingFrmToTx, 0,
				sizeof(InstancePtr->SignallingFrmToTx));
	memset (InstancePtr->AnnounceFrmToTx, 0,
				sizeof(InstancePtr->AnnounceFrmToTx));

	/*
	 * Populate the template frame with source and destination MAC.
	 */
	memcpy(&TemplateFrm[0],DestnAddr,6);
	memcpy(&TemplateFrm[6],SrcAddr,6);

	/*
	 * Populate the template frame with source and destination MAC.
	 */
	TempPntr = (u16 *)(&(TemplateFrm[12]));
	*TempPntr = Xil_Htons (XEMACPS_PTP_ETHERTYPE);

	/*
	 * Populate the template frame with PTP version
	 */
	TemplateFrm[XEMACPS_VERSPTP_OFFSET] = XEMACPS_PTP_VERSION_PTP;
#ifdef IEEE1588_MASTER
	/*
	 * Populate the template frame with Port identity
	 */
	TempIdentity.ClockIdentity[0] = 0x00;
	TempIdentity.ClockIdentity[1] = 0x0A;
	TempIdentity.ClockIdentity[2] = 0x35;
	TempIdentity.ClockIdentity[3] = 0xFF;
	TempIdentity.ClockIdentity[4] = 0xFE;
	TempIdentity.ClockIdentity[5] = 0x01;
	TempIdentity.ClockIdentity[6] = 0x02;
	TempIdentity.ClockIdentity[7] = 0x03;
	TempIdentity.PortNumber = Xil_Htons (0x0001);
#else
	/*
	 * Populate the template frame with Port identity
	 */
	TempIdentity.ClockIdentity[0] = 0x00;
	TempIdentity.ClockIdentity[1] = 0x0A;
	TempIdentity.ClockIdentity[2] = 0x35;
	TempIdentity.ClockIdentity[3] = 0xFF;
	TempIdentity.ClockIdentity[4] = 0xFE;
	TempIdentity.ClockIdentity[5] = 0x01;
	TempIdentity.ClockIdentity[6] = 0x02;
	TempIdentity.ClockIdentity[7] = 0x09;
	TempIdentity.PortNumber = Xil_Htons (0x0001);
#endif
	memcpy(&(TemplateFrm[XEMACPS_PORTIDENTITY_OFFSET]),
			(u8 *)&TempIdentity,10);

	/*
	 * Clone all Tx buffers with template frame
	 */
	memcpy (InstancePtr->SyncFrmToTx,TemplateFrm,50);
	memcpy (InstancePtr->FollowUpFrmToTx,TemplateFrm,50);
	memcpy (InstancePtr->PDelayReqFrmToTx,TemplateFrm,50);
	memcpy (InstancePtr->PDelayRespFrmToTx,TemplateFrm,50);
	memcpy (InstancePtr->PDelayRespFollowUpFrmToTx,TemplateFrm,50);
	memcpy (InstancePtr->SignallingFrmToTx,TemplateFrm,50);
	memcpy (InstancePtr->AnnounceFrmToTx,TemplateFrm,50);

	/*
	 * Now initialize the individual frames
	 */
	/*
	 * Announce Frame
	 */
	InstancePtr->AnnounceFrmToTx[XEMACPS_MSGTYP_OFFSET] =
						XEMACPS_ANNOUNCEFRM_MSG_TYPE;

	TempPntr = (u16 *)&(InstancePtr->
				AnnounceFrmToTx[XEMACPS_MSGLENGTH_OFFSET]);
	*TempPntr = Xil_Htons (XEMACPS_ANNOUNCEFRM_LENGTH);

	TempPntr = (u16 *)&(InstancePtr->
				AnnounceFrmToTx[XEMACPS_FLAGS_OFFSET]);
	*TempPntr = Xil_Htons (XEMACPS_ANNOUNCEFRM_FLAGS_VAL);

	InstancePtr->AnnounceFrmToTx[XEMACPS_CONTROL_OFFSET] = 0x05;
	InstancePtr->AnnounceFrmToTx[XEMACPS_LOGMSG_INTERVAL_OFFSET] = 10;
	InstancePtr->AnnounceFrmToTx[XEMACPS_CURRUTCOFFSET_OFFSET] = 0;
	InstancePtr->AnnounceFrmToTx[XEMACPS_CURRUTCOFFSET_OFFSET + 1] = 0;

#ifdef IEEE1588_MASTER
	InstancePtr->AnnounceFrmToTx[XEMACPS_GMPRI_ONE_OFFSET] = 0x01;
#else
	InstancePtr->AnnounceFrmToTx[XEMACPS_GMPRI_ONE_OFFSET] = 0xFE;
#endif
	TempLongPntr = (u32 *)&(InstancePtr->
			AnnounceFrmToTx[XEMACPS_GM_CLK_QUALITY_OFFSET]);
	*TempLongPntr = Xil_Htonl (GmClkQuality);
	InstancePtr->AnnounceFrmToTx[XEMACPS_GMPRI_TWO_OFFSET] = 0xF8;
	memcpy((u8 *)(&InstancePtr->
				AnnounceFrmToTx[XEMACPS_GM_IDENTITY_OFFSET]),
					(u8 *)TempIdentity.ClockIdentity, 8);
	InstancePtr->AnnounceFrmToTx[XEMACPS_STEPS_REMOVED_OFFSET] = 0;
	InstancePtr->AnnounceFrmToTx[XEMACPS_STEPS_REMOVED_OFFSET + 1] = 0;

	InstancePtr->AnnounceFrmToTx[XEMACPS_TIMESOURCE_OFFSET] = 0x90;

	TempPntr = (u16 *)&(InstancePtr->
				AnnounceFrmToTx[XEMACPS_TLVTYPE_OFFSET]);
	*TempPntr = Xil_Htons (0x0008);

	TempPntr = (u16 *)&(InstancePtr->
				AnnounceFrmToTx[XEMACPS_LENGTHFIELD_OFFSET]);
	*TempPntr = Xil_Htons (0x0008);

	memcpy((u8 *)(&InstancePtr->AnnounceFrmToTx[XEMACPS_PATHSEQ_OFFSET]),
				(u8 *)TempIdentity.ClockIdentity, 8);

	/*
	 * Sync Frame
	 */
	InstancePtr->SyncFrmToTx[XEMACPS_MSGTYP_OFFSET] =
						XEMACPS_SYNCFRM_MSG_TYPE;
	TempPntr = (u16 *)&(InstancePtr->
					SyncFrmToTx[XEMACPS_MSGLENGTH_OFFSET]);
	*TempPntr = Xil_Htons (XEMACPS_SYNCFRM_LENGTH);

	TempPntr = (u16 *)&(InstancePtr->SyncFrmToTx[XEMACPS_FLAGS_OFFSET]);
	*TempPntr = Xil_Htons (XEMACPS_SYNCFRM_FLAGS_VAL);

	/*
	 * Follow-up Frame
	 */
	InstancePtr->FollowUpFrmToTx[XEMACPS_MSGTYP_OFFSET] =
						XEMACPS_FOLLOWUPFRM_MSG_TYPE;
	TempPntr = (u16 *)&(InstancePtr->
				FollowUpFrmToTx[XEMACPS_MSGLENGTH_OFFSET]);
	*TempPntr = Xil_Htons (XEMACPS_FOLLOWUPFRM_LENGTH);
	InstancePtr->FollowUpFrmToTx[XEMACPS_CONTROL_OFFSET] = 0x05;

	TempPntr = (u16 *)&(InstancePtr->FollowUpFrmToTx[58]);
	*TempPntr = Xil_Htons (0x0003);

	TempPntr = (u16 *)&(InstancePtr->FollowUpFrmToTx[60]);
	*TempPntr = Xil_Htons (28);

	memcpy ((u8 *)&(InstancePtr->FollowUpFrmToTx[62]), OrgId, 3);
	memcpy ((u8 *)&(InstancePtr->FollowUpFrmToTx[65]), OrgSubType, 3);

	/*
	 * PDelay Req Frame
	 */
	InstancePtr->PDelayReqFrmToTx[XEMACPS_MSGTYP_OFFSET] =
					XEMACPS_PDELAYREQFRM_MSG_TYPE;

	TempPntr = (u16 *)&(InstancePtr->
				PDelayReqFrmToTx[XEMACPS_MSGLENGTH_OFFSET]);
	*TempPntr = Xil_Htons (XEMACPS_PDELAYREQFRM_LENGTH);
	InstancePtr->PDelayReqFrmToTx[XEMACPS_CONTROL_OFFSET] = 0x05;

	TempPntr = (u16 *)&(InstancePtr->
				PDelayReqFrmToTx[XEMACPS_FLAGS_OFFSET]);
	*TempPntr = Xil_Htons (XEMACPS_PDELAYREQFRM_FLAGS_VAL);

	/*
	 * PDelay Resp Frame
	 */
	InstancePtr->PDelayRespFrmToTx[XEMACPS_MSGTYP_OFFSET] =
					XEMACPS_PDELAYRESPFRM_MSG_TYPE;
	TempPntr = (u16 *)&(InstancePtr->
				PDelayRespFrmToTx[XEMACPS_MSGLENGTH_OFFSET]);
	*TempPntr = Xil_Htons (XEMACPS_PDELAYRESPFRM_LENGTH);
	InstancePtr->PDelayRespFrmToTx[XEMACPS_CONTROL_OFFSET] = 0x05;
	InstancePtr->PDelayRespFrmToTx[XEMACPS_LOGMSG_INTERVAL_OFFSET] = 0x7F;

	/*
	 * PDelay Response Follow Up Frame
	 */
	InstancePtr->PDelayRespFollowUpFrmToTx[XEMACPS_MSGTYP_OFFSET] =
				XEMACPS_PDELAYRESPFOLLOWUPFRM_MSG_TYPE;
	TempPntr = (u16 *)&(InstancePtr->
		PDelayRespFollowUpFrmToTx[XEMACPS_MSGLENGTH_OFFSET]);
	*TempPntr = Xil_Htons (XEMACPS_PDELAYRESPFOLLOWUP_LENGTH);
	InstancePtr->PDelayRespFollowUpFrmToTx[XEMACPS_CONTROL_OFFSET] = 0x05;
	InstancePtr->PDelayRespFollowUpFrmToTx[XEMACPS_LOGMSG_INTERVAL_OFFSET]
									= 0x7F;
}


/*****************************************************************************/
/**
*
* This function initializes the EmacPs DMA buffer descriptors. 16 BDs are used
* on the Tx path and 16 on the Rx path. On the Rx path a 2-dimensional array
* RxBuf[16][1540] is used. The last byte in each of the buffers is used to mark
* whether the RxBuf is already submitted or not. For example, if the location
* RxBuf[1][1539] is 1, then it means the RxBuf[1] is already submitted. During
* initialization, for 16 BDs, 16 RxBufs are submitted (RxBuf[0, RxBuf[1], ...
* RxBuf[15]]) and the corresponding entries RxBuf[0][1539], RxBuf[1][1539], ...
* RxBuf[15][1539] are marked as 1.
* On the Rx path, all 16 BDs are submitted to the hardware.
* Once that is done, the timer is started and so is the EmacPs.
*
* @param	EmacPsInstancePtr is a pointer to the instance of the EmacPs.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XEmacPs_InitializeEmacPsDma (XEmacPs_Ieee1588 *InstancePntr)
{
	XEmacPs_BdRing *TxRingPtr;
	XEmacPs_BdRing *RxRingPtr;
	XEmacPs_Bd *RxBdPtr;
	XEmacPs_Bd *CurrBdPtr;
	int Status;
	int Index;
	XEmacPs_Bd BdTemplate;

	Status = XST_SUCCESS;

	TxRingPtr = &XEmacPs_GetTxRing(InstancePntr->EmacPsInstance);
	RxRingPtr = &XEmacPs_GetRxRing(InstancePntr->EmacPsInstance);

	/* Allocate Rx and Tx BD space each */
	RxBdSpacePtr = &(bd_space[0]);
	TxBdSpacePtr = &(bd_space[0x10000]);

	/*
	 * BdTemplate is used for cloning. Hence it is cleared so that
	 * all 16 BDs can be cleared.
	 */
	XEmacPs_BdClear(&BdTemplate);
	Status = XEmacPs_BdRingCreate (RxRingPtr,
					(UINTPTR)RxBdSpacePtr,
					(UINTPTR)RxBdSpacePtr,
					XEMACPS_IEEE1588_BD_ALIGNMENT,
					XEMACPS_IEEE1588_NO_OF_RX_DESCS);

	if (Status != XST_SUCCESS) {
#ifdef DEBUG_XEMACPS_LEVEL1
		xil_printf("Status :: %d\r\n", Status);
		xil_printf("In %s: BD Ring Creation failed for Rx path \r\n",
								__func__);
#endif
		return;
	}

	/*
	 * Clone the 16 BDs with BdTemplate. This will clear all the 16 BDs.
	 */
	Status = XEmacPs_BdRingClone(RxRingPtr, &BdTemplate, XEMACPS_RECV);
	if (Status != XST_SUCCESS) {
		return;
	}

	/*
	 * BdTemplate is used for cloning on Tx path. Hence it is cleared so
	 * that all 16 BDs can be cleared.
	 */
	XEmacPs_BdClear(&BdTemplate);
	/*
	 * Set the Used Bit.
	 */
	XEmacPs_BdSetStatus(&BdTemplate, XEMACPS_TXBUF_USED_MASK);

	/*
	 * Create 16 BDs for Tx path.
	 */
	Status = XEmacPs_BdRingCreate (TxRingPtr,
					(UINTPTR) TxBdSpacePtr,
					(UINTPTR) TxBdSpacePtr,
					XEMACPS_IEEE1588_BD_ALIGNMENT,
					XEMACPS_IEEE1588_NO_OF_TX_DESCS);

	if (Status != XST_SUCCESS) {
#ifdef DEBUG_XEMACPS_LEVEL1
		xil_printf("In %s: BD Ring Creation failed for Tx path \r\n",
								__func__);
#endif
		return;
	}

	/*
	 * Clone the 16 BDs with BdTemplate. This will clear all the 16 BDs
	 * and set the Used bit in all of them.
	 */
	Status = XEmacPs_BdRingClone (TxRingPtr, &BdTemplate, XEMACPS_SEND);
	if (Status != XST_SUCCESS) {
		return;
	}
	/* @test */
	/*
	* This version of GEM supports priority queuing and the current
	* driver is using tx priority queue 1 and normal rx queue for
	* packet transmit and receive. The below code ensure that the
	* other queue pointers are parked to known state for avoiding
	* the controller to malfunction by fetching the descriptors
	* from these queues.
	*/
	XEmacPs_BdClear(&BdRxTerminate);
	XEmacPs_BdSetAddressRx(&BdRxTerminate, (XEMACPS_RXBUF_NEW_MASK |
					XEMACPS_RXBUF_WRAP_MASK));
	XEmacPs_Out32((EmacCfgPtr->BaseAddress + XEMACPS_RXQ1BASE_OFFSET),
						(UINTPTR)&BdRxTerminate);
	XEmacPs_BdClear(&BdTxTerminate);
	XEmacPs_BdSetStatus(&BdTxTerminate, (XEMACPS_TXBUF_USED_MASK |
					XEMACPS_TXBUF_WRAP_MASK));
	XEmacPs_Out32((EmacCfgPtr->BaseAddress + XEMACPS_TXQBASE_OFFSET),
						(UINTPTR)&BdTxTerminate);
	if (EmacCfgPtr->IsCacheCoherent == 0) {
		Xil_DCacheFlushRange((UINTPTR)(&BdTxTerminate), 64);
	}

	/*
	 * Allocate the 16 BDs on Rx  path.
	 */
	Status = XEmacPs_BdRingAlloc (RxRingPtr,
					XEMACPS_IEEE1588_NO_OF_RX_DESCS,
					&RxBdPtr);
	if (Status != XST_SUCCESS) {
#ifdef DEBUG_XEMACPS_LEVEL1
		xil_printf("In %s: BD Ring allocation failed for Rx path \r\n",
								__func__);
#endif
		return;
	}
	/*
	 * Mark the RxBufs as used.
	 */
	CurrBdPtr = RxBdPtr;
	XEmacPs_BdSetAddressRx(RxBdPtr, (UINTPTR)&RxBuf);
	if (Status != XST_SUCCESS) {
		xil_printf("Error allocating RxBD");
		return XST_FAILURE;
	}

	/*
	 * Submit the BDs on the Rx path.
	 */
	Status = XEmacPs_BdRingToHw (RxRingPtr,
					XEMACPS_IEEE1588_NO_OF_RX_DESCS,
					RxBdPtr);
	if (Status != XST_SUCCESS) {
#ifdef DEBUG_XEMACPS_LEVEL1
		xil_printf("In %s: BD Ring submission failed for Rx path \r\n",
								__func__);
#endif
		return;
	}

	XEmacPs_SetQueuePtr(EmacPsInstancePtr, EmacPsInstancePtr->RxBdRing.BaseBdAddr, 0, XEMACPS_RECV);
	XEmacPs_SetQueuePtr(EmacPsInstancePtr, EmacPsInstancePtr->TxBdRing.BaseBdAddr, 1, XEMACPS_SEND);

	/*
	 * Start the timer and EmacPs.
	 */
	XEmacPs_Start(InstancePntr->EmacPsInstance);
	// #if 0  // ttc 
	TtcPs_Start();
	// #endif
}


/*****************************************************************************/
/**
*
* This function does various initializations.
* - It initializes the Local Port Identity. Clock Identity is
*   initialized with 000A35FFFE010203 and port number as 1.
* - It initializes some of the fields of Tx PTP buffers. These fields remain
*   constant over time and hence are initialized at one place.
* - It initializes signalling frame data. Since signalling frames are
*   not implemented, the entries in the structure SignallingFrameData are
*   hard coded.
* - The entries in the structure instance PtpCounters are initialized to 0.
* - The variable PtpIsRunning is made as 1 so that PTP packets Tx can be
*   initiated in the Timer ISR.
* - The PTP node is made as Master to start with.
* - The Peer Ieee1588v2 capability is made as 0 which means the peer is not
*   Ieee1588v2 capable.
*
* @param	InstancePntr is a pointer to the instance of the
*		XEmacPs_Ieee1588.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XEmacPs_InitializeProtocolData(XEmacPs_Ieee1588 *InstancePntr)
{
	/*
	 * Populate the local port identity.
	 */

	InstancePntr->PortIdLocal.ClockIdentity[0] = 0x00;
	InstancePntr->PortIdLocal.ClockIdentity[1] = 0x0A;
	InstancePntr->PortIdLocal.ClockIdentity[2] = 0x35;
	InstancePntr->PortIdLocal.ClockIdentity[3] = 0xFF;
	InstancePntr->PortIdLocal.ClockIdentity[4] = 0xFE;
	InstancePntr->PortIdLocal.ClockIdentity[5] = 0x01;
	InstancePntr->PortIdLocal.ClockIdentity[6] = 0x02;
	InstancePntr->PortIdLocal.ClockIdentity[7] = 0x09;
	InstancePntr->PortIdLocal.PortNumber = 0x0001;

	/*
	 * Initialize some fields in the Tx Ptp buffers.
	 */
	XEmacPs_SetDfltTxFrms(InstancePntr);

	/*
	 * The PTP node is made Master..
	 */
	XEmacPs_BecomeRtcSlave(InstancePntr);

	// XEmacPs_BecomeRtcMaster(InstancePntr,0);

	/*
	 * The Peer node not Ieee1588v2 capable to start with...
	 */
	XEmacPs_ChangePeerIeee1588v2Capability(InstancePntr, 0);

	InstancePntr->PtpIsRunning = 0;
	InstancePntr->PtpRecords.LinkDelay = 0;

	InstancePntr->SignallingFrameData.SyncIntervalDuration = 2;
	InstancePntr->SignallingFrameData.LinkDelayIntervalDuration = 8;
	InstancePntr->SignallingFrameData.AnnounceIntervalDuration  = 10;

	InstancePntr->LatestMDSyncReceive.SyncIntervalDuration = 2;

	/*
	 * Initialize other driver variables in the device's data structure
	 */
	InstancePntr->PtpCounters.CounterSyncInterval = 0;
	InstancePntr->PtpCounters.CounterLinkDelayInterval = 0;
	InstancePntr->PtpCounters.CounterAnnounceInterval = 0;
	InstancePntr->PtpCounters.CounterSyncEvents = 0;
	InstancePntr->StateMachineData.LostResponses = 0;
	InstancePntr->StateMachineData.RcvdPDelayResp = 0;
	InstancePntr->StateMachineData.RcvdPDelayRespFollowUp = 0;

	XEmacPs_DecodeTxAnnounceFrame (InstancePntr,
					InstancePntr->AnnounceFrmToTx);
	/*
	 * The PTP packets can now be transmitted out.
	 */
	InstancePntr->PtpIsRunning = 1;
	XEmacPs_ChangePeerIeee1588v2Capability (InstancePntr, 0);
}


/*****************************************************************************/
/**
*
* This function extracts length/type information from the Ethernet packet.
*
* @param	PacketBuf contains the Ethernet packet.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
u32 XEmacPs_IsRxFramePTP (u8 *PacketBuf)
{
	if ((Xil_Ntohs (*(u16 *) (PacketBuf + 12))) ==
						XEMACPS_PTP_ETHERTYPE)
		return TRUE;
	else
		return FALSE;
}


/*****************************************************************************/
/**
*
* This function is processes the received packets. This is invoked from
* XEmacPs_RunIEEE1588Protocol. It copies the received packet from the RxBuf
* to the corresponding buffer in XEmacPs_Ieee1588 structure instance.It then
* identifies the type of PTP packet received and calls the corresponding
* handlers to do further processing.
* It then frees the corresponding BD, does a allocation of the freed BD and
* populates the BD with appropriate buffer address (RxBuf).
*
* @param	InstancePntr is a pointer to the instance of the
*		XEmacPs_Ieee1588.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XEmacPs_HandleRecdPTPPacket(XEmacPs_Ieee1588 *InstancePtr)
{
	u8 MessageType      	= 0;
	XEmacPs_Bd *BdPtr;
	XEmacPs_Bd *CurBdPtr;
	unsigned int NumBds;
	unsigned int FreeBds;
	int i;
	int j;
	int k;
	u8 *BufAddr;
	int BufLen;
	int Status;
	XEmacPs_BdRing *TxRingPtr;
	XEmacPs_BdRing *RxRingPtr;

	/*
	 * Get the ring pointers from EmacPs instance
	 */
	TxRingPtr = &XEmacPs_GetTxRing(InstancePtr->EmacPsInstance);
	RxRingPtr = &XEmacPs_GetRxRing(InstancePtr->EmacPsInstance);

	/*
	 * If PTP functions are marked as not running, then take no further
	 * action.
	 */
	if (InstancePtr->PtpIsRunning == 1) {

		/*
		 * Extract all available BDs from EmacPs.
		 */
		NumBds = XEmacPs_BdRingFromHwRx( RxRingPtr,
					XEMACPS_IEEE1588_NO_OF_RX_DESCS, &BdPtr);
		if (NumBds == 0)
			return;
		for (i = 0, CurBdPtr=BdPtr; i < NumBds; i++) {
			/*
			 * Get the buffer address in which the PTP packet is
			 * stored from the BD.
			 */
			BufAddr = (void*)(INTPTR)(XEmacPs_BdGetBufAddr(CurBdPtr) &
			~(XEMACPS_RXBUF_WRAP_MASK | XEMACPS_RXBUF_NEW_MASK));
			BufLen = XEmacPs_BdGetLength(CurBdPtr);

			Xil_DCacheInvalidateRange((u32)BufAddr, 132);
			/*
			 * If the received packet is not PTP, then there is
			 * some error. The example is used to demonstrate the
			 * PTP protocol and hence any other packet is not
			 * acceptable. Print the wrond packet to get some
			 * idea regarding what it contains.
			 */
			if (XEmacPs_IsRxFramePTP((u8*)BufAddr) == FALSE) {
#ifdef DEBUG_XEMACPS_LEVEL1
				xil_printf("A WRONG Packet Received \r\n");
				for (k = 0; k <= 100; k=k+10) {
					xil_printf("%x %x %x %x %x %x %x %x %x %x\r\n",
							BufAddr[k],
							BufAddr[k+1],
							BufAddr[k+2],
							BufAddr[k+3],
							BufAddr[k+4],
							BufAddr[k+5],
							BufAddr[k+6],
							BufAddr[k+7],
							BufAddr[k+8],
							BufAddr[k+9]);
				}
#endif

			} else {
				/*
				 * Extract the PTP message type
				 */
				MessageType = XEmacPs_GetMsgType (BufAddr);
				switch (MessageType) {
				case XEMACPS_PTP_TYPE_SYNC:
					/*
					 * Copy to relevant buffer and call the
					 * corresponding handler.
					 */
					memcpy(InstancePtr->LastRecdSyncFrm,
						BufAddr, BufLen);
					XEmacPs_DecodeRxSync(InstancePtr,
						InstancePtr->LastRecdSyncFrm);
				break;

				case XEMACPS_PTP_TYPE_FOLLOW_UP:
					/*
					 * Copy to relevant buffer and call the
					 * corresponding handler.
					 */
					memcpy(
					InstancePtr->LastRecdFollowUpFrm,
						BufAddr, BufLen);
					XEmacPs_DecodeRxFollowUp(InstancePtr,
					InstancePtr->LastRecdFollowUpFrm); // 本地时钟调整
				break;

				case XEMACPS_PTP_TYPE_PDELAYREQ:
					/*
					 * Copy to relevant buffer and call the
					 * corresponding handler.
					 */
					memcpy(
					InstancePtr->LastRecdPDelayReqFrm,
						BufAddr, BufLen);
					XEmacPs_SendPDelayResp(InstancePtr);
				break;

				case XEMACPS_PTP_TYPE_PDELAYRESP:
					/*
					 * Copy to relevant buffer and call the
					 * corresponding handler.
					 */
					memcpy(
					InstancePtr->LastRecdPDelayRespFrm,
						BufAddr, BufLen);
					XEmacPs_DecodeRxPDelayResp(InstancePtr,
					InstancePtr->LastRecdPDelayRespFrm);

				break;

				case XEMACPS_PTP_TYPE_PDELAYRESP_FOLLOW_UP:
					/*
					 * Copy to relevant buffer and call the
					 * corresponding handler.
					 */
					memcpy(
					InstancePtr->
					LastRecdPDelayRespFollowUpFrm, BufAddr,
								BufLen);
					XEmacPs_DecodeRxPDelayRespFollowUp
					(InstancePtr,
					InstancePtr->
					LastRecdPDelayRespFollowUpFrm);
				break;

				case XEMACPS_PTP_TYPE_ANNOUNCE:
					/*
					 * Copy to relevant buffer and call the
					 * corresponding handler.
					 */
					memcpy(
					InstancePtr->LastRecdAnnounceFrm,
					BufAddr, BufLen);
					XEmacPs_DecodeRxAnnounceFrame
					(InstancePtr,
					InstancePtr->LastRecdAnnounceFrm);
				break;

				case XEMACPS_PTP_TYPE_SIGNALING:
					/*
					 * Copy to relevant buffer and call the
					 * corresponding handler.
					 */
					memcpy(
					InstancePtr->LastRecdSignallingFrm,
					BufAddr, BufLen);
					XEmacPs_DecodeRxSignaling
					(InstancePtr,
					InstancePtr->LastRecdSignallingFrm);

				break;

				default:

				break;
				}
			}
			/*
			 * Clear the used bit in the buffer so that it can
			 * be reused.
			 */
			BufAddr[XEMACPS_PACKET_LEN - 2] = 0;
			CurBdPtr = XEmacPs_BdRingNext( RxRingPtr, CurBdPtr);
		}

		/*
		 * Time to free the BDs
		 */
		XEmacPs_BdRingFree(RxRingPtr, NumBds, BdPtr);
		/*
		 * Time to reallocate the BDs
		 */
		FreeBds = XEmacPs_BdRingGetFreeCnt (RxRingPtr);
		Status = XEmacPs_BdRingAlloc (RxRingPtr, FreeBds,
								&BdPtr);
		if (Status != XST_SUCCESS) {
			return;
		}
		CurBdPtr = BdPtr;
		#if 0
		for (i = 0; i < FreeBds; i++) {
			for (j = 0; j < XEMACPS_IEEE1588_NO_OF_RX_DESCS; j++) {
				if ((RxBuf[j][XEMACPS_PACKET_LEN - 2]) == 0) {
					XEmacPs_BdSetAddressRx
						(CurBdPtr, (UINTPTR)(&(RxBuf[j][0])));
					/*
					 * Set the used bit in the Buffer
					 */
					RxBuf[j][XEMACPS_PACKET_LEN - 2] = 1;
					/*
					 * Clear the used bit so that it
					 * can be reused.
					 */
					XEmacPs_BdClearRxNew (CurBdPtr);
					break;
				}
			}
			CurBdPtr = XEmacPs_BdRingNext (RxRingPtr, CurBdPtr);
		}
		#endif
		/*
		 * Submit the BDs to the hardware
		 */
		Status = XEmacPs_BdRingToHw (RxRingPtr, FreeBds, BdPtr);
	}
}








void XEmacPs_RunIEEE1588Protocol(XEmacPs *EmacInstance)
{
	XEmacPs_Stop(EmacInstance);
	GlobalInstancePntr = &IEEE1588ProtoHandler;
	GlobalInstancePntr->EmacPsInstance = EmacInstance;

	/*
	 * Initialize the DMA and buffer descriptors
	 */
	XEmacPs_InitializeEmacPsDma (GlobalInstancePntr);
	/*
	 * Initialize some of the protocol structure instances and Tx frames
	 * with default data.
	 */
	XEmacPs_InitializeProtocolData(GlobalInstancePntr);

}


// #define TTC_TEST
int InitPtp()
{
	
	int Status = XST_SUCCESS;

	tickHandlerArgs.PtpHandler = &IEEE1588ProtoHandler;
	tickHandlerArgs.TtcPsTick = &TtcPsInst;
	/* Initialize TTCIMER */
	if (InitTtcTimer()  != XST_SUCCESS) while(1);
	
	Init_TSU();
	// Init_XEmac();

	TtcPs_Start();
	/*
	 * Start the PTP standalone state machine.
	 */
	XEmacPs_RunIEEE1588Protocol(EmacPsInstancePtr);

#ifdef TTC_TEST
	Xil_Out32(TSU_TIMER_SEC_GEM0, 0);
	Xil_Out32(TSU_TIMER_NS_GEM0,  0);
	TtcPs_Start();
	while (1)
	{
		sleep(1);
		xil_printf("TSU_TIMER_SEC_GEM0 :: %8.8x\r\n", Xil_In32(TSU_TIMER_SEC_GEM0));
		xil_printf("TSU_TIMER_NS_GEM0  :: %8.8x\r\n", Xil_In32(TSU_TIMER_NS_GEM0));

		break;
	}
#endif
	// Xil_AssertNonvoid((RX_BD_START_ADDRESS & 0x1F) == 0);
	// Xil_AssertNonvoid((TX_BD_START_ADDRESS & 0x1F) == 0);

	// TtcPs_Stop();
	// xil_printf("ptp system exit...\r\n");
	return XST_SUCCESS;
}

void PtpPacketHandler(struct pbuf *p)
{
	u8 MessageType      	= 0;
	// struct eth_hdr *ethhdr = (struct eth_hdr *)p->payload;
	struct ip_hdr *iphdr = (struct ip_hdr *)((u8_t*)p->payload + sizeof(struct eth_hdr));
	struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t*)iphdr + IPH_HL(iphdr) * 4);

	u8_t *ptp_payload = (u8_t*)udphdr + sizeof(struct udp_hdr);
	u16_t ptp_length = p->tot_len - (sizeof(struct eth_hdr) + IPH_HL(iphdr)*4 + sizeof(struct udp_hdr));
	// xil_printf("recv ptp ...ptp_length :: %d\r\n", ptp_length);
	MessageType = XEmacPs_GetMsgType (ptp_payload);
#ifdef PTP_SALVE_TEST
		xil_printf("recv type :: %d\r\n", MessageType);
#endif 
	switch (MessageType) {
	case XEMACPS_PTP_TYPE_SYNC:
	break;
		// XEmacPs_DecodeRxSync_UDP(ptp_payload);
	case XEMACPS_PTP_TYPE_FOLLOW_UP:

	break;

	case XEMACPS_PTP_TYPE_PDELAYREQ:

	break;

	case XEMACPS_PTP_TYPE_PDELAYRESP:

	break;

	case XEMACPS_PTP_TYPE_PDELAYRESP_FOLLOW_UP:

	break;

	case XEMACPS_PTP_TYPE_ANNOUNCE:

	break;

	case XEMACPS_PTP_TYPE_SIGNALING:

	break;

	default:

	break;
	}
}

void IEEE1588Ptpd()
{
  /*
    * If a new packet has been received, copy the packet into
    * corresponding buffer and call XEmacPs_HandleRecdPTPPacket to
    * do further processing.
    */
  if (GlobalInstancePntr->PtpNewPktRecd == TRUE) {
    GlobalInstancePntr->PtpNewPktRecd = FALSE;
    XEmacPs_HandleRecdPTPPacket(GlobalInstancePntr);
  }
  /*
    * If a packet needs to be sent
    */
  if (PTPSendPacket != 0) {
    /*
      * If a PDelayResp needs to be sent, initiate a Tx.
      * Set the corresponding bit in the variable
      * PTPSendPacket to send a PDelayRespFollowUp packet
      * once the PDelayResp is successfully sent out
      * (Tx Done interrupt generated for PDelayResp.)
      */

    if (PTPSendPacket & SEND_PDELAY_RESP) {
        XEmacPs_PtpTxPacket(GlobalInstancePntr,
        GlobalInstancePntr->PDelayRespFrmToTx,
        XEMACPS_PDELAYRESPMSG_TOT_LEN);
      PTPSendPacket = PTPSendPacket &
            (~SEND_PDELAY_RESP);
      PTPSendPacket |= SEND_PDELAY_RESP_FOLLOWUP;
    }

    /*
      * If a PDelayRespFollowUp needs to be sent check if
      * the previous PDelayResp is successfully sent out or
      * not. If the PDelayResp is sent out successfully,
      * the flag PDelayRespSent is marked as True in the
      * Tx Done ISR.
      */
    if (PTPSendPacket & SEND_PDELAY_RESP_FOLLOWUP) {
      if (PDelayRespSent == TRUE) {
        XEmacPs_SendPDelayRespFollowUp
        (GlobalInstancePntr);
        PTPSendPacket = PTPSendPacket &
          (~SEND_PDELAY_RESP_FOLLOWUP);
      }
    }

    /*
      * If a PDelayReq needs to be sent initiate a Tx here.
      */
    if (PTPSendPacket & SEND_PDELAY_REQ) {
      XEmacPs_PtpTxPacket(GlobalInstancePntr,
        GlobalInstancePntr->PDelayReqFrmToTx,
        XEMACPS_PDELAYREQMSG_TOT_LEN);
      PTPSendPacket = PTPSendPacket &
            (~SEND_PDELAY_REQ);
    }

    /*
      * If a SYNC packet needs to be sent initiate a Tx
      * here. Set the bit corresponding to FOLLOWUP packet
      * in the variable PTPSendPacket. This will initiate a
      * Tx  for FollowUp frame once the SYNC frame is
      * successfully sent out and a Tx done interrupt is
      * received.
      */
    if (PTPSendPacket & SEND_SYNC) {
      XEmacPs_PtpTxPacket (GlobalInstancePntr,
        GlobalInstancePntr->SyncFrmToTx,
        XEMACPS_SYNCMSG_TOT_LEN);
      PTPSendPacket = PTPSendPacket & (~SEND_SYNC);
      PTPSendPacket |= SEND_FOLLOW_UP;
    }

    /*
      * If a FollowUp needs to be sent check if
      * the previous SYNC is successfully sent out or
      * not. If the SYNC is sent out successfully,
      * the flag SyncSent is marked as True in the
      * Tx Done ISR.
      */
    if (PTPSendPacket & SEND_FOLLOW_UP) {
      if (SyncSent == TRUE) {
        XEmacPs_MasterSendFollowUp
            (GlobalInstancePntr);
        PTPSendPacket = PTPSendPacket &
            (~SEND_FOLLOW_UP);
      }
    }
  }

}
