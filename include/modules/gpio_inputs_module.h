#ifndef MODULES_GPIO_INPUTS_MODULE_H_
#define MODULES_GPIO_INPUTS_MODULE_H_

#include <stdbool.h>

int gpio_inputs_module_init(void);
int gpio_inputs_module_read_magnetic(bool *closed);
int gpio_inputs_module_read_button(bool *pressed);

#endif /* MODULES_GPIO_INPUTS_MODULE_H_ */
