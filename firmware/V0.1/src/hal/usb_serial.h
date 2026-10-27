#ifndef APSLIM_USB_SERIAL_H_
#define APSLIM_USB_SERIAL_H_
#include <stdbool.h>
#include <stddef.h>
int usb_serial_init(void);
bool usb_serial_process(void);
bool usb_serial_is_connected(void);
/** Nonblocking enqueue; true means accepted, not acknowledged by the PC.
 * text must be non-NULL and remain valid for this call; length is in bytes.
 * Main context/single producer only. false: disconnected or insufficient buffer.
 */
bool usb_serial_write(const char *text, size_t length);
#endif
