#ifndef MODULES_RELAY_MODULE_H_
#define MODULES_RELAY_MODULE_H_

#include <stdbool.h>

int relay_module_init(void);
int relay_module_set(int relay, bool on);
int relay_module_get(int relay, bool *on);

#endif /* MODULES_RELAY_MODULE_H_ */
