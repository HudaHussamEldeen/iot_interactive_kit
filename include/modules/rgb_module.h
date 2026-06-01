#ifndef MODULES_RGB_MODULE_H_
#define MODULES_RGB_MODULE_H_

#include <zephyr/kernel.h>

int rgb_module_init(void);
void rgb_module_run_pattern(k_timeout_t delay);

#endif /* MODULES_RGB_MODULE_H_ */
