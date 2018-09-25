/******************************************************************************
*
* (c) Copyright 2010-2013 Xilinx, Inc. All rights reserved.
*
* This file contains confidential and proprietary information of Xilinx, Inc.
* and is protected under U.S. and international copyright and other
* intellectual property laws.
*
* DISCLAIMER
* This disclaimer is not a license and does not grant any rights to the
* materials distributed herewith. Except as otherwise provided in a valid
* license issued to you by Xilinx, and to the maximum extent permitted by
* applicable law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND WITH ALL
* FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES AND CONDITIONS, EXPRESS,
* IMPLIED, OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF
* MERCHANTABILITY, NON-INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE;
* and (2) Xilinx shall not be liable (whether in contract or tort, including
* negligence, or under any other theory of liability) for any loss or damage
* of any kind or nature related to, arising under or in connection with these
* materials, including for any direct, or any indirect, special, incidental,
* or consequential loss or damage (including loss of data, profits, goodwill,
* or any type of loss or damage suffered as a result of any action brought by
* a third party) even if such damage or loss was reasonably foreseeable or
* Xilinx had been advised of the possibility of the same.
*
* CRITICAL APPLICATIONS
* Xilinx products are not designed or intended to be fail-safe, or for use in
* any application requiring fail-safe performance, such as life-support or
* safety devices or systems, Class III medical devices, nuclear facilities,
* applications related to the deployment of airbags, or any other applications
* that could lead to death, personal injury, or severe property or
* environmental damage (individually and collectively, "Critical
* Applications"). Customer assumes the sole risk and liability of any use of
* Xilinx products in Critical Applications, subject only to applicable laws
* and regulations governing limitations on product liability.
*
* THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS PART OF THIS FILE
* AT ALL TIMES.
*
******************************************************************************/
/*****************************************************************************/
/**
* @file xusbps_intr_example.c
*
* This file contains an example of how to use the USB driver with the USB
* controller in DEVICE mode.
*
*
*<pre>
* MODIFICATION HISTORY:
*
* Ver   Who     Date     Changes
* ----- ------  -------- ----------------------------------------------------
* 1.00a wgr/nm  10/09/10 First release
* 1.01a nm      03/05/10 Included xpseudo_asm.h instead of xpseudo_asm_gcc.h
* 1.04a nm      02/05/13 Fixed CR# 696550.
*		         Added template code for Vendor request.
* 1.06a kpc		11/11/13 Fixed CR#759458, cacheInvalidate size should be 
*				 ailgned to ccahe line size.
*</pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include "xparameters.h"		/* XPAR parameters */
#include "xusbps.h"			/* USB controller driver */
#include "xscugic.h"
#include "xusbps_ch9.h"		/* Generic Chapter 9 handling code */
#include "xusbps_class_storage.h"	/* Storage class handling code */
#include "xil_exception.h"
#include "xpseudo_asm.h"
#include "xreg_cortexa9.h"
#include "xil_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xscutimer.h"
#include "sleep.h"
/************************** Constant Definitions *****************************/


XScuTimer TimerInstance;	/* Cortex A9 Scu Private Timer Instance */
volatile int TimerExpired;
volatile u8 PhyAccessDone = 0;

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

static int UsbIntrExample(XScuGic *IntcInstancePtr, XUsbPs *UsbInstancePtr,
			  u16 UsbDeviceId, u16 UsbIntrId);

static void UsbIntrHandler(void *CallBackRef, u32 Mask);
static void UsbIfPhyIntrHandler(void *CallBackRef, u32 IntrStatus);

static void XUsbPs_Ep0EventHandler(void *CallBackRef, u8 EpNum,
					u8 EventType, void *Data);
static void XUsbPs_Ep1EventHandler(void *CallBackRef, u8 EpNum,
					u8 EventType, void *Data);
static int UsbSetupIntrSystem(XScuGic *IntcInstancePtr,
			      XUsbPs *UsbInstancePtr, u16 UsbIntrId,
				  XScuTimer *TimerInstancePtr, u16 TimerIntrId);
static void UsbDisableIntrSystem(XScuGic *IntcInstancePtr, u16 UsbIntrId);

static int My_UlpiPhyWriteRegister(XUsbPs *InstancePtr, u8 RegAddr, u8 UlpiPhyRegData);
static u8 My_UlpiPhyReadRegister(XUsbPs *InstancePtr, u8 RegAddr);
/************************** Variable Definitions *****************************/

/* The instances to support the device drivers are global such that the
 * are initialized to zero each time the program runs.
 */
static XScuGic IntcInstance;	/* The instance of the IRQ Controller */
static XUsbPs UsbInstance;	/* The instance of the USB Controller */

#define PACKET_SIZE         512 //in Bytes
#define NUM_PACKETS_PER_KB  2
#define KB_TO_MB            1024
#define NUM_MB				1
#define BUFFER_SIZE         PACKET_SIZE*NUM_PACKETS_PER_KB*KB_TO_MB*NUM_MB

#define PACKETS_TO_TRANSFER	BUFFER_SIZE/PACKET_SIZE

static u8 in_buffer[BUFFER_SIZE];
static u8 out_buffer[BUFFER_SIZE];

static volatile int NumIrqsRX = 0;
static volatile int NumIrqsTX = 0;


int My_UlpiPhyWriteRegister(XUsbPs *InstancePtr, u8 RegAddr, u8 UlpiPhyRegData)
{

	if(XUsbPs_ReadReg(InstancePtr->Config.BaseAddress, XUSBPS_ULPIVIEW_OFFSET) & XUSBPS_ULPIVIEW_RUN_MASK)
	{
		xil_printf("XST_DEVICE_BUSY\r\n");
		return XST_DEVICE_BUSY;
	}

	XUsbPs_WriteReg(InstancePtr->Config.BaseAddress,
					XUSBPS_ULPIVIEW_OFFSET,
					XUSBPS_ULPIVIEW_RW_MASK	| ((RegAddr << 16) & XUSBPS_ULPIVIEW_ADDR_MASK) | UlpiPhyRegData);

	return XST_SUCCESS;
}
u8 My_UlpiPhyReadRegister(XUsbPs *InstancePtr, u8 RegAddr)
{

	if(XUsbPs_ReadReg(InstancePtr->Config.BaseAddress, XUSBPS_ULPIVIEW_OFFSET) & XUSBPS_ULPIVIEW_RUN_MASK)
	{
		xil_printf("XST_DEVICE_BUSY\r\n");
		return XST_DEVICE_BUSY;
	}

	XUsbPs_WriteReg(InstancePtr->Config.BaseAddress,
					XUSBPS_ULPIVIEW_OFFSET,
					(RegAddr << 16) & XUSBPS_ULPIVIEW_ADDR_MASK);

	while(XUsbPs_ReadReg(InstancePtr->Config.BaseAddress, XUSBPS_ULPIVIEW_OFFSET) & XUSBPS_ULPIVIEW_RUN_MASK)
	{}

	return ((XUsbPs_ReadReg(InstancePtr->Config.BaseAddress, XUSBPS_ULPIVIEW_OFFSET) & XUSBPS_ULPIVIEW_DATRD_MASK) >> 8);
}
void UsbIfPhyIntrHandler(void *CallBackRef, u32 IntrStatus)
{
	xil_printf("Handler called.\n\r");
	XUsbPs*	InstancePtr;
	InstancePtr = (XUsbPs*) CallBackRef;
	PhyAccessDone = 1;
	if(IntrStatus & XUSBPS_IXR_ULPI_MASK)
	{
		PhyAccessDone = 1;
	}

}
static void TimerIntrHandler(void *CallBackRef)
{
	XScuTimer *TimerInstancePtr = (XScuTimer *) CallBackRef;

	if (XScuTimer_IsExpired(TimerInstancePtr)) {

		TimerExpired++;
		if (TimerExpired == 18) {
			xil_printf("%d Mb/s %d\r\n",NumIrqsRX*512*8/1000000,NumIrqsTX);
			//NumIrqsRX = 0;
			//NumIrqsTX = 0;
			TimerExpired = 0;
		}
	}
}
/*****************************************************************************/
/**
 *
 * Main function to call the USB interrupt example.
 *
 * @param	None
 *
 * @return
 * 		- XST_SUCCESS if successful
 * 		- XST_FAILURE on error
 *
 ******************************************************************************/

int main(void) {
	int Status;
	xil_printf("START\r\n");
	/* Run the USB Interrupt example.*/
	Status = UsbIntrExample(&IntcInstance, &UsbInstance,
				XPAR_XUSBPS_0_DEVICE_ID, XPAR_XUSBPS_0_INTR);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
 *
 * This function does a minimal DEVICE mode setup on the USB device and driver
 * as a design example. The purpose of this function is to illustrate how to
 * set up a USB flash disk emulation system.
 *
 *
 * @param	IntcInstancePtr is a pointer to the instance of the INTC driver.
 * @param	UsbInstancePtr is a pointer to the instance of USB driver.
 * @param	UsbDeviceId is the Device ID of the USB Controller and is the
 * 		XPAR_<USB_instance>_DEVICE_ID value from xparameters.h.
 * @param	UsbIntrId is the Interrupt Id and is typically
 * 		XPAR_<INTC_instance>_<USB_instance>_IP2INTC_IRPT_INTR value
 * 		from xparameters.h.
 *
 * @return
 * 		- XST_SUCCESS if successful
 * 		- XST_FAILURE on error
 *
 ******************************************************************************/
static int UsbIntrExample(XScuGic *IntcInstancePtr, XUsbPs *UsbInstancePtr,
					u16 UsbDeviceId, u16 UsbIntrId)
{
	int	Status;
	u32	MemSize;
	u8	*MemPtr = NULL;
	int	ReturnStatus = XST_FAILURE;
	XScuTimer_Config *ConfigPtr;
	int i;
	u32 Handle;
	//TIMER
	ConfigPtr = XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
	Status = XScuTimer_CfgInitialize(&TimerInstance, ConfigPtr,ConfigPtr->BaseAddr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	XScuTimer_EnableAutoReload(&TimerInstance);
	XScuTimer_LoadTimer(&TimerInstance, 0xFFFF);
	XScuTimer_SetPrescaler(&TimerInstance,0xFF);
	XScuTimer_Start(&TimerInstance);

	/* For this example we only configure 2 endpoints:
	 *   Endpoint 0 (default control endpoint)
	 *   Endpoint 1 (BULK data endpoint)
	 */
	const u8 NumEndpoints = 2;

	XUsbPs_Config		*UsbConfigPtr;
	XUsbPs_DeviceConfig	DeviceConfig;

	/* Initialize the USB driver so that it's ready to use,
	 * specify the controller ID that is generated in xparameters.h
	 */
	UsbConfigPtr = XUsbPs_LookupConfig(UsbDeviceId);
	if (NULL == UsbConfigPtr) {
		goto out;
	}


	/* We are passing the physical base address as the third argument
	 * because the physical and virtual base address are the same in our
	 * example.  For systems that support virtual memory, the third
	 * argument needs to be the virtual base address.
	 */
	Status = XUsbPs_CfgInitialize(UsbInstancePtr,
				       UsbConfigPtr,
				       UsbConfigPtr->BaseAddress);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	/* Set up the interrupt subsystem.
	 */
	Status = UsbSetupIntrSystem(IntcInstancePtr,
				    UsbInstancePtr,
				    UsbIntrId,
					&TimerInstance,
					XPAR_SCUTIMER_INTR);
	if (XST_SUCCESS != Status)
	{
		goto out;
	}

	/* Configuration of the DEVICE side of the controller happens in
	 * multiple stages.
	 *
	 * 1) The user configures the desired endpoint configuration using the
	 * XUsbPs_DeviceConfig data structure. This includes the number of
	 * endpoints, the number of Transfer Descriptors for each endpoint
	 * (each endpoint can have a different number of Transfer Descriptors)
	 * and the buffer size for the OUT (receive) endpoints.  Each endpoint
	 * can have different buffer sizes.
	 *
	 * 2) Request the required size of DMAable memory from the driver using
	 * the XUsbPs_DeviceMemRequired() call.
	 *
	 * 3) Allocate the DMAable memory and set up the DMAMemVirt and
	 * DMAMemPhys members in the XUsbPs_DeviceConfig data structure.
	 *
	 * 4) Configure the DEVICE side of the controller by calling the
	 * XUsbPs_ConfigureDevice() function.
	 */

	/*
	 * For this example we only configure Endpoint 0 and Endpoint 1.
	 *
	 * Bufsize = 0 indicates that there is no buffer allocated for OUT
	 * (receive) endpoint 0. Endpoint 0 is a control endpoint and we only
	 * receive control packets on that endpoint. Control packets are 8
	 * bytes in size and are received into the Queue Head's Setup Buffer.
	 * Therefore, no additional buffer space is needed.
	 */
	DeviceConfig.EpCfg[0].Out.Type		= XUSBPS_EP_TYPE_CONTROL;
	DeviceConfig.EpCfg[0].Out.NumBufs	= 2;
	DeviceConfig.EpCfg[0].Out.BufSize	= 64;
	DeviceConfig.EpCfg[0].Out.MaxPacketSize	= 64;
	DeviceConfig.EpCfg[0].In.Type		= XUSBPS_EP_TYPE_CONTROL;
	DeviceConfig.EpCfg[0].In.NumBufs	= 2;
	DeviceConfig.EpCfg[0].In.MaxPacketSize	= 64;

	DeviceConfig.EpCfg[1].Out.Type			= XUSBPS_EP_TYPE_BULK;
	DeviceConfig.EpCfg[1].Out.NumBufs		= 512;
	DeviceConfig.EpCfg[1].Out.BufSize		= 512;
	DeviceConfig.EpCfg[1].Out.MaxPacketSize	= 512;
	DeviceConfig.EpCfg[1].In.Type			= XUSBPS_EP_TYPE_BULK;
	DeviceConfig.EpCfg[1].In.NumBufs		= 512;
	DeviceConfig.EpCfg[1].In.MaxPacketSize	= 512;

	DeviceConfig.NumEndpoints = NumEndpoints;

	/* Now that we set up the Endpoint configuration, we can ask the driver
	 * how much memory is required in order to configure the controller in
	 * DEVICE mode.
	 */
	//MemSize = XUsbPs_DeviceMemRequired(&DeviceConfig);


	/* Allocate the data structure used to store the application specific
	 * data. This data structure is opaque to the driver and will only be
	 * used by the upper layers.
	 */
	UsbInstancePtr->UserDataPtr = malloc(sizeof(XUsbPs_Local));
	if (NULL == UsbInstancePtr->UserDataPtr) {
		goto out;
	}

	MemPtr = &in_buffer[0];
	memset(&in_buffer[0], 0, BUFFER_SIZE);
	Xil_DCacheFlushRange((unsigned int)&in_buffer, BUFFER_SIZE);

	for(int i = 0; i < BUFFER_SIZE; i++) {
		out_buffer[i] = (i%10) + '0';
	}

	/* Finish the configuration of the DeviceConfig structure and configure
	 * the DEVICE side of the controller.
	 *
	 * Note that in our example the physical and virtual address of the
	 * memory buffer are the same. For systems that use an MMU the
	 * structure needs to be initialized with physical and virtual
	 * addresses respectively.
	 */
	//DeviceConfig.DMAMemVirt = (u32) MemPtr;
	DeviceConfig.DMAMemPhys = (u32) MemPtr;

	Status = XUsbPs_ConfigureDevice(UsbInstancePtr, &DeviceConfig);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	/* Set the handler for receiving frames. */
	Status = XUsbPs_IntrSetHandler(UsbInstancePtr, UsbIntrHandler, NULL,XUSBPS_IXR_UE_MASK);
	if (XST_SUCCESS != Status) {
		goto out;
	}

    Status = XUsbPs_IntrSetHandler(UsbInstancePtr,UsbIfPhyIntrHandler, UsbInstancePtr, XUSBPS_IXR_ULPI_MASK);
    if(XST_SUCCESS != Status)
    {
    	xil_printf("XST_FAILURE\r\n");
    	return XST_FAILURE;
    }
	/* Set the handler for handling endpoint 0 events. This is where we
	 * will receive and handle the Setup packet from the host.
	 */
	Status = XUsbPs_EpSetHandler(UsbInstancePtr, 0,
				XUSBPS_EP_DIRECTION_OUT,
				XUsbPs_Ep0EventHandler, UsbInstancePtr);

	/* Set the handler for handling endpoint 1 events.
	 *
	 * Note that for this example we do not need to register a handler for
	 * TX complete events as we only send data using static data buffers
	 * that do not need to be free()d or returned to the OS after they have
	 * been sent.
	 */
	Status = XUsbPs_EpSetHandler(	UsbInstancePtr,
									1,
									XUSBPS_EP_DIRECTION_IN | XUSBPS_EP_DIRECTION_OUT,
									XUsbPs_Ep1EventHandler,
									UsbInstancePtr);

	/* Enable the interrupts. */
	XUsbPs_IntrEnable(UsbInstancePtr, XUSBPS_IXR_UR_MASK |
					   XUSBPS_IXR_UI_MASK);

	XUsbPs_IntrEnable(UsbInstancePtr, XUSBPS_IXR_ULPI_MASK);
	/* Start the USB engine */
	XUsbPs_Start(UsbInstancePtr);
	/* At this point we wait for the user to plug in the usb plug. This
	 * will cause the host to send USB packets. Once we received something,
	 * we clean up and stop the controller.
	 *
	 * This will not really work if we want to use the USB storage
	 * example. What can we do instead?
	 */
	/*
	xil_printf("BaseAddress %x\r\n",UsbInstancePtr->Config.BaseAddress);
    xil_printf("REG Vendor ID Low %x\r\n",My_UlpiPhyReadRegister(UsbInstancePtr, 0x00));

    Status = My_UlpiPhyWriteRegister(UsbInstancePtr, 0x04, 0x40);
    if(Status == XST_DEVICE_BUSY)
    {
    	xil_printf("ERROR FUNCTION CONTROL\n\r");

    }
    while(!PhyAccessDone);
    PhyAccessDone = 0;
    xil_printf("REG FUNCTION CONTROL %x\r\n",My_UlpiPhyReadRegister(UsbInstancePtr, 0x04));


    Status = My_UlpiPhyWriteRegister(UsbInstancePtr, 0x0A, 0x00);
    if(Status == XST_DEVICE_BUSY)
    {
    	xil_printf("ERROR OTG CONTROL\n\r");

    }
    while(!PhyAccessDone);
    PhyAccessDone = 0;
    xil_printf("REG OTG CONTROL %x\r\n",My_UlpiPhyReadRegister(UsbInstancePtr, 0x0A));
*/
//	for(i = 0; i < 512; i++)
//	{
//		packet[i] = (u8)i;
//	}
	//packet[0]=111;

	while( NumIrqsRX != PACKETS_TO_TRANSFER ) {
		;
	}

	xil_printf("All received!\n\r");


	Status = XUsbPs_EpBufferSend(UsbInstancePtr, 1, out_buffer, BUFFER_SIZE);

	while( NumIrqsTX != 64*NUM_MB ) {
		;
	}

	xil_printf("All sent! %d\n\r", NumIrqsTX);

	for(int i = 0; i < 12; i++) {
		//if((i % 633) == 0) {
			xil_printf("in_buff[%d] = %c\n\r", i, in_buffer[i]);
		//}
	}


	/* Set return code to indicate success and fall through to clean-up
	 * code.
	 */
	ReturnStatus = XST_SUCCESS;

out:
	/* Clean up. It's always safe to disable interrupts and clear the
	 * handlers, even if they have not been enabled/set. The same is true
	 * for disabling the interrupt subsystem.
	 */
	XUsbPs_Stop(UsbInstancePtr);
	XUsbPs_IntrDisable(UsbInstancePtr, XUSBPS_IXR_ALL);
	(int) XUsbPs_IntrSetHandler(UsbInstancePtr, NULL, NULL, 0);

	UsbDisableIntrSystem(IntcInstancePtr, UsbIntrId);

	/* Free allocated memory.
	 */
	if (NULL != UsbInstancePtr->UserDataPtr) {
		free(UsbInstancePtr->UserDataPtr);
	}
	return ReturnStatus;
}

/*****************************************************************************/
/**
 *
 * This function is the handler which performs processing for the USB driver.
 * It is called from an interrupt context such that the amount of processing
 * performed should be minimized.
 *
 * This handler provides an example of how to handle USB interrupts and
 * is application specific.
 *
 * @param	CallBackRef is the Upper layer callback reference passed back
 *		when the callback function is invoked.
 * @param 	Mask is the Interrupt Mask.
 * @param	CallBackRef is the User data reference.
 *
 * @return
 * 		- XST_SUCCESS if successful
 * 		- XST_FAILURE on error
 *
 * @note	None.
 *
 ******************************************************************************/
static void UsbIntrHandler(void *CallBackRef, u32 Mask)
{
	//NumIrqs++;
}


/*****************************************************************************/
/**
* This funtion is registered to handle callbacks for endpoint 0 (Control).
*
* It is called from an interrupt context such that the amount of processing
* performed should be minimized.
*
*
* @param	CallBackRef is the reference passed in when the function
*		was registered.
* @param	EpNum is the Number of the endpoint on which the event occured.
* @param	EventType is type of the event that occured.
*
* @return	None.
*
******************************************************************************/
static void XUsbPs_Ep0EventHandler(void *CallBackRef, u8 EpNum,
					u8 EventType, void *Data)
{
	XUsbPs			*InstancePtr;
	int			Status;
	XUsbPs_SetupData	SetupData;
	u8	*BufferPtr;
	u32	BufferLen;
	u32	Handle;


	Xil_AssertVoid(NULL != CallBackRef);

	InstancePtr = (XUsbPs *) CallBackRef;

	switch (EventType) {

	/* Handle the Setup Packets received on Endpoint 0. */
	case XUSBPS_EP_EVENT_SETUP_DATA_RECEIVED:
		xil_printf("XUsbPs_Ep0EventHandler XUSBPS_EP_EVENT_SETUP_DATA_RECEIVED\r\n");
		Status = XUsbPs_EpGetSetupData(InstancePtr, EpNum, &SetupData);
		if (XST_SUCCESS == Status) {
			/* Handle the setup packet. */
			(int) XUsbPs_Ch9HandleSetupPacket(InstancePtr,
							   &SetupData);
		}
		break;

	/* We get data RX events for 0 length packets on endpoint 0. We receive
	 * and immediately release them again here, but there's no action to be
	 * taken.
	 */
	case XUSBPS_EP_EVENT_DATA_RX:
		xil_printf("XUsbPs_Ep0EventHandler XUSBPS_EP_EVENT_DATA_RX\r\n");
		/* Get the data buffer. */
		Status = XUsbPs_EpBufferReceive(InstancePtr, EpNum,
					&BufferPtr, &BufferLen, &Handle);
		if (XST_SUCCESS == Status) {
			/* Return the buffer. */
			XUsbPs_EpBufferRelease(Handle);
		}
		break;

	default:
		/* Unhandled event. Ignore. */
		break;
	}
}


/*****************************************************************************/
/**
* This funtion is registered to handle callbacks for endpoint 1 (Bulk data).
*
* It is called from an interrupt context such that the amount of processing
* performed should be minimized.
*
*
* @param	CallBackRef is the reference passed in when the function was
*		registered.
* @param	EpNum is the Number of the endpoint on which the event occured.
* @param	EventType is type of the event that occured.
*
* @return	None.
*
* @note 	None.
*
******************************************************************************/
static void XUsbPs_Ep1EventHandler(void *CallBackRef, u8 EpNum,
					u8 EventType, void *Data)
{
	XUsbPs *InstancePtr;
	int Status;
	u8	*BufferPtr;
	u32	BufferLen;
	u32 InavalidateLen;
	u32	Handle;


	Xil_AssertVoid(NULL != CallBackRef);
	InstancePtr = (XUsbPs *) CallBackRef;
	switch (EventType) {
	case XUSBPS_EP_EVENT_DATA_RX:
		Status = XUsbPs_EpBufferReceive(InstancePtr, EpNum, &BufferPtr, &BufferLen, &Handle);

		memcpy(&in_buffer[0], &BufferPtr[0], BufferLen);	//works but not correct
		//memcpy(&in_buffer[PACKET_SIZE*NumIrqsRX], &BufferPtr[0], BufferLen);	//not working, no idea why

		InavalidateLen =  BufferLen;
		if (BufferLen % 32) {
			InavalidateLen = (BufferLen/32) * 32 + 32;
		}
		Xil_DCacheInvalidateRange((unsigned int)BufferPtr,InavalidateLen);
		XUsbPs_EpBufferRelease(Handle);

		NumIrqsRX++;	//Counts interrupts when receiving data from PC
		break;

	case XUSBPS_EP_EVENT_DATA_TX:

		NumIrqsTX++;	//Counts interrupts when sending data to PC	-- warning: measuring speed now won't work
		break;

	default:
		/* Unhandled event. Ignore. */
		break;
	}
}

/*****************************************************************************/
/**
*
* This function setups the interrupt system such that interrupts can occur for
* the USB controller. This function is application specific since the actual
* system may or may not have an interrupt controller. The USB controller could
* be directly connected to a processor without an interrupt controller.  The
* user should modify this function to fit the application.
*
* @param	IntcInstancePtr is a pointer to instance of the Intc controller.
* @param	UsbInstancePtr is a pointer to instance of the USB controller.
* @param	UsbIntrId is the Interrupt Id and is typically
* 		XPAR_<INTC_instance>_<USB_instance>_VEC_ID value
* 		from xparameters.h
*
* @return
* 		- XST_SUCCESS if successful
* 		- XST_FAILURE on error
*
******************************************************************************/
static int UsbSetupIntrSystem(XScuGic *IntcInstancePtr,
			      XUsbPs *UsbInstancePtr, u16 UsbIntrId,
				  XScuTimer *TimerInstancePtr, u16 TimerIntrId)
{
	int Status;
	XScuGic_Config *IntcConfig;

	/*
	 * Initialize the interrupt controller driver so that it is ready to
	 * use.
	 */
	IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
	if (NULL == IntcConfig) {
		return XST_FAILURE;
	}
	Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig,
					IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	Xil_ExceptionInit();
	/*
	 * Connect the interrupt controller interrupt handler to the hardware
	 * interrupt handling logic in the processor.
	 */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
				    (Xil_ExceptionHandler)XScuGic_InterruptHandler,
				    IntcInstancePtr);
	/*
	 * Connect the device driver handler that will be called when an
	 * interrupt for the device occurs, the handler defined above performs
	 * the specific interrupt processing for the device.
	 */
	Status = XScuGic_Connect(IntcInstancePtr, UsbIntrId,
				(Xil_ExceptionHandler)XUsbPs_IntrHandler,
				(void *)UsbInstancePtr);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	Status = XScuGic_Connect(IntcInstancePtr, TimerIntrId,
				(Xil_ExceptionHandler)TimerIntrHandler,
				(void *)TimerInstancePtr);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	/*
	 * Enable the interrupt for the device.
	 */
	XScuGic_Enable(IntcInstancePtr, UsbIntrId);
	XScuGic_Enable(IntcInstancePtr, TimerIntrId);

	XScuTimer_EnableInterrupt(TimerInstancePtr);

	/*
	 * Enable interrupts in the Processor.
	 */
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function disables the interrupts that occur for the USB controller.
*
* @param	IntcInstancePtr is a pointer to instance of the INTC driver.
* @param	UsbIntrId is the Interrupt Id and is typically
* 		XPAR_<INTC_instance>_<USB_instance>_VEC_ID value
* 		from xparameters.h
*
* @return	None
*
* @note		None.
*
******************************************************************************/
static void UsbDisableIntrSystem(XScuGic *IntcInstancePtr, u16 UsbIntrId)
{
	/* Disconnect and disable the interrupt for the USB controller. */
	XScuGic_Disconnect(IntcInstancePtr, UsbIntrId);
}
