# Mongoose OS X/YModem


## Overview

An implementation that allows you to transmit files over UART via X/Ymodem

## Examples

Make sure you set `debug.stdout_uart` and `debug_stderr_uart` to a different UART, as they will cause problems
when trying to transfer the file. 

Here's an example of using the library via C:

```
#include "mgos.h"
#include "mgos_xymodem.h"

void mgos_app_on_init(int ev, void *ev_data, void *userdata)
{
	FILE *fp;
  
  // This is the filename we will send to the destination if (YModem)
  
	char *file_name = (char *)"firmware.bin";
	
  // Make sure you configure the UART you are using properly for baud rate
  // expected by the destination!
  
  struct mgos_uart_config uart_config;

	mgos_uart_config_set_defaults(0, &uart_config);

	uart_config.baud_rate = 9600;

	if(!mgos_uart_configure(0, &uart_config)) {
		LOG(LL_ERROR, ("Failed to initialize UART 0"));
	}

  LOG(LL_INFO, ("App Initialized and Ready to Go"));
  LOG(LL_INFO, ("Attempting Transmission"));

  fp = fopen("fireware_V-3.0.3.bin", "r");

  // These event handlers will let you know when the transfer is complete or a failure
  // Check the log for reasons why
  
  mgos_event_add_handler(MGOS_EVENT_COMPLETE, mgos_app_on_transfer_complete, fp);
  mgos_event_add_handler(MGOS_EVENT_FAILURE, mgos_app_on_transfer_failure, fp);

  mgos_xymodem_transmit(0, fp, file_name);

}

void mgos_app_on_transfer_complete(int ev, void *ev_data, void *fp) {
	FILE *file = (FILE *)fp;
	fclose(file);
}

void mgos_app_on_transfer_failure(int ev, void *ev_data, void *fp) {
	FILE *file = (FILE *)fp;
	fclose(file);
}

enum mgos_app_init_result mgos_app_init(void) {

  mgos_event_add_handler(MGOS_EVENT_INIT_DONE, mgos_app_on_init, NULL);
  return MGOS_APP_INIT_SUCCESS;
}
```

Javascript Example:

```
load("api_xymodem.js");
load("api_file.js");
load('api_timer.js');

print ("XYModem Demo: Sending YModem file");

let xymodem = XYModem.create(0); // UART #0

Timer.set(5000, 0, function() {
	
	print("Opening firmware file stored in VFS and sending..");
	
	let fp = File.fopen("fireware_V-3.0.3.bin", "r");
	xymodem.sendYModem(fp, "firmware.bin"); // This is the filename we send to the destination
}, null);
```
