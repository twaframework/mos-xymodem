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

void mgos_xymodem_hex_dump(char *desc, void *addr, int len)
{

#ifdef MGOS_XYMODEM_DEBUG_HEXDUMP
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

    LOG(LL_DEBUG, ("\r\n%s\r\n", outputBuffer));
#endif

}

unsigned int mgos_xymodem_calc_crc(uint8_t *data, uint8_t start, uint16_t length, uint8_t reflectIn, uint8_t reflectOut, uint16_t polynomial, uint16_t xorIn, uint16_t xorOut, uint16_t msbMask, uint16_t mask)
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

	for (uint8_t bit = 0; bit < bits; bit++)
	{
		if ((data & 0x01) != 0)
		{
			reflection |= (unsigned long)(1 << ((bits - 1) - bit));
		}

		data = (uint8_t)(data >> 1);
	}

	return reflection;
}

uint8_t mgos_xymodem_calc_checksum(uint8_t *data, uint16_t len)
{
	uint8_t iC, i1;

	iC = 0;

	for(i1 = 0; i1 < len; i1++) {
		iC += data[i1];
	}

	return (uint8_t)(iC & 0xFF);
}
