#include "uds.h"
#include "isotp/isotp.h"
#include "can.h"
#include "systimer.h"

#include <string.h>

#define UDS_CLIENT_CANADDR 0x01

static IsoTpShims shims;

static uint8_t payload[OUR_MAX_ISO_TP_MESSAGE_SIZE];

int send_can_isotp(uint32_t arbitration_id, const uint8_t* data, uint8_t size, void* private_data) {
	(void) private_data;
        struct can_pack pack;

        if(size > 8)
                return 0;
        pack.af = arbitration_id;
        pack.dlc = size;
        memcpy(pack.data, data, size);
        can_send(&pack);
        return 1;
}

void send_uds_init() {
	shims = isotp_init_shims(NULL, send_can_isotp, NULL, NULL);
}

int send_uds_error(uint8_t sid, uint8_t nrc) {
	payload[0] = 0x7F | 0x40; /* Error SID */
	payload[1] = sid;
	payload[2] = nrc;

	IsoTpMessage message = isotp_new_send_message((CAN_ID << 5) | UDS_CLIENT_CANADDR, payload, 3);
	IsoTpSendHandle handle = isotp_send(&shims, &message, NULL);

	return (handle.completed && handle.success);
}

int send_uds_positive_routinecontrol(uint8_t op, uint16_t id) {
	payload[0] = 0x31 | 0x40; /* RoutineControl */
	payload[1] = op;
	payload[2] = id >> 8;
	payload[3] = id & 0xFF;

	IsoTpMessage message = isotp_new_send_message((CAN_ID << 5) | UDS_CLIENT_CANADDR, payload, 4);
	IsoTpSendHandle handle = isotp_send(&shims, &message, NULL);

	return (handle.completed && handle.success);
}

int send_uds_positive_sessioncontrol(uint8_t type) {
	payload[0] = 0x10 | 0x40; /* SessionControl */
	payload[1] = type;

	/* TODO: figure out actual timeouts we can support */
	payload[2] = 0xFF;
	payload[3] = 0xFF;
	payload[4] = 0xFF;
	payload[5] = 0xFF;


	IsoTpMessage message = isotp_new_send_message((CAN_ID << 5) | UDS_CLIENT_CANADDR, payload, 6);
	IsoTpSendHandle handle = isotp_send(&shims, &message, NULL);

	return (handle.completed && handle.success);
}


int send_uds_positive_ecureset(uint8_t rtype) {
	payload[0] = 0x11 | 0x40; /* EcuReset */
	payload[1] = rtype;
	payload[2] = 0x00; /* Reset immediately*/

	IsoTpMessage message = isotp_new_send_message((CAN_ID << 5) | UDS_CLIENT_CANADDR, payload, 3);
	IsoTpSendHandle handle = isotp_send(&shims, &message, NULL);

	return (handle.completed && handle.success);
}

int send_uds_positive_reqdownload(uint16_t maxblock) {
	payload[0] = 0x34 | 0x40; /* RequestDownload */
	payload[1] = 0x20; /* 2 bytes for maximum block size */
	payload[2] = maxblock >> 8;
	payload[3] = maxblock & 0xFF;

	IsoTpMessage message = isotp_new_send_message((CAN_ID << 5) | UDS_CLIENT_CANADDR, payload, 4);
	IsoTpSendHandle handle = isotp_send(&shims, &message, NULL);

	return (handle.completed && handle.success);
}

int send_uds_positive_transferdata(uint8_t seqn) {
	payload[0] = 0x36 | 0x40; /* TransferData */
	payload[1] = seqn;

	IsoTpMessage message = isotp_new_send_message((CAN_ID << 5) | UDS_CLIENT_CANADDR, payload, 2);
	IsoTpSendHandle handle = isotp_send(&shims, &message, NULL);

	return (handle.completed && handle.success);
}

int send_uds_positive_transferexit() {
	payload[0] = 0x37 | 0x40; /* RequestTransferExit */

	IsoTpMessage message = isotp_new_send_message((CAN_ID << 5) | UDS_CLIENT_CANADDR, payload, 1);
	IsoTpSendHandle handle = isotp_send(&shims, &message, NULL);

	return (handle.completed && handle.success);
}

int send_uds_positive_readdata(uint16_t did, const uint8_t* data, uint16_t size) {
	struct can_pack cf_pack;
	if(size+3 > OUR_MAX_ISO_TP_MESSAGE_SIZE)
		return 0;

	payload[0] = 0x22 | 0x40; /* ReadDataByIdentifier */
	payload[1] = did >> 8;
	payload[2] = did & 0xFF;
	memcpy(payload+3, data, size);

	/* TODO: send in non-blocking fashion */
	IsoTpMessage message = isotp_new_send_message((CAN_ID << 5) | UDS_CLIENT_CANADDR, payload, 3+size);
	IsoTpSendHandle handle = isotp_send(&shims, &message, NULL);
	while(!handle.completed) {

		while(!can_recv(&cf_pack));
		if(!isotp_receive_flowcontrol(&shims, &handle, cf_pack.af, cf_pack.data, cf_pack.dlc))
			return false;

		while(handle.to_send != 0) {
			if(!isotp_continue_send(&shims, &handle))
				return false;
			if(handle.completed)
				break;
			time_delay(10);
		}

	}

	return (handle.success);
}

