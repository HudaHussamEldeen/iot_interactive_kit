#ifndef MODULES_ANALOG_MODULE_H_
#define MODULES_ANALOG_MODULE_H_

#include <stdint.h>

struct analog_reading {
	int32_t raw;     /* 0–4095 (12-bit) */
	int32_t percent; /* 0–100 */
};

int analog_module_init(void);
int analog_module_read_ldr(struct analog_reading *out);
int analog_module_read_water(struct analog_reading *out);

#endif /* MODULES_ANALOG_MODULE_H_ */
