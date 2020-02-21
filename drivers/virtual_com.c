
#include "board.h"

#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"

#include "usb_device_class.h"
#include "usb_device_cdc_acm.h"
#include "usb_device_ch9.h"

#include "virtual_com.h"

#if ((defined FSL_FEATURE_SOC_USBPHY_COUNT) && (FSL_FEATURE_SOC_USBPHY_COUNT > 0U))
#include "usb_phy.h"
#endif

#include "CK_USBD_BUFFER.h"

/* Line coding of cdc device */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_lineCoding[LINE_CODING_SIZE] = {
    /* E.g. 0x00,0xC2,0x01,0x00 : 0x0001C200 is 115200 bits per second */
    (LINE_CODING_DTERATE >> 0U) & 0x000000FFU,
    (LINE_CODING_DTERATE >> 8U) & 0x000000FFU,
    (LINE_CODING_DTERATE >> 16U) & 0x000000FFU,
    (LINE_CODING_DTERATE >> 24U) & 0x000000FFU,
    LINE_CODING_CHARFORMAT,
    LINE_CODING_PARITYTYPE,
    LINE_CODING_DATABITS};

/* Abstract state of cdc device */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t s_abstractState[COMM_FEATURE_DATA_SIZE] = {(STATUS_ABSTRACT_STATE >> 0U) & 0x00FFU,
                                                          (STATUS_ABSTRACT_STATE >> 8U) & 0x00FFU};

/* Country code of cdc device */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t s_countryCode[COMM_FEATURE_DATA_SIZE] = {(COUNTRY_SETTING >> 0U) & 0x00FFU,
                                                        (COUNTRY_SETTING >> 8U) & 0x00FFU};

/* CDC ACM information */
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) usb_cdc_acm_info_t s_usbCdcAcmInfo;
/* Data buffer for receiving and sending*/

/* Data structure of virtual com device */
usb_cdc_vcom_struct_t s_cdcVcom;


/* USB device class information */
usb_device_class_config_struct_t s_cdcAcmConfig[1] = {{
    USB_DeviceCdcVcomCallback, 0, &g_UsbDeviceCdcVcomConfig,
}};

/* USB device class configuration information */
usb_device_class_config_list_struct_t s_cdcAcmConfigList = {
    s_cdcAcmConfig, USB_DeviceCallback, 1,
};

uint8_t s_currRecvBuf[DATA_BUFF_SIZE];

uint32_t s_recvSize = 0;

uint8_t copy_buffer[DATA_BUFF_SIZE];

uint32_t copy_length = 0;

circularBuffer_t usbd_receive_cb;


void USB_OTG1_IRQHandler(void){

    USB_DeviceEhciIsrFunction(s_cdcVcom.deviceHandle);
}

void USB_DeviceClockInit(void){

	usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };

    CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, 480000000U);
    CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, 480000000U);
    USB_EhciPhyInit(CONTROLLER_ID, BOARD_XTAL0_CLK_HZ, &phyConfig);
}

void USB_DeviceIsrEnable(void){

    uint8_t irqNumber;

    uint8_t usbDeviceEhciIrq[] = USBHS_IRQS;
    irqNumber                  = usbDeviceEhciIrq[CONTROLLER_ID - kUSB_ControllerEhci0];

    /* Install isr, set priority, and enable IRQ. */
    NVIC_SetPriority((IRQn_Type)irqNumber, USB_DEVICE_INTERRUPT_PRIORITY);
    EnableIRQ((IRQn_Type)irqNumber);
}

/*!
 * @brief CDC class specific callback function.
 *
 * This function handles the CDC class specific requests.
 *
 * @param handle          The CDC ACM class handle.
 * @param event           The CDC ACM class event type.
 * @param param           The parameter of the class specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcVcomCallback(class_handle_t handle, uint32_t event, void *param){

    uint32_t len;
    uint8_t *uartBitmap;
    usb_device_cdc_acm_request_param_struct_t *acmReqParam;
    usb_device_endpoint_callback_message_struct_t *epCbParam;
    usb_status_t error = kStatus_USB_Error;
    usb_cdc_acm_info_t *acmInfo = &s_usbCdcAcmInfo;
    acmReqParam = (usb_device_cdc_acm_request_param_struct_t *)param;
    epCbParam = (usb_device_endpoint_callback_message_struct_t *)param;
    switch (event)
    {
        case kUSB_DeviceCdcEventSendResponse:
        {
            if ((epCbParam->length != 0) && (!(epCbParam->length % g_UsbDeviceCdcVcomDicEndpoints[0].maxPacketSize)))
            {
                /* If the last packet is the size of endpoint, then send also zero-ended packet,
                 ** meaning that we want to inform the host that we do not have any additional
                 ** data, so it can flush the output.
                 */
                error = USB_DeviceCdcAcmSend(handle, USB_CDC_VCOM_BULK_IN_ENDPOINT, NULL, 0);
            }
            else if ((1 == s_cdcVcom.attach) && (1 == s_cdcVcom.startTransactions))
            {
                if ((epCbParam->buffer != NULL) || ((epCbParam->buffer == NULL) && (epCbParam->length == 0)))
                {
                    /* User: add your own code for send complete event */
                    /* Schedule buffer for next receive event */
                    error = USB_DeviceCdcAcmRecv(handle, USB_CDC_VCOM_BULK_OUT_ENDPOINT, s_currRecvBuf,
                                                 g_UsbDeviceCdcVcomDicEndpoints[0].maxPacketSize);
                }
            }
            else
            {
            }
        }
        break;
        case kUSB_DeviceCdcEventRecvResponse:
        {
            if ((1 == s_cdcVcom.attach) && (1 == s_cdcVcom.startTransactions))
            {
                s_recvSize = epCbParam->length;

                // Whenever data is received it comes here and read the received data
                // to s_currRecvBuf so circular buffer will be added here.

                /* Schedule buffer for next receive event */
                error = USB_DeviceCdcAcmRecv(handle, USB_CDC_VCOM_BULK_OUT_ENDPOINT, s_currRecvBuf, g_UsbDeviceCdcVcomDicEndpoints[0].maxPacketSize);

                CK_USBD_WriteRxCircularBuffer(&s_currRecvBuf[0], &s_recvSize);

            }
        }
        break;
        case kUSB_DeviceCdcEventSerialStateNotif:
            ((usb_device_cdc_acm_struct_t *)handle)->hasSentState = 0;
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventSendEncapsulatedCommand:
            break;
        case kUSB_DeviceCdcEventGetEncapsulatedResponse:
            break;
        case kUSB_DeviceCdcEventSetCommFeature:
            if (USB_DEVICE_CDC_FEATURE_ABSTRACT_STATE == acmReqParam->setupValue)
            {
                if (1 == acmReqParam->isSetup)
                {
                    *(acmReqParam->buffer) = s_abstractState;
                }
                else
                {
                    *(acmReqParam->length) = 0;
                }
            }
            else if (USB_DEVICE_CDC_FEATURE_COUNTRY_SETTING == acmReqParam->setupValue)
            {
                if (1 == acmReqParam->isSetup)
                {
                    *(acmReqParam->buffer) = s_countryCode;
                }
                else
                {
                    *(acmReqParam->length) = 0;
                }
            }
            else
            {
            }
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventGetCommFeature:
            if (USB_DEVICE_CDC_FEATURE_ABSTRACT_STATE == acmReqParam->setupValue)
            {
                *(acmReqParam->buffer) = s_abstractState;
                *(acmReqParam->length) = COMM_FEATURE_DATA_SIZE;
            }
            else if (USB_DEVICE_CDC_FEATURE_COUNTRY_SETTING == acmReqParam->setupValue)
            {
                *(acmReqParam->buffer) = s_countryCode;
                *(acmReqParam->length) = COMM_FEATURE_DATA_SIZE;
            }
            else
            {
            }
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventClearCommFeature:
            break;
        case kUSB_DeviceCdcEventGetLineCoding:
            *(acmReqParam->buffer) = s_lineCoding;
            *(acmReqParam->length) = LINE_CODING_SIZE;
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventSetLineCoding:
        {
            if (1 == acmReqParam->isSetup)
            {
                *(acmReqParam->buffer) = s_lineCoding;
            }
            else
            {
                *(acmReqParam->length) = 0;
            }
        }
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventSetControlLineState:
        {
            s_usbCdcAcmInfo.dteStatus = acmReqParam->setupValue;
            /* activate/deactivate Tx carrier */
            if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_CARRIER_ACTIVATION)
            {
                acmInfo->uartState |= USB_DEVICE_CDC_UART_STATE_TX_CARRIER;
            }
            else
            {
                acmInfo->uartState &= (uint16_t)~USB_DEVICE_CDC_UART_STATE_TX_CARRIER;
            }

            /* activate carrier and DTE. Com port of terminal tool running on PC is open now */
            if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE)
            {
                acmInfo->uartState |= USB_DEVICE_CDC_UART_STATE_RX_CARRIER;
            }
            /* Com port of terminal tool running on PC is closed now */
            else
            {
                acmInfo->uartState &= (uint16_t)~USB_DEVICE_CDC_UART_STATE_RX_CARRIER;
            }

            /* Indicates to DCE if DTE is present or not */
            acmInfo->dtePresent = (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE) ? true : false;

            /* Initialize the serial state buffer */
            acmInfo->serialStateBuf[0] = NOTIF_REQUEST_TYPE;                /* bmRequestType */
            acmInfo->serialStateBuf[1] = USB_DEVICE_CDC_NOTIF_SERIAL_STATE; /* bNotification */
            acmInfo->serialStateBuf[2] = 0x00;                              /* wValue */
            acmInfo->serialStateBuf[3] = 0x00;
            acmInfo->serialStateBuf[4] = 0x00; /* wIndex */
            acmInfo->serialStateBuf[5] = 0x00;
            acmInfo->serialStateBuf[6] = UART_BITMAP_SIZE; /* wLength */
            acmInfo->serialStateBuf[7] = 0x00;
            /* Notify to host the line state */
            acmInfo->serialStateBuf[4] = acmReqParam->interfaceIndex;
            /* Lower byte of UART BITMAP */
            uartBitmap = (uint8_t *)&acmInfo->serialStateBuf[NOTIF_PACKET_SIZE + UART_BITMAP_SIZE - 2];
            uartBitmap[0] = acmInfo->uartState & 0xFFu;
            uartBitmap[1] = (acmInfo->uartState >> 8) & 0xFFu;
            len = (uint32_t)(NOTIF_PACKET_SIZE + UART_BITMAP_SIZE);
            if (0 == ((usb_device_cdc_acm_struct_t *)handle)->hasSentState)
            {
                error = USB_DeviceCdcAcmSend(handle, USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT, acmInfo->serialStateBuf, len);
                if (kStatus_USB_Success != error)
                {
                    usb_echo("kUSB_DeviceCdcEventSetControlLineState error!");
                }
                ((usb_device_cdc_acm_struct_t *)handle)->hasSentState = 1;
            }

            /* Update status */
            if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_CARRIER_ACTIVATION)
            {
                /*  To do: CARRIER_ACTIVATED */
            }
            else
            {
                /* To do: CARRIER_DEACTIVATED */
            }
            if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE)
            {
                /* DTE_ACTIVATED */
                if (1 == s_cdcVcom.attach)
                {
                    s_cdcVcom.startTransactions = 1;

                }
            }
            else
            {
                /* DTE_DEACTIVATED */
                if (1 == s_cdcVcom.attach)
                {
                    s_cdcVcom.startTransactions = 0;
                }
            }
        }
        break;
        case kUSB_DeviceCdcEventSendBreak:
            break;
        default:
            break;
    }

    return error;
}

/*!
 * @brief USB device callback function.
 *
 * This function handles the usb device specific requests.
 *
 * @param handle          The USB device handle.
 * @param event           The USB device event type.
 * @param param           The parameter of the device specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param){

    usb_status_t error = kStatus_USB_Error;
    uint16_t *temp16 = (uint16_t *)param;
    uint8_t *temp8 = (uint8_t *)param;

    switch (event)
    {
        case kUSB_DeviceEventBusReset:
        {
            s_cdcVcom.attach = 0;
            s_cdcVcom.currentConfiguration = 0U;
#if (defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)) || \
    (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
            /* Get USB speed to configure the device, including max packet size and interval of the endpoints. */
            if (kStatus_USB_Success == USB_DeviceClassGetSpeed(CONTROLLER_ID, &s_cdcVcom.speed))
            {
                USB_DeviceSetSpeed(handle, s_cdcVcom.speed);
            }
#endif
        }
        break;
        case kUSB_DeviceEventSetConfiguration:
            if (0U ==(*temp8))
            {
                s_cdcVcom.attach = 0;
                s_cdcVcom.currentConfiguration = 0U;
            }
            else if (USB_CDC_VCOM_CONFIGURE_INDEX == (*temp8))
            {
                s_cdcVcom.attach = 1;
                s_cdcVcom.currentConfiguration = *temp8;
                /* Schedule buffer for receive */
                USB_DeviceCdcAcmRecv(s_cdcVcom.cdcAcmHandle, USB_CDC_VCOM_BULK_OUT_ENDPOINT, s_currRecvBuf,
                                     g_UsbDeviceCdcVcomDicEndpoints[0].maxPacketSize);
            }
            else
            {
                error = kStatus_USB_InvalidRequest;
            }
            break;
        case kUSB_DeviceEventSetInterface:
            if (s_cdcVcom.attach)
            {
                uint8_t interface = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                uint8_t alternateSetting = (uint8_t)(*temp16 & 0x00FFU);
                if (interface < USB_CDC_VCOM_INTERFACE_COUNT)
                {
                    s_cdcVcom.currentInterfaceAlternateSetting[interface] = alternateSetting;
                }
            }
            break;
        case kUSB_DeviceEventGetConfiguration:
            break;
        case kUSB_DeviceEventGetInterface:
            break;
        case kUSB_DeviceEventGetDeviceDescriptor:
            if (param)
            {
                error = USB_DeviceGetDeviceDescriptor(handle, (usb_device_get_device_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetConfigurationDescriptor:
            if (param)
            {
                error = USB_DeviceGetConfigurationDescriptor(handle,
                                                             (usb_device_get_configuration_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetStringDescriptor:
            if (param)
            {
                /* Get device string descriptor request */
                error = USB_DeviceGetStringDescriptor(handle, (usb_device_get_string_descriptor_struct_t *)param);
            }
            break;
        default:
            break;
    }

    return error;
}

void CK_USBD_Init(void){

    // USB Init
    USB_DeviceClockInit();

    s_cdcVcom.speed = USB_SPEED_FULL;
    s_cdcVcom.attach = 0;
    s_cdcVcom.cdcAcmHandle = (class_handle_t)NULL;
    s_cdcVcom.deviceHandle = NULL;

    if (kStatus_USB_Success != USB_DeviceClassInit(CONTROLLER_ID, &s_cdcAcmConfigList, &s_cdcVcom.deviceHandle))
    {
        usb_echo("USB device init failed\r\n");
    }
    else
    {
        usb_echo("USB device CDC virtual com demo\r\n");
        s_cdcVcom.cdcAcmHandle = s_cdcAcmConfigList.config->classHandle;
    }

    USB_DeviceIsrEnable();

    USB_DeviceRun(s_cdcVcom.deviceHandle);

    CK_USBD_BUFFER_Init(&usbd_receive_cb, DATA_BUFF_SIZE);
}

usb_status_t CK_USBD_Transfer(void){

	usb_status_t status = kStatus_USB_Error;

	if ((1 == s_cdcVcom.attach) && (1 == s_cdcVcom.startTransactions)){

		status = USB_DeviceCdcAcmSend(s_cdcVcom.cdcAcmHandle, USB_CDC_VCOM_BULK_IN_ENDPOINT, copy_buffer, copy_length);

		copy_length = 0;

		status = kStatus_USB_Success;
	}

	return status;

}

// This will be deleted once the method below is done
void CK_USBD_Receive(uint8_t* rx_buffer){

	if((0 != s_recvSize) && (0xFFFFFFFFU != s_recvSize)){

		int32_t i;

		// Copy Buffer
		for (i = 0; i < s_recvSize; i++){

			*rx_buffer++ = s_currRecvBuf[i];
		}

		s_recvSize = 0;
	}

}

int CK_USBD_ReadData(uint8_t* data){

	if(!CK_USBD_BUFFER_IsBufferEmpty(&usbd_receive_cb)){
		CK_USBD_BUFFER_BufferRead(&usbd_receive_cb, data);
		return 1;
	}

	return 0; // Data is not available
}

int CK_USBD_WriteRxCircularBuffer(uint8_t* Buf, uint32_t* Len){

	// Later add FAST_TRANSFER mode for reception as well where
	// array should be copied to circular buffer directly once.

	uint32_t receivedLength = *Len; // point to the received length address which is the length number.

	for(int i = 0; i < receivedLength; i++){

		if(!CK_USBD_BUFFER_IsBufferFull(&usbd_receive_cb)){

			uint8_t data = Buf[i];
			CK_USBD_BUFFER_BufferWrite(&usbd_receive_cb, data);

		}
		else{
			return 0; // Full, could not write
		}
	}

	return 1; // OK
}

int CK_USBD_WriteTxBuffer(uint8_t data){

	if(copy_length <= DATA_BUFF_SIZE){
		copy_buffer[copy_length++] = data;
		return 1; // OK
	}

	return 0; // Full, could not write

}


void CK_USBD_IntPrint(int32_t num){
	if(num < 0){
		CK_USBD_WriteTxBuffer((uint8_t)'-');
		num *= -1;
	}
	int32_t tmp[10];
	int numOfDigits = 0;
	int limit = 10;

	for(int i=0; i < limit; i++){
		if(num >= 10){
			tmp[i] = (int32_t)num % 10;
			num = num - tmp[i];
			num = num / 10;
		}
		else{
			tmp[i] = num;
			numOfDigits = i;
			i = limit;
		}
	}
	for(int i = numOfDigits; i >= 0; i--){
		CK_USBD_WriteTxBuffer((uint8_t)tmp[i] + 48); //for ASCII code of number
	}
}

void CK_USBD_IntPrintln(int32_t num){
	if(num < 0){
		CK_USBD_WriteTxBuffer((uint8_t)'-');
		num *= -1;
	}
	int32_t tmp[10];
	int numOfDigits = 0;
	int limit = 10;

	for(int i = 0; i < limit; i++){
		if(num >= 10){
			tmp[i] = (int32_t)num % 10;
			num = num - tmp[i];
			num = num / 10;
		}
		else{
			tmp[i] = num;
			numOfDigits = i;
			i = limit;
		}
	}
	for(int i=numOfDigits; i >= 0; i--){
		CK_USBD_WriteTxBuffer((uint8_t)tmp[i] + 48); //for ASCII code of number
	}
	CK_USBD_WriteTxBuffer((uint8_t)10); //for ASCII new line

}

void CK_USBD_FloatPrintln(float num){
	int flag = 0;
	num = num * 100; // 2 digit after comma
	int intnum = (int)num;
	const int limit = 10; // 10 digit number max
	if(intnum < 0){
		CK_USBD_WriteTxBuffer((uint8_t)'-');
		intnum *= -1;
	}
	if(intnum < 10){ // 0.00
		flag = 1;
	}
	else if(intnum >= 10 && intnum <= 99){ // 0.01 to 0.99
		flag = 2;
	}

	int tmp[limit];
	int numOfDigits = 0;
	for(int i = 0; i < limit; i++){
		if(intnum>=10){
			tmp[i] = (int)intnum % 10;
			intnum = intnum - tmp[i];
			intnum = intnum / 10;
		}
		else{
			tmp[i] = intnum;
			numOfDigits = i; // get num of digits
			i = limit; // end loop
		}
	}
	if(flag==1){ // make 0.00
		tmp[1] = 0;tmp[2] = 0;
		numOfDigits = 2;
	}
	if(flag==2){ // add 0 for 0.01 to 0.99 numbers
		tmp[++numOfDigits] = 0;
	}

	for(int i=numOfDigits; i >= 0; i--){
		if(i>1){
			CK_USBD_WriteTxBuffer((uint8_t)tmp[i] + 48); //for ASCII code of number
		}
		else if(i==1){
			CK_USBD_WriteTxBuffer((uint8_t)46); //for ASCII '.'
			CK_USBD_WriteTxBuffer((uint8_t)tmp[i] + 48); //for ASCII code of number
		}
		else{
			CK_USBD_WriteTxBuffer((uint8_t)tmp[i] + 48); //for ASCII code of number
		}
	}
	CK_USBD_WriteTxBuffer((uint8_t)10); //for ASCII new line

}

void CK_USBD_FloatPrint(float num){
	int flag = 0;
	num = num * 100; // 2 digit after comma
	int intnum = (int)num;
	const int limit = 10; // 10 digit number max
	if(intnum < 0){
		CK_USBD_WriteTxBuffer((uint8_t)'-');
		intnum *= -1;
	}
	if(intnum < 10){ // 0.00
		flag = 1;
	}
	else if(intnum >= 10 && intnum <= 99){ // 0.01 to 0.99
		flag = 2;
	}

	int tmp[limit];
	int numOfDigits = 0;
	for(int i = 0; i < limit; i++){
		if(intnum >= 10){
			tmp[i] = (int)intnum % 10;
			intnum = intnum - tmp[i];
			intnum = intnum / 10;
		}
		else{
			tmp[i] = intnum;
			numOfDigits = i; // get num of digits
			i = limit; // end loop
		}
	}
	if(flag == 1){ // make 0.00
		tmp[1] = 0;tmp[2] = 0;
		numOfDigits = 2;
	}
	if(flag == 2){ // add 0 for 0.01 to 0.99 numbers
		tmp[++numOfDigits] = 0;
	}

	for(int i = numOfDigits; i >= 0; i--){
		if(i > 1){
			CK_USBD_WriteTxBuffer((uint8_t)tmp[i] + 48); //for ASCII code of number
		}
		else if(i == 1){
			CK_USBD_WriteTxBuffer((uint8_t)46); //for ASCII '.'
			CK_USBD_WriteTxBuffer((uint8_t)tmp[i] + 48); //for ASCII code of number
		}
		else{
			CK_USBD_WriteTxBuffer((uint8_t)tmp[i] + 48); //for ASCII code of number
		}
	}

}

void CK_USBD_StringPrintln(const char str[]){

	int size = strlen(str);

	for(int i = 0; i < size; i++){
		CK_USBD_WriteTxBuffer((uint8_t)str[i]);
	}

	CK_USBD_WriteTxBuffer((uint8_t)10); //for ASCII new line

}

void CK_USBD_StringPrint(const char str[]){

	int size = strlen(str);

	for(int i = 0; i < size; i++){
		CK_USBD_WriteTxBuffer((uint8_t)str[i]);
	}

}












