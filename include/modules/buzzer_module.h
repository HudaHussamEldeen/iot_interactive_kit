#ifndef MODULES_BUZZER_MODULE_H_
#define MODULES_BUZZER_MODULE_H_

#include <stdint.h>

int  buzzer_module_init(void);
void buzzer_module_on(uint32_t freq_hz);
void buzzer_module_off(void);
void buzzer_module_beep(uint32_t freq_hz, uint32_t duration_ms);

#endif /* MODULES_BUZZER_MODULE_H_ */
