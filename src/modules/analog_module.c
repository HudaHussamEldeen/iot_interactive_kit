#include "modules/analog_module.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(analog_module, LOG_LEVEL_INF);

#define ADC_FULL_SCALE 4095 /* 12-bit */

static const struct adc_dt_spec ldr_ch =
	ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), ldr);
static const struct adc_dt_spec water_ch =
	ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), water_level);

static void analog_self_test(void);

int analog_module_init(void)
{
	int ret;

	if (!adc_is_ready_dt(&ldr_ch)) {
		LOG_ERR("LDR ADC not ready (GPIO9, ADC1_CH8)");
		return -ENODEV;
	}

	if (!adc_is_ready_dt(&water_ch)) {
		LOG_ERR("Water level ADC not ready (GPIO10, ADC1_CH9)");
		return -ENODEV;
	}

	ret = adc_channel_setup_dt(&ldr_ch);
	if (ret < 0) {
		LOG_ERR("LDR channel setup failed: %d", ret);
		return ret;
	}

	ret = adc_channel_setup_dt(&water_ch);
	if (ret < 0) {
		LOG_ERR("Water level channel setup failed: %d", ret);
		return ret;
	}

	analog_self_test();
	LOG_INF("Analog module ready (LDR=GPIO9, Water=GPIO10)");
	return 0;
}

static int read_channel(const struct adc_dt_spec *ch, struct analog_reading *out)
{
	int16_t buf;
	struct adc_sequence seq = {
		.buffer      = &buf,
		.buffer_size = sizeof(buf),
	};
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	ret = adc_sequence_init_dt(ch, &seq);
	if (ret < 0) {
		return ret;
	}

	ret = adc_read_dt(ch, &seq);
	if (ret < 0) {
		return ret;
	}

	out->raw = (int32_t)buf;
	if (out->raw < 0) {
		out->raw = 0;
	} else if (out->raw > ADC_FULL_SCALE) {
		out->raw = ADC_FULL_SCALE;
	}

	out->percent = (out->raw * 100) / ADC_FULL_SCALE;

	return 0;
}

static void analog_self_test(void)
{
	struct analog_reading r;

	LOG_INF("Analog self-test start");
	if (analog_module_read_ldr(&r) == 0) {
		LOG_INF("LDR:   raw=%d (%d%%)", (int)r.raw, (int)r.percent);
	} else {
		LOG_WRN("LDR read failed");
	}
	if (analog_module_read_water(&r) == 0) {
		LOG_INF("Water: raw=%d (%d%%)", (int)r.raw, (int)r.percent);
	} else {
		LOG_WRN("Water level read failed");
	}
	LOG_INF("Analog self-test done");
}

int analog_module_read_ldr(struct analog_reading *out)
{
	return read_channel(&ldr_ch, out);
}

int analog_module_read_water(struct analog_reading *out)
{
	return read_channel(&water_ch, out);
}
