#ifndef MODULES_LED_MODULE_H_
#define MODULES_LED_MODULE_H_

#include <stdbool.h>

#define LED_COUNT 3

int led_module_init(void);
int led_module_set(int led, bool on);
int led_module_get(int led, bool *on);

#endif /* MODULES_LED_MODULE_H_ */
