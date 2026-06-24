#ifndef MODULES_DHT22_MODULE_H_
#define MODULES_DHT22_MODULE_H_

#include <stdint.h>

struct dht22_reading {
	int32_t temp_1,  temp_2;  /* °C  — val1 + val2/1e6 */
	int32_t humid_1, humid_2; /* %RH — val1 + val2/1e6 */
};

int dht22_module_init(void);
int dht22_module_read(struct dht22_reading *out);

#endif /* MODULES_DHT22_MODULE_H_ */
