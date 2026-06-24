#include "modules/motor_module.h"

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(motor_module, LOG_LEVEL_INF);

/* 50 Hz — L293D requires low-frequency PWM for reliable operation */
#define MOTOR_PERIOD_NS 20000000U

#define MOTOR_CH_IN1 3U
#define MOTOR_CH_IN2 4U

static const struct device *ledc_dev = DEVICE_DT_GET(DT_NODELABEL(ledc0));
static struct motor_state current_state = {0, MOTOR_DIR_FORWARD};

static int set_channels(uint32_t duty_in1, uint32_t duty_in2)
{
	int ret1 = pwm_set(ledc_dev, MOTOR_CH_IN1, MOTOR_PERIOD_NS, duty_in1, PWM_POLARITY_NORMAL);
	int ret2 = pwm_set(ledc_dev, MOTOR_CH_IN2, MOTOR_PERIOD_NS, duty_in2, PWM_POLARITY_NORMAL);

	if (ret1 < 0) {
		LOG_ERR("pwm_set IN1 failed: %d", ret1);
	}
	if (ret2 < 0) {
		LOG_ERR("pwm_set IN2 failed: %d", ret2);
	}

	return (ret1 < 0) ? ret1 : ret2;
}

static void motor_self_test(void);

int motor_module_init(void)
{
	if (!device_is_ready(ledc_dev)) {
		LOG_ERR("LEDC device not ready");
		return -ENODEV;
	}

	/* Prime both LEDC channels so hardware latches before coasting */
	(void)set_channels(1, 1);
	k_msleep(5);
	(void)set_channels(0, 0);

	/* motor_self_test(); */
	LOG_INF("Motor module ready — IN1=GPIO15 (CH3), IN2=GPIO16 (CH4)");
	return 0;
}

static void motor_self_test(void)
{
	LOG_INF("Motor self-test start");
	motor_module_set(80, MOTOR_DIR_FORWARD);
	k_msleep(1500);
	motor_module_set(80, MOTOR_DIR_BACKWARD);
	k_msleep(1500);
	motor_module_stop();
	LOG_INF("Motor self-test done");
}

int motor_module_set(int32_t speed_percent, motor_dir_t direction)
{
	if (speed_percent < 0 || speed_percent > 100) {
		LOG_ERR("speed_percent out of range: %d", (int)speed_percent);
		return -EINVAL;
	}

	/* Stop active channel before switching direction */
	if (current_state.speed_percent > 0 && direction != current_state.direction) {
		set_channels(0, 0);
		k_msleep(100);
	}

	uint32_t duty = (uint32_t)((uint64_t)MOTOR_PERIOD_NS * (uint32_t)speed_percent / 100U);
	uint32_t in1  = (direction == MOTOR_DIR_FORWARD)  ? duty : 0U;
	uint32_t in2  = (direction == MOTOR_DIR_BACKWARD) ? duty : 0U;

	int ret = set_channels(in1, in2);

	if (ret < 0) {
		return ret;
	}

	current_state.speed_percent = speed_percent;
	current_state.direction     = direction;

	LOG_INF("Motor %s %d%%",
		direction == MOTOR_DIR_FORWARD ? "forward" : "backward",
		(int)speed_percent);
	return 0;
}

int motor_module_stop(void)
{
	int ret = set_channels(0, 0);

	if (ret < 0) {
		return ret;
	}

	current_state.speed_percent = 0;
	LOG_INF("Motor coasting (stopped)");
	return 0;
}

void motor_module_get(struct motor_state *out)
{
	*out = current_state;
}
