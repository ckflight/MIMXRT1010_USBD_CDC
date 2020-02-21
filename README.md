# MIMXRT1011_USBD_CDC
USB Device Com Port Implementation on NXP MIMXRT1011 Arm Cortex-M7 Microcontroller

When creating project on MCUXpresso IDE, select "Usb_device" from "Middleware" section which will include /usb folder
in the project directory. 

Use my virtual_com.c/.h files for user interface to interact with USBD CDC.

Circular buffer for reception and a fast simple transfer buffer with 512 byte size is implemented. Also, printer methods are implemented to easily print integer, string, float etc.

