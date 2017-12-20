/* hello.c              (c) 2015 Freescale Semiconductor, Inc.
 * Descriptions: Minimal hello world example code with GPIO
 * 28 Sept 2015 Osvaldo Romero et al: Initial version
 */

#include <string.h>
#include "isotp/isotp.h"

#include "SKEAZ1284.h" /* include peripheral declarations SKEAZ128M4 */
#include "can.h"
#include "led.h"
#include "systimer.h"
#include "flash.h"
#include "flash_load.h"
#include "uds.h"
#include "script.h"

#ifndef CAN_ID
#    error "CAN_ID should be provided"
#endif

/* This variable is necessary for testing the flash loader*/
const uint32_t flash_start_address = 0x00000000;


uint32_t flash_load_startaddr = 0;
uint32_t flash_load_curaddr = 0;
uint32_t flash_load_size = 0;

int uds_in_download = 0;
int uds_in_programming = 0;

static uint32_t make_32(uint8_t hh, uint8_t hl, uint8_t lh, uint8_t ll) {
	return (hh << 24) | (hl << 16) | (lh << 8) | ll;
}
void main(void) {
  int i;
  uint8_t led_mask = 0x01;
  struct can_pack pack;
  struct can_filter can_filters[2];
  uint32_t session_ts;

  IsoTpShims isotp_shims;
  IsoTpReceiveHandle isotp_receive_handle;
  int isotp_in_progress = 0;

  time_init();
  led_init();
  flash_init();
  
  script_init();

  can_filters[0].filter = 0xffffffe0 | CAN_ID;
  can_filters[0].mask = 0xffffffe0;
  //can_filters[0].mask = 0xffffffff;
  can_filters[0].ext = 0;

  can_filters[1].filter = 0xffffffe0 | CAN_ID;
  can_filters[1].mask = 0xffffffe0;
  //can_filters[1].mask = 0xffffffff;
  can_filters[1].ext = 0;

  can_init(125000, can_filters);

  __enable_irq();

  for(i = 0; i < 4; i++)
	led_set(i, 1);

  time_delay(500);

  for(i = 0; i < 4; i++)
	led_set(i, 0);

  isotp_shims = isotp_init_shims(NULL, send_can_isotp, NULL, NULL);

  send_uds_init();
  for(;;) {
	if(uds_in_programming) {
		/* TODO: figure out real timeout (S3) */
		if(time_passed(session_ts) > 60000) {
			uds_in_download = 0;
			uds_in_programming = 0;
		}
	}
	
	//send_uds_positive_sessioncontrol(0x33);
	if(!uds_in_programming) {
		script_execute();
	}

	// execute script stored in a dedicated flash sector
	// process FDS messages
	if(can_recv(&pack)) {
		if(!isotp_in_progress) {
			isotp_receive_handle = isotp_receive(&isotp_shims, pack.af, NULL);
			isotp_in_progress = 1;
		}
		IsoTpMessage isotp_message = isotp_continue_receive(&isotp_shims, &isotp_receive_handle, pack.af, pack.data, pack.dlc);	
		if(isotp_message.completed && isotp_receive_handle.completed) {
			isotp_in_progress = 0 ;
			if(isotp_receive_handle.success) {
				uint32_t flash_addr;
				uint32_t flash_size;
				uint8_t addr_len;
				uint8_t size_len;
				uint8_t uds_seq_number;
				int res;
				/* Don't care about AF here, it should be filtered on CAN level */
				/* Switch over SID */
				switch (isotp_message.payload[0]) {
					case 0x10: /* DiagnosticSessionControl */
						if(isotp_message.size < 2) {
							send_uds_error(0x10, 0x13); /* Invalid format */
							break;
						}

						if(isotp_message.payload[1] ==  0x01) {
							uds_in_programming = false;
						} else if (isotp_message.payload[1] ==  0x02) {
							uds_in_programming = true;
							session_ts = time_get();
						} else {
							send_uds_error(0x10, 0x12); /* Subfunction not supported */
							break;
						}

						send_uds_positive_sessioncontrol(isotp_message.payload[1]);
						break;

					case 0x31: /* RoutineControl*/
						if(!uds_in_programming) {
							send_uds_error(0x31, 0x22); /* Conditions not correct */
							break;
						}

						session_ts = time_get();

						if(isotp_message.size < 4) {
							send_uds_error(0x31, 0x13); /* Invalid Format */
							break;
						}
						if(isotp_message.payload[1] != 0x01) { /* Start routine*/
							/* TODO: support other routine operations */
							send_uds_error(0x31, 0x12); /* SFNS */
							break;
						}
						if(isotp_message.payload[2] != 0xff &&isotp_message.payload[3] != 0x00) {
							send_uds_error(0x31, 0x12); /* SFNS */
							break;
						}
						if(isotp_message.size != 12) {
							send_uds_error(0x31, 0x13); /* Invalid Format */
							break;
						}
						flash_addr = make_32(isotp_message.payload[4], isotp_message.payload[5], isotp_message.payload[6], isotp_message.payload[7]);
						flash_size = make_32(isotp_message.payload[8], isotp_message.payload[9], isotp_message.payload[10], isotp_message.payload[11]);

						if((flash_addr < PROGRAM_FLASH_BEGIN) ||
								(flash_addr + flash_size) >= PROGRAM_FLASH_END ||
								(flash_addr + flash_size) < flash_addr) { /*overflow*/
							send_uds_error(0x31, 0x31); /* ROOR */
							break;
						}
						res = flash_load_erase(flash_addr, flash_size);
						if(res)
							send_uds_error(0x31, 0x10); /* General Error */
						else
							send_uds_positive_routinecontrol(isotp_message.payload[1], (isotp_message.payload[2] << 8) | isotp_message.payload[3]);
						break;
					case 0x11: /* ECUReset */
						if(isotp_message.size < 2) {
							send_uds_error(0x11, 0x13); /* Invalid format */
							break;
						}

						if(isotp_message.payload[1] != 0x01) { /* Only hard reset is supported */
							send_uds_error(0x11, 0x12); /* Subfunction not supported */
							break;
						}
						send_uds_positive_ecureset(isotp_message.payload[1]);
						can_flush_send();
						NVIC_SystemReset();
						break;
						
					case 0x34: /* Request download */
						if(!uds_in_programming) {
							send_uds_error(0x34, 0x70); /* Not accepted */
							break;
						}

						session_ts = time_get();

						if(isotp_message.size < 3) {
							send_uds_error(0x34, 0x13); /* Invalid Format */
							break;
						}
						if(isotp_message.payload[1] != 0x00) { /* Only support raw data */
							send_uds_error(0x34, 0x31); /* ROOR */
							break;
						}
						addr_len = isotp_message.payload[2] & 0x0F;
						size_len = (isotp_message.payload[2] >> 4) & 0x0F;
						if(addr_len == 0 || addr_len > 4 || size_len == 0 || size_len > 4)  {
							send_uds_error(0x34, 0x31); /* ROOR */
							break;
						}

						if(isotp_message.size < 3+addr_len+size_len) {
							send_uds_error(0x34, 0x13); /* Invalid Format */
							break;
						}

						flash_addr = 0;
						for(i = 3; i < addr_len+3; i++) {
							flash_addr <<= 8;
							flash_addr |=isotp_message.payload[i];
						}
						flash_size = 0;
						for(;i < addr_len+size_len+3; i++) {
							flash_size <<= 8;
							flash_size |=isotp_message.payload[i];
						}

						if((flash_addr < PROGRAM_FLASH_BEGIN) ||
								(flash_addr + flash_size) >= PROGRAM_FLASH_END ||
								(flash_addr + flash_size) < flash_addr) { /*overflow*/
							send_uds_error(0x34, 0x31); /* ROOR */
							break;
						}

						flash_load_prepare(flash_addr, flash_size);
						uds_seq_number = 0x00;
						uds_in_download = 1;
						flash_load_startaddr = flash_addr;
						flash_load_curaddr = flash_addr;
						flash_load_size = flash_size;
						send_uds_positive_reqdownload(UDS_MAX_BLOCK);
						break;
					case 0x36: /* TransferData */
						session_ts = time_get();

						if(!uds_in_download) {
							send_uds_error(0x36, 0x24); /* Sequence Error */
							break;
						}
						if(isotp_message.size < 2) {
							send_uds_error(0x36, 0x13); /* Invalid Format */
							break;
						}
						if(isotp_message.payload[1] == uds_seq_number) { /* Repeated segment, acknowledge and ignore*/
							send_uds_positive_transferdata(uds_seq_number);
							break;
						} else if(isotp_message.payload[1] != (uint8_t) (uds_seq_number+1)) {
							send_uds_error(0x36, 0x24); /* Sequence Error */
							break;
						}

						if(flash_load_curaddr +isotp_message.size - 2 > flash_load_startaddr+flash_load_size) {
							send_uds_error(0x36, 0x31); /* ROOR */
							break;
						}
						flash_load_continue(isotp_message.payload+2,isotp_message.size-2);
						uds_seq_number = isotp_message.payload[1];
						send_uds_positive_transferdata(uds_seq_number);
						break;

					case 0x37: /* RequestTransferExit */
						session_ts = time_get();

						if(!uds_in_download) {
							send_uds_error(0x37, 0x24); /* Sequence Error */
							break;
						}
						flash_load_finalize();
						uds_in_download = 0;
						send_uds_positive_transferexit();
						break;

					case 0x22: /* ReadDataByIdentifier */
						if(isotp_message.size < 3) {
							send_uds_error(0x22, 0x13); /* Invalid Format */
							break;
						}
						
						/* Only support one piece of data per message */
						if(isotp_message.size > 3) {
							send_uds_error(0x22, 0x14); /* Response too long */
							break;
						}

						switch((isotp_message.payload[1] << 8) | (isotp_message.payload[2])) {
							case HW_ID_DID:
								send_uds_positive_readdata(HW_ID_DID, (const uint8_t* )UPTANE_HARDWARE_ID, strlen(UPTANE_HARDWARE_ID));
								break;
							case ECU_SERIAL_DID:
								send_uds_positive_readdata(ECU_SERIAL_DID, (const uint8_t*) UPTANE_ECU_SERIAL, strlen(UPTANE_ECU_SERIAL));
								break;
							default:
								send_uds_error(0x22, 0x31); /* ROOR */
								break;
						}
						break;
					default:
						send_uds_error(isotp_message.payload[0], 0x11); /* Service not supported */
						break;

				}
			} else {
			}
		}
	}
  }
}
