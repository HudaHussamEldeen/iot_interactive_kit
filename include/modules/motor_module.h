#ifndef MODULES_MOTOR_MODULE_H_
#define MODULES_MOTOR_MODULE_H_

#include <stdint.h>

typedef enum {
	MOTOR_DIR_FORWARD  = 0,
	MOTOR_DIR_BACKWARD = 1,
} motor_dir_t;

struct motor_state {
	int32_t    speed_percent; /* 0–100 */
	motor_dir_t direction;
};

int motor_module_init(void);
int motor_module_set(int32_t speed_percent, motor_dir_t direction);
int motor_module_stop(void);
void motor_module_get(struct motor_state *out);

#endif /* MODULES_MOTOR_MODULE_H_ */
