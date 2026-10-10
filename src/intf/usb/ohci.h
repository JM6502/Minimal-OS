#ifndef OHCI_H
#define OHCI_H

#include <stdbool.h>
#include <stdint.h>

#include "usb/usb_stack.h"
#include "x86_64/pci.h"

#define OHCI_PORT_STATUS_CONNECTED (1U << 0)
#define OHCI_PORT_STATUS_LOW_SPEED (1U << 9)

bool ohci_init(pci_device_t* dev);
uint8_t ohci_get_port_count(void);
uint32_t ohci_get_port_status(uint8_t port);
void ohci_reset_port(uint8_t port);
bool ohci_control_transfer(uint8_t dev_addr, usb_setup_packet_t* setup,
                           void* data, uint16_t length, uint8_t max_packet,
                           bool low_speed);
void ohci_keyboard_interrupt_init(usb_device_t* dev);
bool ohci_keyboard_interrupt_poll(uint8_t* buffer, uint16_t length);
void ohci_mouse_interrupt_init(usb_device_t* dev);
bool ohci_mouse_interrupt_poll(uint8_t* buffer, uint16_t length);

#endif