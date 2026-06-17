#ifndef MODULES_VL6180X_MODULE_H_
#define MODULES_VL6180X_MODULE_H_

#include <stdint.h>

int  vl6180x_module_init(void);
int  vl6180x_module_read_range(uint8_t *range_mm);

#endif /* MODULES_VL6180X_MODULE_H_ */
