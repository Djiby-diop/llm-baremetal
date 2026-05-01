#ifndef OO_DRIVERS_PS2_H
#define OO_DRIVERS_PS2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OO PS/2 Keyboard Metadriver (Sensor)
 *
 * Implémenté via polling pour éviter les IRQs. L'IA lit quand elle a du temps.
 */

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64

void oo_ps2_init(void);
int oo_ps2_has_data(void);
uint8_t oo_ps2_read_scancode(void);
char oo_ps2_scancode_to_ascii(uint8_t scancode, int shift_pressed);

#ifdef __cplusplus
}
#endif

#endif
