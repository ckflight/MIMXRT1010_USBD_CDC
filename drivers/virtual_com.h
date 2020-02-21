
#ifndef _USB_CDC_VCOM_H_
#define _USB_CDC_VCOM_H_ 1

#include "usb_device_descriptor.h"
#include "usb.h"

/*******************************************************************************
* Definitions
******************************************************************************/
#if defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0)
#define CONTROLLER_ID kUSB_ControllerEhci0
#define DATA_BUFF_SIZE HS_CDC_VCOM_BULK_OUT_PACKET_SIZE

#endif

#define USB_DEVICE_INTERRUPT_PRIORITY (3U)
/* Currently configured line coding */
#define LINE_CODING_SIZE (0x07)
#define LINE_CODING_DTERATE (115200)
#define LINE_CODING_CHARFORMAT (0x00)
#define LINE_CODING_PARITYTYPE (0x00)
#define LINE_CODING_DATABITS (0x08)

/* Communications feature */
#define COMM_FEATURE_DATA_SIZE (0x02)
#define STATUS_ABSTRACT_STATE (0x0000)
#define COUNTRY_SETTING (0x0000)

/* Notification of serial state */
#define NOTIF_PACKET_SIZE (0x08)
#define UART_BITMAP_SIZE (0x02)
#define NOTIF_REQUEST_TYPE (0xA1)

/* Define the types for application */
typedef struct _usb_cdc_vcom_struct
{
    usb_device_handle deviceHandle; /* USB device handle. */
    class_handle_t cdcAcmHandle; /* USB CDC ACM class handle.                                                         */
    volatile uint8_t attach;     /* A flag to indicate whether a usb device is attached. 1: attached, 0: not attached */
    uint8_t speed;               /* Speed of USB device. USB_SPEED_FULL/USB_SPEED_LOW/USB_SPEED_HIGH.                 */
    volatile uint8_t
        startTransactions; /* A flag to indicate whether a CDC device is ready to transmit and receive data.    */
    uint8_t currentConfiguration; /* Current configuration value. */
    uint8_t currentInterfaceAlternateSetting
        [USB_CDC_VCOM_INTERFACE_COUNT]; /* Current alternate setting value for each interface. */
} usb_cdc_vcom_struct_t;

/* Define the information relates to abstract control model */
typedef struct _usb_cdc_acm_info
{
    uint8_t serialStateBuf[NOTIF_PACKET_SIZE + UART_BITMAP_SIZE]; /* Serial state buffer of the CDC device to notify the
                                                                     serial state to host. */
    bool dtePresent;          /* A flag to indicate whether DTE is present.         */
    uint16_t breakDuration;   /* Length of time in milliseconds of the break signal */
    uint8_t dteStatus;        /* Status of data terminal equipment                  */
    uint8_t currentInterface; /* Current interface index.                           */
    uint16_t uartState;       /* UART state of the CDC device.                      */
} usb_cdc_acm_info_t;


extern usb_device_endpoint_struct_t g_UsbDeviceCdcVcomDicEndpoints[];
extern usb_device_class_struct_t g_UsbDeviceCdcVcomConfig;

usb_status_t USB_DeviceCdcVcomCallback(class_handle_t handle, uint32_t event, void *param);
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param);

void USB_DeviceClockInit(void);
void USB_DeviceIsrEnable(void);

usb_status_t USB_DeviceCdcVcomCallback(class_handle_t handle, uint32_t event, void *param);
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param);


void CK_USBD_Init(void);

usb_status_t CK_USBD_Transfer(void);

void CK_USBD_Receive(uint8_t* rx_buffer);

int CK_USBD_ReadData(uint8_t* data);

int CK_USBD_WriteRxCircularBuffer(uint8_t* Buf, uint32_t* Len);

int CK_USBD_WriteTxBuffer(uint8_t data);

void CK_USBD_IntPrint(int32_t num);

void CK_USBD_IntPrintln(int32_t num);

void CK_USBD_FloatPrintln(float num);

void CK_USBD_FloatPrint(float num);

void CK_USBD_StringPrintln(const char str[]);

void CK_USBD_StringPrint(const char str[]);




#endif /* _USB_CDC_VCOM_H_ */
