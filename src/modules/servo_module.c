#include "modules/servo_module.h"

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(servo_module, LOG_LEVEL_INF);

/* Standard RC servo: 50 Hz, 1 ms = 0°, 2 ms = 180° */
#define SERVO_PERIOD_NS    20000000U
#define SERVO_MIN_PULSE_NS  1000000U
#define SERVO_MAX_PULSE_NS  2000000U
#define SERVO_CHANNEL      0U

static const struct device *servo_dev = DEVICE_DT_GET(DT_NODELABEL(ledc0));
static int current_angle = 90;

static void servo_self_test(void)
{
	static const int test_angles[] = {0, 90, 180, 90};

	LOG_INF("Servo self-test start");
	for (int i = 0; i < ARRAY_SIZE(test_angles); i++) {
		servo_module_set_angle(test_angles[i]);
		k_msleep(500);
	}
	LOG_INF("Servo self-test done");
}

int servo_module_init(void)
{
	if (!device_is_ready(servo_dev)) {
		LOG_ERR("Servo PWM device not ready");
		return -ENODEV;
	}

	servo_self_test();

	LOG_INF("Servo module ready on GPIO6");
	return 0;
}

int servo_module_set_angle(int angle)
{
	if (angle < 0 || angle > 180) {
		LOG_ERR("Servo angle out of range: %d", angle);
		return -EINVAL;
	}

	uint32_t pulse_ns = SERVO_MIN_PULSE_NS +
			    (uint32_t)((angle * (SERVO_MAX_PULSE_NS - SERVO_MIN_PULSE_NS)) / 180);

	int ret = pwm_set(servo_dev, SERVO_CHANNEL, SERVO_PERIOD_NS, pulse_ns,
			  PWM_POLARITY_NORMAL);

	if (ret < 0) {
		LOG_ERR("pwm_set failed: %d", ret);
		return ret;
	}

	current_angle = angle;
	LOG_INF("Servo -> %d deg (pulse %u ns)", angle, pulse_ns);
	return 0;
}

int servo_module_get_angle(void)
{
	return current_angle;
}
