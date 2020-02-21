

#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "MIMXRT1011.h"

#include "fsl_debug_console.h"
#include "CK_TIME.h"
#include "virtual_com.h"


uint8_t rx_buffer[128];
uint16_t rx_buffer_index = 0;

int main(void){

  	/* Init board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
  	/* Init FSL debug console. */
    BOARD_InitDebugConsole();

    CK_TIME_Init();

    CK_USBD_Init();

    while (1){

    	uint8_t data;
    	while(CK_USBD_ReadData(&data) == 1){

    		rx_buffer[rx_buffer_index++] = data;
	}

    	if(rx_buffer_index){

    		// Besides string int etc printer methods,
    		// I will add the received data to TxBuffer directly and transfer back.
    		CK_USBD_StringPrint("Received Data is :");

    		for(int i = 0; i < rx_buffer_index; i++){
    			CK_USBD_WriteTxBuffer(rx_buffer[i]);
    		}
    		CK_USBD_WriteTxBuffer('\n');

    		CK_USBD_Transfer();

    		rx_buffer_index = 0;

    	}


        CK_TIME_DelayMilliSec(100);


    }
}
