#ifndef OO_DRIVERS_UART_H
#define OO_DRIVERS_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COM1_PORT 0x3F8
#define COM2_PORT 0x2F8

/*
 * OO UART Metadriver (Actuator/Sensor)
 *
 * Utilise l'architecture de scrutation (polling) pour ne pas interrompre l'IA.
 */

void oo_uart_init(uint16_t port);
void oo_uart_write_char(uint16_t port, char c);
void oo_uart_write_string(uint16_t port, const char* str);
int oo_uart_has_data(uint16_t port);
char oo_uart_read_char(uint16_t port);

#ifdef __cplusplus
}
#endif

#endif
