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
#include "mgos.h"

#define MGOS_XYMODEM_SOH 			0x01
#define MGOS_XYMODEM_STX 			0x02
#define MGOS_XYMODEM_EOT 			0x04
#define MGOS_XYMODEM_ACK 			0x06
#define MGOS_XYMODEM_NAK 			0x15
#define MGOS_XYMODEM_CAN  			0x18
#define MGOS_XYMODEM_CRC16 			0x43

#define MGOS_XYMODEM_ABORT			0x41
#define MGOS_XYMODEM_ABORT_ALT 		0x61

enum mgos_xymodem_protocol_t {
	MGOS_XYMODEM_PROTOCOL_XMODEM = 1,
	MGOS_XYMODEM_PROTOCOL_YMODEM = 2,
	MGOS_XYMODEM_PROTOCOL_UNKNOWN = 3
};

enum mgos_xymodem_checksum_t {
	MGOS_XYMODEM_CHECKSUM_OLD = 1,
	MGOS_XYMODEM_CHECKSUM_CRC_16 = 2,
	MGOS_XYMODEM_CHECKSUM_NONE = 3
};

struct mgos_xymodem_state_t {
	enum mgos_xymodem_protocol_t protocol;
	enum mgos_xymodem_checksum_t checksum_type;
	uint8_t packet_number;
	int uart_no;
} mgos_xymodem_state;

void mgos_xymodem_set_uart_no(int uartno);
bool mgos_xymodem_transmit(FILE *fp, char *filename, size_t fileSize);
uint8_t mgos_xymodem_read_uart_byte();
bool mgos_xymodem_init(void);
void mgos_xymodem_set_protocol(enum mgos_xymodem_protocol_t protocol);
enum mgos_xymodem_checksum_t mgos_xymodem_determine_checksum();
void mgos_xymodem_set_crc_type(enum mgos_xymodem_checksum_t crcType);
unsigned int mgos_xymodem_calc_crc(uint8_t data[], uint8_t start, uint16_t length, uint8_t reflectIn, uint8_t reflectOut, uint16_t polynomial, uint16_t xorIn, uint16_t xorOut, uint16_t msbMask, uint16_t mask);
uint8_t mgos_xymodem_crc_reflect(uint8_t data, uint8_t bits);
void mgos_xymodem_hex_dump(char *desc, void *addr, int len);
bool mgos_xymodem_end_transmission();
uint8_t mgos_xymodem_send_payload(uint8_t payload[], size_t length, uint8_t packetType);


#define mgos_xymodem_xmodem_crc(data, start, len) mgos_xymodem_calc_crc(data, start, len, false, false, 0x1021, 0x0000, 0x0000, 0x8000, 0xffff);
