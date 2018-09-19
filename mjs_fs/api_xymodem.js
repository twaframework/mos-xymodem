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

let XYModem = {
	
	_trns_y : ffi('bool mgos_xymodem_transmit_ymodem(void *, char *)'),
	_suart : ffi('void mgos_xymodem_set_uart(int)'),
	
	create : function(uart_no) {
		let obj = Object.create(XYModem._proto);
		XYModem._suart(uart_no);
		return obj;
	},
	
	_proto : {
		sendYModem : function(file, filename) {
			return XYModem._trns_y(file, filename);
		},
	
	},
	
}