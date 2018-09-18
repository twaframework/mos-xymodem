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
#include "mgos_uart_hal.h"

bool mgos_xymodem_init(void)
{
	mgos_xymodem_set_protocol(MGOS_XYMODEM_PROTOCOL_YMODEM);
	mgos_xymodem_set_crc_type(MGOS_XYMODEM_CHECKSUM_CRC_16);
	mgos_xymodem_set_uart_no(0);

#ifdef MGOS_XYMODEM_DEBUG
	LOG(LL_INFO, ("Hello from XYMODEM"));
#endif
	return true;
}

uint8_t mgos_xymodem_read_uart_byte()
{
	uint8_t inByte;
	uint16_t timeout = 5000;

	do {
		mgos_msleep(1000);
		timeout -= 100;

#ifdef MGOS_XYMODEM_DEBUG
		LOG(LL_INFO, ("Attempting to read a byte from UART %d (timeout in %d)", mgos_xymodem_state.uart_no, timeout));
#endif

	} while((mgos_uart_read(mgos_xymodem_state.uart_no, &inByte, 1) <= 0) && (timeout > 0));

	if(timeout <= 0) {
		return 0;
	}

	return inByte;
}

enum mgos_xymodem_checksum_t mgos_xymodem_determine_checksum()
{
	switch(mgos_xymodem_read_uart_byte()) {
		case MGOS_XYMODEM_CRC16:
			mgos_xymodem_state.checksum_type = MGOS_XYMODEM_CHECKSUM_CRC_16;
			break;
		case MGOS_XYMODEM_NAK:
			mgos_xymodem_state.checksum_type = MGOS_XYMODEM_CHECKSUM_OLD;
			break;
		case 0:
		default:
			mgos_xymodem_state.checksum_type = MGOS_XYMODEM_CHECKSUM_NONE;
			break;
	}

	return mgos_xymodem_state.checksum_type;
}

void mgos_xymodem_flush_read_buffer()
{
	uint8_t inByte;

	while(mgos_uart_read(mgos_xymodem_state.uart_no, &inByte, 1) > 0);
}

bool mgos_xymodem_end_transmission()
{
	uint8_t eot = MGOS_XYMODEM_EOT;
	uint8_t responseByte;
	uint8_t retries = 0;
	uint8_t payload = 0x0;

#ifdef MGOS_XYMODEM_DEBUG
	LOG(LL_INFO, ("Ending Transmission"));
#endif

	do {
		mgos_uart_write(mgos_xymodem_state.uart_no, &eot, 1);
		responseByte = mgos_xymodem_read_uart_byte();
		retries++;
	} while((responseByte != MGOS_XYMODEM_ACK) && (retries <= 10));

	if(responseByte != MGOS_XYMODEM_ACK) {

#ifdef MGOS_XYMODEM_DEBUG
		LOG(LL_INFO, ("Failed to receive an ACK for our EOT"));
#endif

		return false;
	}

#ifdef MGOS_XYMODEM_DEBUG
	LOG(LL_INFO, ("Received ACK for EOT"));
#endif

	if(mgos_xymodem_state.protocol == MGOS_XYMODEM_PROTOCOL_YMODEM) {
		// Eat up the next checksum announcment because YModem expects another file to transfer
		mgos_xymodem_determine_checksum();

		// Send a NULL filename
		mgos_xymodem_state.packet_number = 0;
		if(mgos_xymodem_send_payload(&payload, sizeof(payload), MGOS_XYMODEM_SOH) != MGOS_XYMODEM_ACK) {
			LOG(LL_INFO, ("Failed to close YMODEM Session"));
			return false;
		}

	}

	return true;
}

void mgos_xymodem_hex_dump(char *desc, void *addr, int len)
{

#ifdef MGOS_XYMODEM_DEBUG
    int i;
    char buff[17];
    unsigned char *pc = (unsigned char*)addr;
    char outputBuffer[8192] = "";

    // Output description if given.
    if (desc != NULL) {
    	c_snprintf(outputBuffer, sizeof(outputBuffer), "%s%s\r\n", outputBuffer, desc);
    }

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0) {
            	c_snprintf(outputBuffer, sizeof(outputBuffer), "%s  %s\r\n", outputBuffer, buff);
            }

            c_snprintf(outputBuffer, sizeof(outputBuffer), "%s  %04x ", outputBuffer, i);
        }

        c_snprintf(outputBuffer, sizeof(outputBuffer), "%s %02x", outputBuffer, pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];

        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
    	c_snprintf(outputBuffer, sizeof(outputBuffer), "%s   ", outputBuffer);
        i++;
    }

    c_snprintf(outputBuffer, sizeof(outputBuffer), "%s %s\r\n", outputBuffer, buff);

    LOG(LL_INFO, ("\r\n%s\r\n", outputBuffer));
#endif

}

uint8_t mgos_xymodem_send_payload(uint8_t payload[], size_t length, uint8_t packetType)
{
	uint8_t modemPacket[1029];
	uint16_t crcVal;
	uint16_t payloadLength = 0x0;
	size_t writtenLength = 0;
	uint8_t retries = 0;
	uint8_t responseByte;

	do {

		mgos_xymodem_flush_read_buffer();

#ifdef MGOS_XYMODEM_DEBUG
		LOG(LL_INFO, ("Sending Packet #%d", mgos_xymodem_state.packet_number));
#endif

		switch(packetType) {
			case MGOS_XYMODEM_SOH:
				payloadLength = 128;
				break;
			case MGOS_XYMODEM_STX:
				payloadLength = 1024;
				break;
			default:
#ifdef MGOS_XYMODEM_DEBUG
				LOG(LL_INFO, ("Invalid Packet Type Specified"));
#endif
				return 0;
		}

		if(length > payloadLength) {
#ifdef MGOS_XYMODEM_DEBUG
			LOG(LL_INFO, ("Specified length is larger than maximum payload length"));
#endif
			return 0;
		}

		memset(&modemPacket[0], 0x0, sizeof(modemPacket));

		modemPacket[0] = packetType;
		modemPacket[1] = mgos_xymodem_state.packet_number;
		modemPacket[2] = ~mgos_xymodem_state.packet_number;

		memcpy(&modemPacket[3], payload, length);

		crcVal = mgos_xymodem_xmodem_crc(&modemPacket[3], 0, payloadLength);

		modemPacket[payloadLength -2 + 5] = (uint8_t)(crcVal >> 8) & 0xFF;
		modemPacket[payloadLength -1 + 5] = (uint8_t)(crcVal & 0xFF);
#ifdef MGOS_XYMODEM_DEBUG
		LOG(LL_INFO, ("CRC Value is %d", crcVal));
#endif

#ifdef MGOS_XYMODEM_DEBUG
		mgos_xymodem_hex_dump(NULL, (void *)&modemPacket[0], payloadLength + 5);
#endif

		writtenLength = mgos_uart_write(mgos_xymodem_state.uart_no, &modemPacket, payloadLength + 5);

		mgos_uart_flush(mgos_xymodem_state.uart_no);

		if((uint16_t)writtenLength != (payloadLength + 5)) {
#ifdef MGOS_XYMODEM_DEBUG
			LOG(LL_INFO, ("Error Writing Packet! Wrote %d instead of %d byte(s)", writtenLength, sizeof(modemPacket)));
#endif
			return 0;
		}

		responseByte = mgos_xymodem_read_uart_byte();

		switch(responseByte) {
			case MGOS_XYMODEM_ACK:
#ifdef MGOS_XYMODEM_DEBUG
				LOG(LL_INFO, ("Packet Acknowledged!"));
#endif
				mgos_xymodem_state.packet_number++;
				break;
			case MGOS_XYMODEM_NAK:
#ifdef MGOS_XYMODEM_DEBUG
				LOG(LL_INFO, ("Packet Not Acknowledged, Retrying..."));
#endif
				retries++;
				break;
			case MGOS_XYMODEM_CAN:
				responseByte = mgos_xymodem_read_uart_byte();

				if(responseByte == MGOS_XYMODEM_CAN) {
#ifdef MGOS_XYMODEM_DEBUG
					LOG(LL_INFO, ("Target Cancelled Transmission."));
#endif
					return MGOS_XYMODEM_CAN;
				}
				break;
			case MGOS_XYMODEM_CRC16:
#ifdef MGOS_XYMODEM_DEBUG
				LOG(LL_INFO, ("Got another CRC16 announcement, Retrying..."));
#endif
				retries++;
				break;
			default:
#ifdef MGOS_XYMODEM_DEBUG
				LOG(LL_INFO, ("Unknown Response Byte Received"));
				mgos_xymodem_hex_dump(NULL, &responseByte, 1);
#endif

				retries++;
				break;
		}

	} while((responseByte != MGOS_XYMODEM_ACK) && (retries <= 10));

	return responseByte;
}


bool mgos_xymodem_transmit(FILE *fp, char *filename, size_t fileSize)
{
	uint8_t payload[1024];
	char str_file_size[64] = "";
	long int file_position = 0;
	struct mgos_uart_config uart_config;

	//mgos_uart_flush(mgos_xymodem_state.uart_no);

	mgos_uart_config_set_defaults(mgos_xymodem_state.uart_no, &uart_config);

	uart_config.baud_rate = 9600;
	uart_config.num_data_bits = 8;
	uart_config.parity = MGOS_UART_PARITY_NONE;
	uart_config.stop_bits = MGOS_UART_STOP_BITS_1;

	if(!mgos_uart_configure(mgos_xymodem_state.uart_no, &uart_config)) {
#ifdef MGOS_XYMODEM_DEBUG
		LOG(LL_INFO, ("Failed to configure UART for File Transfer"));
#endif
		return false;
	}

	mgos_uart_set_dispatcher(mgos_xymodem_state.uart_no, NULL, NULL);
	mgos_uart_set_rx_enabled(mgos_xymodem_state.uart_no, true);

	if(mgos_xymodem_determine_checksum() == MGOS_XYMODEM_CHECKSUM_NONE) {
#ifdef MGOS_XYMODEM_DEBUG
		LOG(LL_INFO, ("No checksum could be determined, cannot continue tranmission"));
#endif
		return false;
	}

	memset(&payload[0], 0x0, sizeof(payload));

	if(mgos_xymodem_state.protocol == MGOS_XYMODEM_PROTOCOL_YMODEM) {

		mgos_xymodem_state.packet_number = 0;

		//filename = file.name();
		//filename = filename.substring(filename.lastIndexOf('/') + 1);
		c_snprintf(str_file_size, sizeof(str_file_size), "%zu", fileSize);

		memcpy(&payload[0], filename, strlen(filename));
		memcpy(&payload[strlen(filename) + 1], &str_file_size, strlen(str_file_size));

		if(mgos_xymodem_send_payload(&payload[0], strlen(filename) + strlen(str_file_size) + 2, MGOS_XYMODEM_STX) != MGOS_XYMODEM_ACK) {
#ifdef MGOS_XYMODEM_DEBUG
			LOG(LL_INFO, ("Failed to receive ACK for Packet.."));
#endif
			return false;
		}

	} else {
		mgos_xymodem_state.packet_number = 1;
	}

	fseek(fp, 0, SEEK_SET);

	file_position = ftell(fp);

	if(file_position < 0) {
#ifdef MGOS_XYMODEM_DEBUG
		LOG(LL_INFO, ("Failed to determine file position"));
#endif
		return false;
	}

	while((unsigned long int)file_position < (unsigned long)fileSize) {

		uint16_t readSize = (fileSize - file_position >= sizeof(payload)) ? sizeof(payload) : (fileSize - file_position);

		memset(&payload[0], 0x0, sizeof(payload));

#ifdef MGOS_XYMODEM_DEBUG
		LOG(LL_INFO, ("Reading %d byte(s) from source file", readSize));
#endif

		fread(&payload[0], readSize, 1, fp);

		if(mgos_xymodem_send_payload(&payload[0], readSize, MGOS_XYMODEM_STX) != MGOS_XYMODEM_ACK) {
#ifdef MGOS_XYMODEM_DEBUG
			LOG(LL_INFO, ("Failed to recieve ACK for Packet..."));
#endif
			return false;
		}

		file_position = ftell(fp);

		if(file_position < 0) {
#ifdef MGOS_XYMODEM_DEBUG
			LOG(LL_INFO, ("Failed to determine file position"));
#endif
			return false;
		}
	}

	return mgos_xymodem_end_transmission();
}

void mgos_xymodem_set_protocol(enum mgos_xymodem_protocol_t protocol)
{
	mgos_xymodem_state.protocol = protocol;
}

void mgos_xymodem_set_crc_type(enum mgos_xymodem_checksum_t crcType)
{
	mgos_xymodem_state.checksum_type = crcType;
}

void mgos_xymodem_set_uart_no(int uartno)
{
	mgos_xymodem_state.uart_no = uartno;
}

unsigned int mgos_xymodem_calc_crc(uint8_t data[], uint8_t start, uint16_t length, uint8_t reflectIn, uint8_t reflectOut, uint16_t polynomial, uint16_t xorIn, uint16_t xorOut, uint16_t msbMask, uint16_t mask)
{
	unsigned int crc = xorIn;

	int j;
	uint8_t c;
	unsigned int bit;

	if (length == 0) return crc;

	for (int i = start; i < (start + length); i++)
	{
		c = data[i];

		if (reflectIn != 0)
			c = (uint8_t) mgos_xymodem_crc_reflect(c, 8);

		j = 0x80;

		while (j > 0)
		{
			bit = (unsigned int)(crc & msbMask);
			crc <<= 1;

			if ((c & j) != 0)
			{
				bit = (unsigned int)(bit ^ msbMask);
			}

			if (bit != 0)
			{
				crc ^= polynomial;
			}

			j >>= 1;
		}
	}

	if (reflectOut != 0)
		crc = (unsigned int)((mgos_xymodem_crc_reflect(crc, 32) ^ xorOut) & mask);

	return crc;
}

uint8_t mgos_xymodem_crc_reflect(uint8_t data, uint8_t bits)
{
	unsigned long reflection = 0x00000000;
	// Reflect the data about the center bit.
	for (uint8_t bit = 0; bit < bits; bit++)
	{
		// If the LSB bit is set, set the reflection of it.
		if ((data & 0x01) != 0)
		{
			reflection |= (unsigned long)(1 << ((bits - 1) - bit));
		}

		data = (uint8_t)(data >> 1);
	}

	return reflection;
}
