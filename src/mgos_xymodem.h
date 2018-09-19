/*
  +----------------------------------------------------------------------+
  | Mongoose XYModem                                                     |
  +----------------------------------------------------------------------+
  | Copyright (c) 2018 John Coggeshall                                   |
  +----------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0 (the "License");      |
  | you may not use this file except in compliance with the License. You |
  | may obtain a copy of the License at:                                 |
  |                                                                      |
  | http://www.apache.org/licenses/LICENSE-2.0                           |
  |                                                                      |
  | Unless required by applicable law or agreed to in writing, software  |
  | distributed under the License is distributed on an "AS IS" BASIS,    |
  | WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or      |
  | implied. See the License for the specific language governing         |
  | permissions and limitations under the License.                       |
  +----------------------------------------------------------------------+
  | Authors: John Coggeshall <john@thissmarthouse.com>                   |
  +----------------------------------------------------------------------+
*/

#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include "mgos.h"
#include "mgos_event.h"

#define MGOS_XYMODEM_SOH 			0x01
#define MGOS_XYMODEM_STX 			0x02
#define MGOS_XYMODEM_EOT 			0x04
#define MGOS_XYMODEM_ACK 			0x06
#define MGOS_XYMODEM_NAK 			0x15
#define MGOS_XYMODEM_CAN  			0x18
#define MGOS_XYMODEM_CRC16 			0x43

#define MGOS_XYMODEM_ABORT			0x41
#define MGOS_XYMODEM_ABORT_ALT 		0x61

#define MGOS_XYMODEM_EVENT_BASE MGOS_EVENT_BASE('X', 'Y', 'M')

#define MGOS_XYMODEM_PACKET_RETRY	5

enum mgos_xymodem_crc_type {
	MGOS_XYMODEM_CHECKSUM,
	MGOS_XYMODEM_CRC_16
};

enum mgos_xymodem_events {
	MGOS_XYMODEM_SEND_PACKET = MGOS_XYMODEM_EVENT_BASE,
	MGOS_XYMODEM_READ_FILE,
	MGOS_XYMODEM_FAILED,
	MGOS_XYMODEM_COMPLETE,
	MGOS_XYMODEM_FINISH
};

enum mgos_xymodem_protocol {
	MGOS_XYMODEM_PROTOCOL_XMODEM = 1,
	MGOS_XYMODEM_PROTOCOL_YMODEM = 2,
	MGOS_XYMODEM_PROTOCOL_UNKNOWN = 3
};

struct mgos_xymodem_config_t {
	uint8_t uart_no;
};

typedef struct mgos_xymodem_event_params_t {
	int event;
	void *data;
} mgos_xymodem_event_params;

typedef struct mgos_xymodem_packet_t {
	uint8_t *payload;
	uint8_t type;
	uint8_t retries;
	FILE *fp;
	size_t file_size;
	size_t bytes_sent;
	uint8_t number;
	bool is_final;
	enum mgos_xymodem_protocol protocol;
	enum mgos_xymodem_crc_type crc_type;
} mgos_xymodem_packet;


struct mgos_xymodem_config_t mgos_xymodem_config;

#define ELEVENTH_ARGUMENT(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, ...) a11
#define COUNT_ARGUMENTS(...) ELEVENTH_ARGUMENT(dummy, ## __VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

void mgos_xymodem_init();

bool mgos_xymodem_transmit_impl(uint8_t, ...);
#define mgos_xymodem_transmit(...) \
		mgos_xymodem_transmit_impl( COUNT_ARGUMENTS(__VA_ARGS__), __VA_ARGS__)

bool mgos_xymodem_transmit_ymodem(FILE *, char *);
bool mgos_xymodem_transmit_xmodem(FILE *);

unsigned int mgos_xymodem_calc_crc(uint8_t *, uint8_t, uint16_t, uint8_t, uint8_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t);
uint8_t mgos_xymodem_crc_reflect(uint8_t, uint8_t);
uint8_t mgos_xymodem_calc_checksum(uint8_t *, uint16_t);
#define mgos_xymodem_crc16(data, start, len) mgos_xymodem_calc_crc(data, start, len, false, false, 0x1021, 0x0000, 0x0000, 0x8000, 0xffff);

void mgos_xymodem_event_trigger_cb(void *);
void mgos_xymodem_on_send_packet(int, void *, void *);
void mgos_xymodem_on_finish(int, void *, void *);

void mgos_xymodem_hex_dump(char *, void *, int);
uint8_t mgos_xymodem_read_byte();
bool mgos_xymodem_determine_crc(mgos_xymodem_packet *);

mgos_xymodem_packet *mgos_xymodem_create_packet(uint8_t);

#define MGOS_XYMODEM_FREE_PACKET(packet) \
	if((packet) != NULL) {	\
		if((packet)->payload != NULL) free((packet)->payload); \
		free((packet));	\
	}

#define MGOS_XYMODEM_UART_NO (mgos_xymodem_config.uart_no)

#define MGOS_XYMODEM_PAYLOAD_SIZE(packet) \
	(((packet)->type == MGOS_XYMODEM_SOH) ? 128 : 1024)

#define MGOS_XYMODEM_TRIGGER_EVENT(e, d) \
	do { \
		mgos_xymodem_event_params *p; \
		p = malloc(sizeof(mgos_xymodem_event_params)); \
		p->event = e; \
		p->data = d; \
		mgos_set_timer(1, MGOS_TIMER_RUN_NOW, mgos_xymodem_event_trigger_cb, p); \
	} while(false);
