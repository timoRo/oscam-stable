#ifndef SU980
#define SU980
#endif
#if defined(COOL) ||defined(SU980)
/*
		ifd_cool.c
		This module provides IFD handling functions for Coolstream internal reader.
*/

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include"ifd_cool.h"
#include"../globals.h"
#include"icc_async.h"
#include"../module-coolapi.h"

#define READ_WRITE_TRANSMIT_TIMEOUT				50

struct s_coolstream_reader {
	void      *handle; //device handle for coolstream
	char      cardbuffer[256];
	int32_t		cardbuflen;
	int32_t		read_write_transmit_timeout;
};

#define specdev() \
 ((struct s_coolstream_reader *)reader->spec_dev)

int32_t Cool_Init (struct s_reader *reader)
{
	char *device = reader->device;
	int32_t reader_nb = 0;
	int ret = 0;

	cs_debug_mask(D_DEVICE,"cool init %s\n", device);
	// this is to stay compatible with older config.
	if(strlen(device))
		reader_nb=atoi((const char *)device);
	if(reader_nb>1) {
		// there are only 2 readers in the coolstream : 0 or 1
		cs_log("Coolstream reader device can only be 0 or 1");
		return FALSE;
	}
	reader->spec_dev=malloc(sizeof(struct s_coolstream_reader));
	call(cnxt_smc_open (&specdev()->handle, &reader_nb, NULL, NULL));

#if defined(COOL)
	ret = cnxt_smc_enable_flow_control(specdev()->handle);
#elif defined(SU980)
	ret = cnxt_smc_enable_flow_control(specdev()->handle, TRUE);
#endif
	check_error("cnxt_smc_enable_flow_control", ret);

	specdev()->cardbuflen = 0;
#if 0
	if (reader->cool_timeout_init > 0) {
		cs_debug_mask(D_DEVICE,"%s init timeout set to cool_timeout_init = %i", reader->label, reader->cool_timeout_init);
		specdev()->read_write_transmit_timeout = reader->cool_timeout_init;
	} else {
		cs_debug_mask(D_DEVICE,"No init timeout for reader %s specified - using default init timeout (%i). If you encounter any problems while card init try to use the reader parameter cool_timeout_init = 500", reader->label, READ_WRITE_TRANSMIT_TIMEOUT);
		specdev()->read_write_transmit_timeout = READ_WRITE_TRANSMIT_TIMEOUT;
	}
#endif
	return OK;
}


int32_t Cool_GetStatus (struct s_reader *reader, int32_t * in)
{
	if (cool_kal_opened) {
		int32_t state;
		int32_t ret = cnxt_smc_get_state(specdev()->handle, &state);
		if (ret) {
			check_error("cnxt_smc_get_state", ret);
			return ERROR;
		}
		//state = 0 no card, 1 = not ready, 2 = ready
		if (state)
			*in = 1; //CARD, even if not ready report card is in, or it will never get activated
		else
			*in = 0; //NOCARD
	} else {
		*in = 0;
	}
	return OK;
}

int32_t Cool_Reset (struct s_reader *reader, ATR * atr)
{
	//set freq to reader->cardmhz if necessary
	uint32_t clk;
	int32_t ret;

	ret = cnxt_smc_get_clock_freq (specdev()->handle, &clk);
	check_error("cnxt_smc_get_clock_freq", ret);
	if (clk/10000 != reader->cardmhz) {
		cs_debug_mask(D_DEVICE,"COOL: %s clock freq: %i, scheduling change to %i for card reset", reader->label, clk, reader->cardmhz*10000);
		call (Cool_SetClockrate(reader, reader->cardmhz));
	} 

	//reset card
	ret = cnxt_smc_reset_card (specdev()->handle, ATR_TIMEOUT, FALSE, NULL);
	check_error("cnxt_smc_reset_card", ret);
	cs_sleepms(50);
	int32_t n = 40;
	unsigned char buf[40];
	ret = cnxt_smc_get_atr (specdev()->handle, buf, &n);
	check_error("cnxt_smc_get_atr", ret);
		
	call (!ATR_InitFromArray (atr, buf, n) == ATR_OK);
	{
		cs_sleepms(50);
		return OK;
	}
}

int32_t Cool_Transmit (struct s_reader *reader, BYTE * sent, uint32_t size)
{ 
	specdev()->cardbuflen = 256;//it needs to know max buffer size to respond?

	int32_t ret = cnxt_smc_read_write(specdev()->handle, FALSE, sent, size, specdev()->cardbuffer, &specdev()->cardbuflen, specdev()->read_write_transmit_timeout, 0);
	check_error("cnxt_smc_read_write", ret);

	cs_ddump_mask(D_DEVICE, sent, size, "COOL IO: Transmit: ");	
	return OK;
}

int32_t Cool_Receive (struct s_reader *reader, BYTE * data, uint32_t size)
{ 
	if (size > specdev()->cardbuflen)
		size = specdev()->cardbuflen; //never read past end of buffer
	memcpy(data, specdev()->cardbuffer, size);
	specdev()->cardbuflen -= size;
	memmove(specdev()->cardbuffer, specdev()->cardbuffer+size, specdev()->cardbuflen);
	cs_ddump_mask(D_DEVICE, data, size, "COOL IO: Receive: ");
	return OK;
}	

int32_t Cool_SetClockrate (struct s_reader *reader, int32_t mhz)
{
	uint32_t clk;
	clk = mhz * 10000;
	int32_t ret = cnxt_smc_set_clock_freq (specdev()->handle, clk);
	check_error("cnxt_smc_set_clock_freq", ret);
	#ifdef COOL
	call (Cool_FastReset(reader));
	#endif
	cs_debug_mask(D_DEVICE, "COOL: %s clock succesfully set to %i",reader->label, clk);
	return OK;
}

#if defined(COOL)
int32_t Cool_WriteSettings (struct s_reader *reader, uint32_t BWT, uint32_t CWT, uint32_t EGT, uint32_t BGT)
{
	//this code worked with old cnxt_lnx.ko, but prevented nagra cards from working with new cnxt_lnx.ko
/*	struct
	{
		unsigned short  CardActTime;   //card activation time (in clock cycles = 1/54Mhz)
		unsigned short  CardDeactTime; //card deactivation time (in clock cycles = 1/54Mhz)
		unsigned short  ATRSTime;			//ATR first char timeout in clock cycles (1/f)
		unsigned short  ATRDTime;			//ATR duration in ETU
		unsigned long	  BWT;
		unsigned long   CWT;
		unsigned char   EGT;
		unsigned char   BGT;
	} params;
	params.BWT = BWT;
	params.CWT = CWT;
	params.EGT = EGT;
	params.BGT = BGT;
	call (cnxt_smc_set_config_timeout(specdev()->handle, params));
	cs_debug_mask(D_DEVICE, "COOL WriteSettings OK");*/

	//set freq back to reader->mhz if necessary
	uint32_t clk;
	int32_t ret = cnxt_smc_get_clock_freq (specdev()->handle, &clk);
	check_error("cnxt_smc_get_clock_freq", ret);
	if (clk/10000 != reader->mhz) {
		cs_debug_mask(D_DEVICE,"COOL: %s clock freq: %i, scheduling change to %i", reader->label, clk, reader->mhz*10000);
		call (Cool_SetClockrate(reader, reader->mhz));
	} 

	return OK;
}
#elif defined(SU980)
int32_t Cool_WriteSettings (struct s_reader *reader, uint32_t BWT, uint32_t CWT, uint32_t CGT, uint32_t BGT, uint16_t F, uint8_t D)
{
#if 0 /*It works without setting timeout for conax card. Maybe needed by other smart card in the future*/
    struct
    {
       /* the card activation time in clock cycles (1/54MHz) */
       unsigned short  uCardActTime;
       /* the card deactivation time in clock cycles (1/54MHz) */
       unsigned short  uCardDeactTime;
       /* the max time in Smart Card clock cycles (1/f) for the */
       /* first character of ATR reception */
       unsigned short  uATRSTime;
       /* the max duration of ATR character stream in ETUs */
       unsigned short  uATRDTime;
       /* the max time for receipt of first character of data stream in ETUs */
       unsigned long  uBLKTime;
       /* the max time allowed between characters in ETUs */
       unsigned long  uCHTime;
       /* the extra guard time added between two consecutive characters in ETUs */
       unsigned char   uCHGuardTime;
       /* the min time between the leading edges of two characters sent */
       /* in opposite direction in ETUs */
       unsigned char   uBKGuardTime;
    }params;

    call(cnxt_smc_get_config_timeout(specdev()->handle, &params));
    cs_debug_mask(D_DEVICE, "Cool_WriteSettings(get):\
        uCardActTime=%d, uCardDeactTime=%d, uATRSTime=%d, uATRDTime=%d,\
        uBLKTime=%d, uCHTime=%d, uCHGuardTime=%d, uBKGuardTime=%d\n",\
        params.uCardDeactTime, params.uCardDeactTime, params.uATRSTime, params.uATRDTime, \
        params.uBLKTime, params.uCHTime, params.uCHGuardTime, params.uBKGuardTime);

    params.uBLKTime = BWT;
    params.uBKGuardTime = BGT;
    params.uCHTime = CWT;
    params.uCHGuardTime = CGT;
    cs_debug_mask(D_DEVICE, "Cool_WriteSettings(set):\
        uCardActTime=%d, uCardDeactTime=%d, uATRSTime=%d, uATRDTime=%d,\
        uBLKTime=%d, uCHTime=%d, uCHGuardTime=%d, uBKGuardTime=%d\n",\
        params.uCardDeactTime, params.uCardDeactTime, params.uATRSTime, params.uATRDTime, \
        params.uBLKTime, params.uCHTime, params.uCHGuardTime, params.uBKGuardTime);
    call(cnxt_smc_set_config_timeout(specdev()->handle, params));
#endif
	//irdeto(T14) can not change F,d  before pps unilateral, Not sure but test OK
	if(ATR_PROTOCOL_TYPE_T14 != reader->protocol_type)
	{
		cs_debug_mask(D_DEVICE,"Cool_WriteSettings F:%d, D:%d\n", F, D);
		call(cnxt_smc_set_F_D_factors(specdev()->handle, F, D));
	}
    //set freq back to reader->mhz if necessary
    uint32_t clk;
    int32_t ret = cnxt_smc_get_clock_freq (specdev()->handle, &clk);
    check_error("cnxt_smc_get_clock_freq", ret);
    if (clk/10000 != reader->mhz) {
        cs_debug_mask(D_DEVICE,"COOL: %s clock freq: %i, scheduling change to %i", reader->label, clk, reader->mhz*10000);
        call (Cool_SetClockrate(reader, reader->mhz));
    } 
    return OK;
}
#endif

int32_t Cool_FastReset (struct s_reader *reader)
{
	int32_t n = 40, ret;
	unsigned char buf[40];

	//reset card
	ret = cnxt_smc_reset_card (specdev()->handle, ATR_TIMEOUT, NULL, NULL);
	check_error("cnxt_smc_reset_card", ret);
	cs_sleepms(50);
	ret = cnxt_smc_get_atr (specdev()->handle, buf, &n);
	check_error("cnxt_smc_get_atr", ret);
    return 0;
}

int32_t Cool_FastReset_With_ATR (struct s_reader *reader, ATR * atr)
{
	int32_t n = 40, ret;
	unsigned char buf[40];

	//reset card
	ret = cnxt_smc_reset_card (specdev()->handle, ATR_TIMEOUT, NULL, NULL);
	check_error("cnxt_smc_reset_card", ret);
	cs_sleepms(50);
	ret = cnxt_smc_get_atr (specdev()->handle, buf, &n);
	check_error("cnxt_smc_get_atr", ret);

	call (!ATR_InitFromArray (atr, buf, n) == ATR_OK);
	{
		cs_sleepms(50);
		return OK;
	}
}

int32_t Cool_Close (struct s_reader *reader)
{
	if (cool_kal_opened) {
		int32_t ret = cnxt_smc_close (specdev()->handle);
		check_error("cnxt_smc_close", ret);
	}
	NULLFREE(reader->spec_dev);
	return OK;
}

#endif
