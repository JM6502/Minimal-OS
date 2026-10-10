#include "usb/ohci.h"

#include "serial.h"
#include "string.h"
#include "x86_64/scheduler.h"

#define OHCI_REG_REVISION       0x00
#define OHCI_REG_CONTROL        0x04
#define OHCI_REG_COMMAND_STATUS 0x08
#define OHCI_REG_INTERRUPT_STATUS 0x0C
#define OHCI_REG_INTERRUPT_DISABLE 0x14
#define OHCI_REG_HCCA           0x18
#define OHCI_REG_CONTROL_HEAD   0x20
#define OHCI_REG_PERIODIC_START 0x40
#define OHCI_REG_FRAME_INTERVAL 0x34
#define OHCI_REG_LS_THRESHOLD   0x44
#define OHCI_REG_RH_DESC_A      0x48
#define OHCI_REG_RH_STATUS      0x50
#define OHCI_REG_RH_PORT_STATUS 0x54

#define OHCI_CMD_HCR  (1U << 0)
#define OHCI_CMD_CLF  (1U << 1)
#define OHCI_CTRL_PLE (1U << 2)
#define OHCI_CTRL_CLE (1U << 4)
#define OHCI_CTRL_HCFS_OPERATIONAL (2U << 6)

#define OHCI_PORT_CCS  (1U << 0)
#define OHCI_PORT_PES  (1U << 1)
#define OHCI_PORT_PRS  (1U << 4)
#define OHCI_PORT_LSDA (1U << 9)
#define OHCI_PORT_CSC  (1U << 16)
#define OHCI_PORT_PESC (1U << 17)
#define OHCI_PORT_PRSC (1U << 20)

#define OHCI_TD_CC_NOT_ACCESSED (0xFU << 28)
#define OHCI_TD_DI_IMMEDIATE    (0U << 21)
#define OHCI_TD_DP_SETUP        (0U << 19)
#define OHCI_TD_DP_OUT          (1U << 19)
#define OHCI_TD_DP_IN           (2U << 19)
#define OHCI_TD_T_CARRY         (0U << 24)
#define OHCI_TD_T_DATA0         (1U << 24)
#define OHCI_TD_T_DATA1         (2U << 24)
#define OHCI_TD_ROUNDING        (1U << 18)

#define OHCI_PERIODIC_SLOTS 32
#define OHCI_DMA_POOL_SIZE (64 * 1024)
#define OHCI_CONTROL_MAX_LENGTH 4096

typedef struct __attribute__((packed, aligned(16))) {
    uint32_t control;
    uint32_t tailp;
    uint32_t headp;
    uint32_t next_ed;
} ohci_ed_t;

typedef struct __attribute__((packed, aligned(16))) {
    uint32_t control;
    uint32_t current_buffer;
    uint32_t next_td;
    uint32_t buffer_end;
} ohci_td_t;

typedef struct __attribute__((packed, aligned(256))) {
    uint32_t interrupt_table[OHCI_PERIODIC_SLOTS];
    uint16_t frame_number;
    uint16_t pad;
    volatile uint32_t done_head;
    uint8_t reserved[120];
} ohci_hcca_t;

typedef struct {
    ohci_ed_t* ed;
    ohci_td_t* td;
    ohci_td_t* dummy;
    uint8_t* buffer;
    bool completed;
} ohci_interrupt_endpoint_t;

static volatile uint32_t* g_ohci_regs;
static uint8_t g_ohci_dma_storage[OHCI_DMA_POOL_SIZE]
    __attribute__((aligned(4096)));
static uint8_t* g_ohci_dma_pool;
static size_t g_ohci_dma_offset;
static ohci_hcca_t* g_ohci_hcca;
static ohci_ed_t* g_ohci_control_ed;
static ohci_td_t* g_ohci_control_dummy;
static uint8_t* g_ohci_control_buffer;
static uint8_t* g_ohci_setup_buffer;
static uint8_t g_ohci_port_count;
static bool g_ohci_initialized;
static ohci_interrupt_endpoint_t g_ohci_keyboard;
static ohci_interrupt_endpoint_t g_ohci_mouse;

extern volatile void* pci_map_bar_mmio(pci_device_t* dev, int bar_num);
extern uint32_t pci_read_config_dword(uint8_t bus, uint8_t device,
                                      uint8_t function, uint8_t offset);
extern void pci_write_config_dword(uint8_t bus, uint8_t device,
                                   uint8_t function, uint8_t offset,
                                   uint32_t value);

static inline uint32_t ohci_read(uint32_t offset) {
    __asm__ volatile("mfence" ::: "memory");
    return g_ohci_regs[offset / sizeof(uint32_t)];
}

static inline void ohci_write(uint32_t offset, uint32_t value) {
    g_ohci_regs[offset / sizeof(uint32_t)] = value;
    __asm__ volatile("mfence" ::: "memory");
}

static void* ohci_dma_alloc(size_t size, size_t alignment) {
    size_t aligned = (g_ohci_dma_offset + alignment - 1) & ~(alignment - 1);
    if (!g_ohci_dma_pool || aligned > OHCI_DMA_POOL_SIZE ||
        size > OHCI_DMA_POOL_SIZE - aligned) {
        return NULL;
    }

    void* result = g_ohci_dma_pool + aligned;
    g_ohci_dma_offset = aligned + size;
    memset(result, 0, size);
    return result;
}

static bool ohci_dma_address(const void* ptr, uint32_t* address) {
    uintptr_t value = (uintptr_t)ptr;
    if (!ptr || value > UINT32_MAX) return false;
    *address = (uint32_t)value;
    return true;
}

static void ohci_short_delay(void) {
    for (volatile uint32_t i = 0; i < 100000; i++) {
        __asm__ volatile("pause");
    }
}

bool ohci_init(pci_device_t* dev) {
    if (!dev || dev->bar_type[0] != PCI_BAR_MEM || (dev->bar[0] & 1U) ||
        (dev->bar[0] & 0x6U) == 0x4U) {
        serial_write_str("OHCI: invalid or unsupported MMIO BAR0\n");
        return false;
    }

    volatile void* regs = pci_map_bar_mmio(dev, 0);
    if (!regs) {
        serial_write_str("OHCI: failed to map controller registers\n");
        return false;
    }

    g_ohci_regs = (volatile uint32_t*)regs;

    uint32_t command = pci_read_config_dword(dev->bus, dev->device,
                                              dev->function, 0x04);
    pci_write_config_dword(dev->bus, dev->device, dev->function, 0x04,
                           (command | 0x0006U) & 0xFFFFU);

    uint32_t revision = ohci_read(OHCI_REG_REVISION) & 0xFFU;
    if (revision < 0x10U || revision > 0x11U) {
        serial_write_str("OHCI: unsupported revision\n");
        return false;
    }

    ohci_write(OHCI_REG_CONTROL, 0);
    ohci_write(OHCI_REG_COMMAND_STATUS, OHCI_CMD_HCR);
    uint32_t timeout = 1000000;
    while ((ohci_read(OHCI_REG_COMMAND_STATUS) & OHCI_CMD_HCR) && timeout--) {
        __asm__ volatile("pause");
    }
    if (!timeout) {
        serial_write_str("OHCI: reset timed out\n");
        return false;
    }

    memset(g_ohci_dma_storage, 0, sizeof(g_ohci_dma_storage));
    g_ohci_dma_pool = g_ohci_dma_storage;
    g_ohci_dma_offset = 0;

    uint32_t hcca_address;
    g_ohci_hcca = (ohci_hcca_t*)ohci_dma_alloc(sizeof(*g_ohci_hcca), 256);
    g_ohci_control_ed = (ohci_ed_t*)ohci_dma_alloc(sizeof(*g_ohci_control_ed), 16);
    g_ohci_control_dummy = (ohci_td_t*)ohci_dma_alloc(sizeof(*g_ohci_control_dummy), 16);
    g_ohci_control_buffer = (uint8_t*)ohci_dma_alloc(OHCI_CONTROL_MAX_LENGTH, 4096);
    g_ohci_setup_buffer = (uint8_t*)ohci_dma_alloc(16, 16);
    if (!g_ohci_hcca || !g_ohci_control_ed || !g_ohci_control_dummy ||
        !g_ohci_control_buffer || !g_ohci_setup_buffer ||
        !ohci_dma_address(g_ohci_hcca, &hcca_address)) {
        serial_write_str("OHCI: DMA structures unavailable below 4GiB\n");
        return false;
    }

    ohci_write(OHCI_REG_HCCA, hcca_address);
    ohci_write(OHCI_REG_INTERRUPT_DISABLE, 0xFFFFFFFFU);
    ohci_write(OHCI_REG_FRAME_INTERVAL, 0x2EDF2778U);
    ohci_write(OHCI_REG_PERIODIC_START, 0x2A2FU);
    ohci_write(OHCI_REG_LS_THRESHOLD, 0x628U);

    uint32_t descriptor = ohci_read(OHCI_REG_RH_DESC_A);
    g_ohci_port_count = (uint8_t)(descriptor & 0xFFU);
    if (g_ohci_port_count == 0 || g_ohci_port_count > 15) {
        serial_write_str("OHCI: invalid root hub port count\n");
        return false;
    }

    ohci_write(OHCI_REG_RH_STATUS, 1U << 16);
    ohci_write(OHCI_REG_CONTROL,
               OHCI_CTRL_HCFS_OPERATIONAL | OHCI_CTRL_PLE);
    ohci_short_delay();

    if ((ohci_read(OHCI_REG_CONTROL) & (3U << 6)) !=
        OHCI_CTRL_HCFS_OPERATIONAL) {
        serial_write_str("OHCI: controller failed to enter operational state\n");
        return false;
    }

    g_ohci_initialized = true;
    serial_write_str("OHCI: controller initialized, ports=");
    serial_write_dec(g_ohci_port_count);
    serial_write_str("\n");
    return true;
}

uint8_t ohci_get_port_count(void) {
    return g_ohci_port_count;
}

uint32_t ohci_get_port_status(uint8_t port) {
    if (!g_ohci_initialized || port >= g_ohci_port_count) return 0;
    return ohci_read(OHCI_REG_RH_PORT_STATUS + (uint32_t)port * 4U);
}

void ohci_reset_port(uint8_t port) {
    if (!g_ohci_initialized || port >= g_ohci_port_count) return;

    uint32_t reg = OHCI_REG_RH_PORT_STATUS + (uint32_t)port * 4U;
    uint32_t status = ohci_read(reg);
    if (!(status & OHCI_PORT_CCS)) return;

    ohci_write(reg, OHCI_PORT_PRS);
    sleep(50);
    for (volatile uint32_t i = 0; i < 10000000; i++) {
        if (!(ohci_read(reg) & OHCI_PORT_PRS)) break;
        __asm__ volatile("pause");
    }
    ohci_short_delay();

    status = ohci_read(reg);
    if (status & OHCI_PORT_PRSC) ohci_write(reg, OHCI_PORT_PRSC);
    if (status & OHCI_PORT_CSC) ohci_write(reg, OHCI_PORT_CSC);
    if ((status & OHCI_PORT_CCS) && !(status & OHCI_PORT_PES)) {
        ohci_write(reg, OHCI_PORT_PES);
    }
    ohci_short_delay();
}

static uint8_t ohci_td_condition_code(const ohci_td_t* td) {
    return (uint8_t)(td->control >> 28);
}

bool ohci_control_transfer(uint8_t dev_addr, usb_setup_packet_t* setup,
                           void* data, uint16_t length, uint8_t max_packet,
                           bool low_speed) {
    if (!g_ohci_initialized || !setup || length > OHCI_CONTROL_MAX_LENGTH) {
        return false;
    }
    if (max_packet == 0 || max_packet > 64) max_packet = 8;

    uint32_t setup_address;
    uint32_t buffer_address = 0;
    if (!ohci_dma_address(g_ohci_setup_buffer, &setup_address)) return false;
    memcpy(g_ohci_setup_buffer, setup, sizeof(*setup));

    uint16_t data_length = length;
    if (data_length > setup->wLength) data_length = setup->wLength;
    if (data_length && (!data || !ohci_dma_address(g_ohci_control_buffer,
                                                    &buffer_address))) {
        return false;
    }

    bool direction_in = (setup->bmRequestType & 0x80U) != 0;
    if (data_length && direction_in) {
        memset(g_ohci_control_buffer, 0, data_length);
    } else if (data_length) {
        memcpy(g_ohci_control_buffer, data, data_length);
    }

    uint32_t ed_address;
    uint32_t dummy_address;
    if (!ohci_dma_address(g_ohci_control_ed, &ed_address) ||
        !ohci_dma_address(g_ohci_control_dummy, &dummy_address)) {
        return false;
    }

    ohci_td_t* setup_td = (ohci_td_t*)ohci_dma_alloc(sizeof(*setup_td), 16);
    ohci_td_t* data_td = data_length
        ? (ohci_td_t*)ohci_dma_alloc(sizeof(*data_td), 16) : NULL;
    ohci_td_t* status_td = (ohci_td_t*)ohci_dma_alloc(sizeof(*status_td), 16);
    uint32_t setup_td_address;
    uint32_t data_td_address = 0;
    uint32_t status_td_address;
    if (!setup_td || (data_length && !data_td) || !status_td ||
        !ohci_dma_address(setup_td, &setup_td_address) ||
        (data_length && !ohci_dma_address(data_td, &data_td_address)) ||
        !ohci_dma_address(status_td, &status_td_address)) {
        return false;
    }

    setup_td->control = OHCI_TD_CC_NOT_ACCESSED | OHCI_TD_DI_IMMEDIATE |
                        OHCI_TD_DP_SETUP | OHCI_TD_T_DATA0;
    setup_td->current_buffer = setup_address;
    setup_td->next_td = data_length ? data_td_address : status_td_address;
    setup_td->buffer_end = setup_address + sizeof(*setup) - 1U;

    if (data_td) {
        data_td->control = OHCI_TD_CC_NOT_ACCESSED | OHCI_TD_DI_IMMEDIATE |
            (direction_in ? OHCI_TD_DP_IN | OHCI_TD_ROUNDING : OHCI_TD_DP_OUT) |
            OHCI_TD_T_CARRY;
        data_td->current_buffer = buffer_address;
        data_td->next_td = status_td_address;
        data_td->buffer_end = buffer_address + data_length - 1U;
    }

    status_td->control = OHCI_TD_CC_NOT_ACCESSED | OHCI_TD_DI_IMMEDIATE |
        (direction_in ? OHCI_TD_DP_OUT : OHCI_TD_DP_IN) | OHCI_TD_T_DATA1;
    status_td->current_buffer = 0;
    status_td->next_td = dummy_address;
    status_td->buffer_end = 0;

    g_ohci_control_ed->control = (dev_addr & 0x7FU) |
        ((low_speed ? 1U : 0U) << 13) | ((uint32_t)max_packet << 16);
    g_ohci_control_ed->tailp = dummy_address;
    g_ohci_control_ed->headp = setup_td_address;
    g_ohci_control_ed->next_ed = 0;
    ohci_write(OHCI_REG_CONTROL_HEAD, ed_address);

    uint32_t control = ohci_read(OHCI_REG_CONTROL);
    ohci_write(OHCI_REG_CONTROL, control | OHCI_CTRL_CLE | OHCI_CTRL_PLE);
    ohci_write(OHCI_REG_COMMAND_STATUS, OHCI_CMD_CLF);

    bool completed = false;
    for (volatile uint32_t i = 0; i < 20000000; i++) {
        uint32_t head = g_ohci_control_ed->headp & ~0xFU;
        if (head == dummy_address) {
            completed = true;
            break;
        }
        __asm__ volatile("pause");
    }

    ohci_write(OHCI_REG_CONTROL,
               ohci_read(OHCI_REG_CONTROL) & ~OHCI_CTRL_CLE);
    if (!completed || ohci_td_condition_code(setup_td) != 0 ||
        (data_td && ohci_td_condition_code(data_td) != 0) ||
        ohci_td_condition_code(status_td) != 0) {
        return false;
    }

    if (data_length && direction_in) memcpy(data, g_ohci_control_buffer, data_length);
    return true;
}

static void ohci_interrupt_init(ohci_interrupt_endpoint_t* endpoint,
                                usb_device_t* dev, bool mouse) {
    uint8_t endpoint_number = mouse ? dev->mouse_endpoint : dev->keyboard_endpoint;
    uint16_t max_packet = mouse ? dev->mouse_max_packet_size
                                : dev->keyboard_max_packet_size;
    if (max_packet == 0 || max_packet > 8) max_packet = 8;

    endpoint->ed = (ohci_ed_t*)ohci_dma_alloc(sizeof(*endpoint->ed), 16);
    endpoint->td = (ohci_td_t*)ohci_dma_alloc(sizeof(*endpoint->td), 16);
    endpoint->dummy = (ohci_td_t*)ohci_dma_alloc(sizeof(*endpoint->dummy), 16);
    endpoint->buffer = (uint8_t*)ohci_dma_alloc(8, 8);
    uint32_t ed_address;
    uint32_t td_address;
    uint32_t dummy_address;
    uint32_t buffer_address;
    if (!endpoint->ed || !endpoint->td || !endpoint->dummy || !endpoint->buffer ||
        !ohci_dma_address(endpoint->ed, &ed_address) ||
        !ohci_dma_address(endpoint->td, &td_address) ||
        !ohci_dma_address(endpoint->dummy, &dummy_address) ||
        !ohci_dma_address(endpoint->buffer, &buffer_address)) {
        serial_write_str("OHCI: failed to allocate interrupt endpoint\n");
        return;
    }

    endpoint->ed->control = (dev->address & 0x7FU) |
        ((uint32_t)(endpoint_number & 0x0FU) << 7) | (2U << 11) |
        ((dev->low_speed ? 1U : 0U) << 13) | ((uint32_t)max_packet << 16);
    endpoint->ed->tailp = dummy_address;
    endpoint->ed->headp = td_address;
    endpoint->ed->next_ed = 0;

    endpoint->td->control = OHCI_TD_CC_NOT_ACCESSED |
                           OHCI_TD_DI_IMMEDIATE | OHCI_TD_DP_IN;
    endpoint->td->current_buffer = buffer_address;
    endpoint->td->next_td = dummy_address;
    endpoint->td->buffer_end = buffer_address + max_packet - 1U;
    endpoint->completed = false;

    if (!g_ohci_keyboard.ed && !mouse) {
        g_ohci_keyboard = *endpoint;
    } else if (!g_ohci_mouse.ed && mouse) {
        g_ohci_mouse = *endpoint;
    }

    ohci_ed_t* first = g_ohci_keyboard.ed ? g_ohci_keyboard.ed
                                          : g_ohci_mouse.ed;
    if (g_ohci_keyboard.ed && g_ohci_mouse.ed) {
        uint32_t mouse_address;
        if (ohci_dma_address(g_ohci_mouse.ed, &mouse_address)) {
            g_ohci_keyboard.ed->next_ed = mouse_address;
        }
    }
    uint32_t first_address;
    if (!first || !ohci_dma_address(first, &first_address)) return;
    for (size_t i = 0; i < OHCI_PERIODIC_SLOTS; i++) {
        g_ohci_hcca->interrupt_table[i] = first_address;
    }

    uint32_t control = ohci_read(OHCI_REG_CONTROL);
    ohci_write(OHCI_REG_CONTROL, control | OHCI_CTRL_PLE);
}

void ohci_keyboard_interrupt_init(usb_device_t* dev) {
    if (!dev || !g_ohci_initialized) return;
    ohci_interrupt_init(&g_ohci_keyboard, dev, false);
}

void ohci_mouse_interrupt_init(usb_device_t* dev) {
    if (!dev || !g_ohci_initialized) return;
    ohci_interrupt_init(&g_ohci_mouse, dev, true);
}

static void ohci_reap_done_queue(void) {
    __asm__ volatile("mfence" ::: "memory");
    uint32_t done_head = g_ohci_hcca->done_head & ~0xFU;
    if (!done_head) return;

    g_ohci_hcca->done_head = 0;
    __asm__ volatile("mfence" ::: "memory");

    uintptr_t pool_start = (uintptr_t)g_ohci_dma_storage;
    uintptr_t pool_end = pool_start + sizeof(g_ohci_dma_storage);
    for (uint32_t count = 0; done_head && count < 1024; count++) {
        uintptr_t td_address = done_head;
        if (td_address < pool_start ||
            td_address > pool_end - sizeof(ohci_td_t)) {
            break;
        }

        ohci_td_t* td = (ohci_td_t*)td_address;
        uint32_t next = td->next_td & ~0xFU;
        if (td == g_ohci_keyboard.td) g_ohci_keyboard.completed = true;
        if (td == g_ohci_mouse.td) g_ohci_mouse.completed = true;
        done_head = next;
    }

    ohci_write(OHCI_REG_INTERRUPT_STATUS, 1U << 1);
}

static bool ohci_interrupt_poll(ohci_interrupt_endpoint_t* endpoint,
                                uint8_t* buffer, uint16_t length) {
    if (!endpoint->ed || !endpoint->td || !endpoint->dummy || !buffer) return false;
    ohci_reap_done_queue();
    if (!endpoint->completed) return false;
    endpoint->completed = false;

    uint8_t condition = ohci_td_condition_code(endpoint->td);
    bool success = condition == 0;
    uint16_t copy_length = length < 8 ? length : 8;
    if (success) memcpy(buffer, endpoint->buffer, copy_length);

    uint32_t head = endpoint->ed->headp;
    uint32_t td_address;
    uint32_t dummy_address;
    uint32_t buffer_address;
    if (!ohci_dma_address(endpoint->td, &td_address) ||
        !ohci_dma_address(endpoint->dummy, &dummy_address) ||
        !ohci_dma_address(endpoint->buffer, &buffer_address)) {
        return success;
    }

    uint32_t carry = head & 0x2U;
    endpoint->td->control = OHCI_TD_CC_NOT_ACCESSED |
                            OHCI_TD_DI_IMMEDIATE | OHCI_TD_DP_IN;
    endpoint->td->current_buffer = buffer_address;
    endpoint->td->next_td = dummy_address;
    endpoint->td->buffer_end = buffer_address + 7U;
    endpoint->ed->headp = td_address | carry;
    return success;
}

bool ohci_keyboard_interrupt_poll(uint8_t* buffer, uint16_t length) {
    return ohci_interrupt_poll(&g_ohci_keyboard, buffer, length);
}

bool ohci_mouse_interrupt_poll(uint8_t* buffer, uint16_t length) {
    return ohci_interrupt_poll(&g_ohci_mouse, buffer, length);
}