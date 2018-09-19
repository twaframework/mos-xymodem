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

#include "mgos_xymodem.h"

// Uncomment this and the debug will include a hex dump of each packet sent
//#define MGOS_XYMODEM_DEBUG_HEXDUMP

void mgos_xymodem_init()
{
	mgos_xymodem_config.uart_no = 0;

	mgos_event_register_base(MGOS_XYMODEM_EVENT_BASE, "xymodem");

	mgos_event_add_handler(MGOS_XYMODEM_SEND_PACKET, mgos_xymodem_on_send_packet, NULL);
	mgos_event_add_handler(MGOS_XYMODEM_FINISH, mgos_xymodem_on_finish, NULL);
}

mgos_xymodem_packet *mgos_xymodem_create_packet(uint8_t type)
{
	mgos_xymodem_packet *retval;

	LOG(LL_DEBUG, ("Creating new data packet of type 0x%02x", type));

	retval = malloc(sizeof(mgos_xymodem_packet));

	if(type == MGOS_XYMODEM_SOH) {
		retval->payload = malloc(sizeof(uint8_t) * 128);
		memset(retval->payload, 0x0, sizeof(uint8_t) * 128);
	} else if(type == MGOS_XYMODEM_STX) {
		retval->payload = malloc(sizeof(uint8_t) * 1024);
		memset(retval->payload, 0x0, sizeof(uint8_t) * 1024);
	} else {
		LOG(LL_ERROR, ("Cannot create invalid packet type: %02x", type));
		free(retval);
		return NULL;
	}

	retval->type = type;
	retval->retries = 0;
	retval->fp = NULL;
	retval->file_size = 0;
	retval->number = 0;
	retval->bytes_sent = 0;
	retval->is_final = false;

	return retval;
}

uint8_t mgos_xymodem_read_byte()
{
	uint8_t tByte;
	uint16_t timeout = 30000;

	do {
		mgos_msleep(1000);
		timeout -= 1000;
		LOG(LL_DEBUG, ("Attempting to read a byte from UART #%d (timeout: %d)", MGOS_XYMODEM_UART_NO, timeout));
	} while((mgos_uart_read(MGOS_XYMODEM_UART_NO, &tByte, 1) <= 0) && (timeout > 0));

	if(timeout <= 0) {
		LOG(LL_INFO, ("Failed to read a byte from UART"));
		return 0x0;
	}

	if(tByte == 0x0) {
		return mgos_xymodem_read_byte();
	}

	return tByte;
}

bool mgos_xymodem_transmit_impl(uint8_t param_count, ...)
{
	FILE *fp;
	va_list v;
	char *filename;

	va_start(v, param_count);

	LOG(LL_DEBUG, ("Beginning File Transfer"));

	int uart_no = va_arg(v, int);

	if(uart_no > 3) {
		LOG(LL_ERROR, ("Invalid UART Number for File Transfer: %d", uart_no));
		return false;
	}

	mgos_xymodem_config.uart_no = uart_no;

	mgos_uart_set_dispatcher(mgos_xymodem_config.uart_no, NULL, NULL);
	mgos_uart_set_rx_enabled(mgos_xymodem_config.uart_no, true);

	switch(param_count) {
		case 2:

			LOG(LL_DEBUG, ("Using XModem Protocol"));

			fp = va_arg(v, FILE *);

			if(!mgos_xymodem_transmit_xmodem(fp)) {
				LOG(LL_ERROR, ("Could not start XMODEM transfer"));
				va_end(v);
				return false;
			}
			va_end(v);

			return true;
		case 3:

			LOG(LL_DEBUG, ("Using YModem Protocol"));

			fp = va_arg(v, FILE *);
			filename = va_arg(v, char *);

			if(!mgos_xymodem_transmit_ymodem(fp, filename)) {
				LOG(LL_ERROR, ("Could not start YMODEM transfer"));
				va_end(v);
				return false;
			}
			va_end(v);

			return true;
	}

	va_end(v);
	LOG(LL_ERROR, ("Invalid arguments to function 'mgos_xymodem_transmit()'"));
	return false;
}

bool mgos_xymodem_determine_crc(mgos_xymodem_packet *packet)
{
	uint8_t tByte;

	LOG(LL_INFO, ("Awaiting destination CRC preference.."));

	tByte = mgos_xymodem_read_byte();
	switch(tByte) {
		case MGOS_XYMODEM_NAK:
			LOG(LL_DEBUG, ("Using Checksum for data verification"));
			packet->crc_type = MGOS_XYMODEM_CHECKSUM;
			return true;
		case MGOS_XYMODEM_CRC16:
			LOG(LL_DEBUG, ("Using CRC16 for data verification"));
			packet->crc_type = MGOS_XYMODEM_CRC_16;
			return true;
	}

	LOG(LL_ERROR, ("Could not determine destination CRC preference, received 0x%02x", tByte));

	return false;
}

bool mgos_xymodem_transmit_ymodem(FILE *fp, char *filename)
{
	mgos_xymodem_packet *packet;
	char str_file_size[64] = "";

	packet = mgos_xymodem_create_packet(MGOS_XYMODEM_STX);

	if(!mgos_xymodem_determine_crc(packet)) {
		MGOS_XYMODEM_FREE_PACKET(packet);
		MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_FAILED, NULL);
		return false;
	}

	packet->protocol = MGOS_XYMODEM_PROTOCOL_YMODEM;
	packet->fp = fp;
	fseek(packet->fp, 0, SEEK_END);

	packet->file_size = ftell(fp);

	if(packet->file_size <= 0) {
		LOG(LL_ERROR, ("Invalid File pointer - could not determine file size or empty file"));
		MGOS_XYMODEM_FREE_PACKET(packet);
		return false;
	}

	fseek(packet->fp, 0, SEEK_SET);

	packet->number = 0;

	// @todo can we refactor this to us %s\0%zu
	c_snprintf(str_file_size, sizeof(str_file_size), "%zu", packet->file_size);

	memcpy(packet->payload, filename, strlen(filename));
	memcpy(packet->payload + (strlen(filename) + 1), &str_file_size, strlen(str_file_size));

	LOG(LL_DEBUG, ("Created header packet (filename: %s, filesize: %zu", filename, packet->file_size));

	MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_SEND_PACKET, packet);

	return true;
}

bool mgos_xymodem_transmit_xmodem(FILE *fp)
{
	return false;
}

void mgos_xymodem_event_trigger_cb(void *event_params)
{
	mgos_xymodem_event_params *params = (mgos_xymodem_event_params *)event_params;
	mgos_event_trigger(params->event, params->data);
	free(params);
}

void mgos_xymodem_on_finish(int ev, void *packet_data, void *unused)
{
	uint8_t eot = MGOS_XYMODEM_EOT;
	uint8_t tByte, tries = 0;
	mgos_xymodem_packet *packet = (mgos_xymodem_packet *)packet_data;
	mgos_xymodem_packet *final_packet = NULL;

	LOG(LL_DEBUG, ("Entering tranmission finish event on packet #%d", packet->number));

	do {
		mgos_uart_write(MGOS_XYMODEM_UART_NO, &eot, 1);
		mgos_uart_flush(MGOS_XYMODEM_UART_NO);
		tByte = mgos_xymodem_read_byte();
		tries++;
	} while((tByte != MGOS_XYMODEM_ACK) && (tries < 5));

	if(tByte != MGOS_XYMODEM_ACK) {
		LOG(LL_ERROR, ("Failed to receive an ACK of EOT, received 0x%02x instead", tByte));
		MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_FAILED, NULL);
		return;
	}

	if(packet->protocol == MGOS_XYMODEM_PROTOCOL_YMODEM) {

		final_packet = mgos_xymodem_create_packet(MGOS_XYMODEM_STX);
		final_packet->number = 0;
		final_packet->is_final = true;

		if(!mgos_xymodem_determine_crc(final_packet)) {
			MGOS_XYMODEM_FREE_PACKET(final_packet);
			MGOS_XYMODEM_FREE_PACKET(packet);
			MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_FAILED, NULL);
			return;
		}

		LOG(LL_DEBUG, ("Creating final YModem packet with null filename to end transmission"));

		MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_SEND_PACKET, final_packet);

	} else {
		LOG(LL_INFO, ("Transmission Complete!"));
		MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_COMPLETE, NULL);
	}

	MGOS_XYMODEM_FREE_PACKET(packet);
}

void mgos_xymodem_on_send_packet(int ev, void *packet_data, void *unused)
{
	mgos_xymodem_packet *packet = (mgos_xymodem_packet *)packet_data;
	mgos_xymodem_packet *next_packet = NULL;
	uint8_t *uart_packet;
	uint8_t tByte;
	uint16_t crc;
	size_t uart_packet_len, wrote_len, read_len;

	LOG(LL_DEBUG, ("Entered Send Packet Event"));

	if(packet->retries > MGOS_XYMODEM_PACKET_RETRY) {
		LOG(LL_ERROR, ("Attempt to send packet #%d failed %d times, aborting", packet->number, MGOS_XYMODEM_PACKET_RETRY));
		MGOS_XYMODEM_FREE_PACKET(packet);
		MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_FAILED, NULL);
		return;
	}

	if(packet->crc_type = MGOS_XYMODEM_CRC16) {
		uart_packet_len = MGOS_XYMODEM_PAYLOAD_SIZE(packet) + 5;
	} else if(packet->crc_type = MGOS_XYMODEM_CHECKSUM) {
		uart_packet_len = MGOS_XYMODEM_PAYLOAD_SIZE(packet) + 4;
	}

	LOG(LL_DEBUG, ("Setting UART packet size to %zu based on a payload size of %zu and data integrity check", uart_packet_len, MGOS_XYMODEM_PAYLOAD_SIZE(packet)));

	uart_packet = malloc(sizeof(uint8_t) * uart_packet_len);

	if(mgos_uart_read_avail(MGOS_XYMODEM_UART_NO) > 0) {
		LOG(LL_DEBUG, ("Clearing out UART read buffer"));
		while(mgos_uart_read(MGOS_XYMODEM_UART_NO, &tByte, 1) > 0);
	}

	memset(uart_packet, 0x0, uart_packet_len);

	uart_packet[0] = packet->type;
	uart_packet[1] = packet->number;
	uart_packet[2] = ~packet->number;

	memcpy(uart_packet + (sizeof(uint8_t) * 3), packet->payload, MGOS_XYMODEM_PAYLOAD_SIZE(packet));

	if(packet->crc_type == MGOS_XYMODEM_CRC16) {

		crc = mgos_xymodem_crc16(uart_packet + (sizeof(uint8_t) * 3),
								0,
								MGOS_XYMODEM_PAYLOAD_SIZE(packet));

		uart_packet[uart_packet_len - 2] = (uint8_t)(crc >> 8) & 0xFF;
		uart_packet[uart_packet_len - 1] = (uint8_t)(crc & 0xFF);
	} else {
		uart_packet[uart_packet_len - 1] = mgos_xymodem_calc_checksum(uart_packet + (sizeof(uint8_t) * 3),
												MGOS_XYMODEM_PAYLOAD_SIZE(packet)
										   );
	}

	mgos_xymodem_hex_dump("UART Packet", uart_packet, uart_packet_len);

	wrote_len = mgos_uart_write(MGOS_XYMODEM_UART_NO, uart_packet, uart_packet_len);

	mgos_uart_flush(MGOS_XYMODEM_UART_NO);

	LOG(LL_DEBUG, ("Wrote UART Packet for packet #%d", packet->number));

	free(uart_packet);

	if(wrote_len != uart_packet_len) {
		LOG(LL_ERROR, ("Error writing packet to UART, wrote %d byte(s) instead of %d byte(s)", wrote_len, uart_packet_len));
		MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_FAILED, NULL);
		return;
	}

	tByte = mgos_xymodem_read_byte();

	switch(tByte) {
		case MGOS_XYMODEM_ACK:

			LOG(LL_DEBUG, ("Received ACK of packet #%d", packet->number));

			if(packet->is_final) {
				LOG(LL_DEBUG, ("Packet was marked as final packet, we're done!"));
				MGOS_XYMODEM_FREE_PACKET(packet);
				MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_COMPLETE, NULL);
				return;
			}

			if(feof(packet->fp)) {
				LOG(LL_DEBUG, ("Packet file reference is now empty, wrapping up"));
				MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_FINISH, packet);
				return;
			}

			LOG(LL_DEBUG, ("Creating next packet"));

			next_packet = mgos_xymodem_create_packet(packet->type);
			next_packet->bytes_sent = packet->bytes_sent + MGOS_XYMODEM_PAYLOAD_SIZE(packet);
			next_packet->number = packet->number + 1;
			next_packet->fp = packet->fp;
			next_packet->type = packet->type;
			next_packet->file_size = packet->file_size;
			next_packet->protocol = packet->protocol;
			next_packet->crc_type = packet->crc_type;

			read_len = fread(next_packet->payload, MGOS_XYMODEM_PAYLOAD_SIZE(packet), 1, next_packet->fp);

			// Technically this is inaccurate, because whatever is < the payload size
			// hasn't actually been "sent" yet, but for all intents and purposes
			// it shouldn't be a problem to be off at the very end by < 1024 bytes

			if(read_len != MGOS_XYMODEM_PAYLOAD_SIZE(next_packet)) {
				next_packet->bytes_sent += read_len;
			}

			MGOS_XYMODEM_FREE_PACKET(packet);
			MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_SEND_PACKET, next_packet);
			return;

		case MGOS_XYMODEM_NAK:

			LOG(LL_DEBUG, ("Received NAK for Packet #%d, retrying", packet->number));

			packet->retries++;
			MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_SEND_PACKET, packet);
			return;

		case MGOS_XYMODEM_CAN:

			LOG(LL_DEBUG, ("Received CAN for Packet #%d, confirming..", packet->number));
			tByte = mgos_xymodem_read_byte();

			if(tByte == MGOS_XYMODEM_CAN) {
				LOG(LL_INFO, ("Transfer cancelled by destination"));
				MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_FAILED, NULL);
				return;
			}

			LOG(LL_DEBUG, ("Confirmation of CAN failed, retrying packet #%d", packet->number));
			packet->retries++;
			MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_SEND_PACKET, packet);
			return;

		default:
			LOG(LL_DEBUG, ("Unknown response to packet #%d (0x%02x), retrying", packet->number, tByte));
			packet->retries++;
			MGOS_XYMODEM_TRIGGER_EVENT(MGOS_XYMODEM_SEND_PACKET, packet);
			return;
	}
}
