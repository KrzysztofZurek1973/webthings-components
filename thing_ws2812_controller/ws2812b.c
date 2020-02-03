/*
 * ws2812b.c
 *
 *  Created on: Apr 11, 2019
 *      Author: kz
 */
#include <string.h>

#include "ws2812b.h"

#define HIGH 0x06   //1 1 0
#define LOW 0x04    //1 0 0

uint32_t spi_tx_counter;


/***********************************************************************
 *
 * convert RGB value for every diode into bit steam for SPI sending
 *
 * *********************************************************************/
int convertRgb2Bits(unsigned char *inBuff,
					unsigned char *outBuff,
					int inBytes){
	int i, j, k, codePos, codePosRest;
	unsigned char byte, code, codeCopy, bit;

    memset(outBuff, 0, inBytes * 3);

    i = 0; j = 0; k = 0;
	codePos = 8;
	while (i < inBytes){
		byte = inBuff[i];
		for (k = 0; k < 8; k++){
			bit = byte & 0x80;
			byte = byte << 1;
			if (bit){
				code = HIGH;
			}
			else{
				code = LOW;
			}

			codePos -= 3;
			if (codePos > 0){
				outBuff[j] |= code << codePos;
			}
			else if (codePos == 0){
				outBuff[j] |= code;
				j++;
				codePos = 8;
			}
			else{
				codePosRest = -codePos;
				codeCopy = code;
				code = code >> codePosRest;
				outBuff[j] |= code;
				j++;
				codePos = 8 - codePosRest;
				code = codeCopy << codePos;
				outBuff[j] |= code;
			}
		}
		i++;
	}

	return 0;
}


/********************************************************************
*
* SPI after transfer callback - for test only
*
* *******************************************************************/
void IRAM_ATTR spiAfterCallback(spi_transaction_t *t){

	spi_tx_counter++;
	if (spi_tx_counter%10 == 0){
		//printf("spi transmit counter: %i\n", spi_tx_counter);
	}
}


