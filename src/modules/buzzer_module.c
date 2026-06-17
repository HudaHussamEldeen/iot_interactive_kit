#include "modules/buzzer_module.h"

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(buzzer_module, LOG_LEVEL_INF);

#define BUZZER_CHANNEL  1U
#define BUZZER_MIN_FREQ 100U
#define BUZZER_MAX_FREQ 20000U

static const struct device *buzzer_dev = DEVICE_DT_GET(DT_NODELABEL(ledc0));

void buzzer_module_on(uint32_t freq_hz)
{
	if (freq_hz < BUZZER_MIN_FREQ || freq_hz > BUZZER_MAX_FREQ) {
		LOG_ERR("Buzzer freq out of range: %u", freq_hz);
		return;
	}

	uint32_t period_ns = NSEC_PER_SEC / freq_hz;

	pwm_set(buzzer_dev, BUZZER_CHANNEL, period_ns, period_ns / 2U, PWM_POLARITY_NORMAL);
}

void buzzer_module_off(void)
{
	uint32_t period_ns = NSEC_PER_SEC / 1000U;

	pwm_set(buzzer_dev, BUZZER_CHANNEL, period_ns, 0, PWM_POLARITY_NORMAL);
}

void buzzer_module_beep(uint32_t freq_hz, uint32_t duration_ms)
{
	buzzer_module_on(freq_hz);
	k_msleep(duration_ms);
	buzzer_module_off();
}

static void buzzer_self_test(void)
{
	static const struct {
		uint32_t freq;
		uint32_t dur;
	} tones[] = {
		{1000, 150},
		{2000, 150},
		{3000, 250},
	};

	LOG_INF("Buzzer self-test start");
	for (int i = 0; i < ARRAY_SIZE(tones); i++) {
		buzzer_module_beep(tones[i].freq, tones[i].dur);
		k_msleep(80);
	}
	LOG_INF("Buzzer self-test done");
}

int buzzer_module_init(void)
{
	if (!device_is_ready(buzzer_dev)) {
		LOG_ERR("Buzzer PWM device not ready");
		return -ENODEV;
	}

	buzzer_module_off();
	buzzer_self_test();

	LOG_INF("Buzzer module ready on GPIO5");
	return 0;
}
