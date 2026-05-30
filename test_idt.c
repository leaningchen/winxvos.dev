#include <types.h>
struct idt_entry { uint16_t offset_0_15; uint16_t selector; uint8_t ist; uint8_t type_attr; uint16_t offset_16_31; uint32_t offset_32_63; uint32_t reserved; } __attribute__((packed));
_Static_assert(sizeof(struct idt_entry) == 16, "idt_entry must be 16 bytes");
int x = sizeof(struct idt_entry);