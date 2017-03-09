﻿/**
 * Leg Module Deci Zebro
 * The Zebro Project
 * Delft University of Technology
 * 2017
 *
 * Filename: uart1.h
 *
 * Description:
 * Code to drive UARTC0, used for debugging
 *
 * Authors:
 * Piet De Vaere -- Piet@DeVae.re
 * Daniel Booms -- d.booms@solcon.nl
 */


#include "asf.h"
#include "../inc/uart1.h"
#include "../inc/leds.h"
#include "../inc/vregs.h"

uint8_t vregs[VREGS_FILE_TOTAL_SIZE];
uint8_t vregs_buffer[VREGS_NUM_OF_BUFFERS][VREGS_FILE_TOTAL_SIZE];



int8_t uart1_init(void){
	uart1_pins_init();
	
	/* Enable USART transmitter */
	USARTC0_CTRLB |= USART_TXEN_bm;
	/* Set communication mode to UART */
	USARTC0_CTRLC |= USART_CMODE_ASYNCHRONOUS_gc;
	/* Set data frame length to 8-bit */
	USARTC0_CTRLC |= USART_CHSIZE_8BIT_gc;
	/* Set baud rate to 115.2 kbaud */
	/*BSEL = 131, BSCALE = -3, see page 213 of the reference manual */
	USARTC0_BAUDCTRLA |= 0x83; /* 131 */
	USARTC0_BAUDCTRLB |= (0b1101<<4) /* -3 */
	
	return 0;
}

/**
 * Initialise the UART, and set it up for continuously transferring information
 * from the vregs over DMA.
 */
int32_t uart1_init_dma(void){

	/*
	 * FIRST set up the UART
	 * */
	uart1_pins_init();

	/* Enable USART transmitter */
	USARTC0_CTRLB |= USART_TXEN_bm;
	/* Set communication mode to UART */
	USARTC0_CTRLC |= USART_CMODE_ASYNCHRONOUS_gc;
	/* Set data frame length to 8-bit */
	USARTC0_CTRLC |= USART_CHSIZE_8BIT_gc;
	/* Set baud rate to 115.2 kbaud */
	/*BSEL = 131, BSCALE = -3, see page 213 of the reference manual */
	USARTC0_BAUDCTRLA |= 0x83; /* 131 */
	USARTC0_BAUDCTRLB |= (0b1101<<4) /* -3 */
	

	/*
	 * SECOND set up the DMA channel
	 * DMA channel 3 is used for UART
	 * The block is the 256 bytes of the VREG, 
	 * The block consists of bursts of 1 byte
	 * */
	
	/* Enable DMA controller, leave further settings at default*/
	DMA_CTRL |= DMA_ENABLE_bm;
	
	/* Reload start address after each block transfer */
	DMA_CH3_ADDRCTRL |= DMA_CH_SRCRELOAD_BLOCK_gc;
	/* Increment address after each burst */
	DMA_CH3_ADDRCTRL |= DMA_CH_SRCDIR_INC_gc;
	/* Do not alter destination address, leave reload setting at default: none*/
	DMA_CH3_ADDRCTRL |= DMA_CH_DESTDIR_FIXED_gc;
	/* Set UART as a trigger for the DMA */
	DMA_CH3_TRIGSRC = UART1_TRIGGER_ADDRESS;
	/* Set transfer count in bytes to size of VREGS */
	DMA_CH3_TRFCNT	= VREGS_FILE_TOTAL_SIZE;
	/* Set repeat count to unlimited */
	DMA_CH3_REPCNT = 0;
	/* Read the data from the VREGS */
	DMA_CH3_SRCADDR0 =  (uint8_t) vregs_get_buffer_address();
	/* And write it to the UART */
	DMA_CH3_DESTADDR0 = (uint8_t) &USARTC0_DATA;
	/* Set single shot and repeat mode */
	DMA_CH3_CTRLA |= DMA_CH_SINGLE_bm;
	DMA_CH3_CTRLA |= DMA_CH_REPEAT_bm;
		
	/*
	 * THIRD enable the DMA channel
	 */
	DMA_CH3_CTRLA |= DMA_ENABLE_bm;

	return 0;
}

/**
 * Wait for the last UART data dump to be finished
 */
int32_t uart1_wait_until_done(void){
	/* wait until the DMA says the transfer is done */
	while(!(DMA1->ISR & DMA_ISR_TCIF4));
	/* we don't reset the flag here, because that is done in
	 * uart1_trigger_dma_once() */

	return 0;
}



/**
 * Write the correct data to the vregs, and transmit it over the UART
 */
int32_t uart1_trigger_dma_once(void){
	/* if the transfer is done */
	if(DMA1->ISR & DMA_ISR_TCIF4){
		/* copy the right data in to the vreg data buffer */
		vregs_writeout();
		/* clear the flag */
		DMA1->IFCR |= DMA_IFCR_CTCIF4;
		/* disable the channel */
		DMA1_Channel4->CCR &= ~DMA_CCR_EN;
		/* set the origin of the data */
		DMA1_Channel4->CMAR = (uint32_t) vregs_get_buffer_address();
		/* set the number of bytes to be transfered */
		DMA1_Channel4->CNDTR = VREGS_FILE_TOTAL_SIZE;
		/* enable the channel: send the data */
		DMA1_Channel4->CCR |= DMA_CCR_EN;
	}

	return 0;
}

/**
 * Configures the pins corresponding to UART1
 */
int32_t uart1_pins_init(void){
	GPIO_InitTypeDef  GPIO_InitStruct;

	/* enable UART1 GPIO clocks */
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/* set the GPIO pins */
	GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull  = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

	GPIO_InitStruct.Alternate = GPIO_AF0_USART1;
	GPIO_InitStruct.Pin = UART1_TX_PIN;
	HAL_GPIO_Init(UART1_TX_BANK, &GPIO_InitStruct);

	GPIO_InitStruct.Alternate = GPIO_AF0_USART1;
	GPIO_InitStruct.Pin = GPIO_PIN_6;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	GPIO_InitStruct.Alternate = GPIO_AF1_USART1;
	GPIO_InitStruct.Pin = UART1_RX_PIN;
	HAL_GPIO_Init(UART1_RX_BANK, &GPIO_InitStruct);

	return 0;
}

/**
 * Send out a raw byte over UART1
 * This function uses busy waits
 */
int32_t uart1_send_raw(uint8_t tx_data){
	/* wait for transmit data register to be empty */
	while(!(USART1->ISR & USART_ISR_TXE));

	/* place new data in transmit data register */
	USART1->TDR = tx_data;

	return 0;
}
