#include "modules/dht22_module.h"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dht22_module, LOG_LEVEL_INF);

static const struct device *dht = DEVICE_DT_GET(DT_NODELABEL(dht22_sensor));

static void dht22_self_test(void);

int dht22_module_init(void)
{
	if (!device_is_ready(dht)) {
		LOG_ERR("DHT22 not ready (check GPIO4, pull-up to 3.3V)");
		return -ENODEV;
	}

	/* dht22_self_test(); */
	LOG_INF("DHT22 ready on GPIO21");
	return 0;
}

static void dht22_self_test(void)
{
	struct dht22_reading r;

	LOG_INF("DHT22 self-test start");
	if (dht22_module_read(&r) == 0) {
		LOG_INF("DHT22: temp=%d.%06d C  humid=%d.%06d %%",
			r.temp_1, r.temp_2, r.humid_1, r.humid_2);
	} else {
		LOG_WRN("DHT22: first read failed (sensor warm-up, OK)");
	}
	LOG_INF("DHT22 self-test done");
}

int dht22_module_read(struct dht22_reading *out)
{
	struct sensor_value temp, humidity;
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	if (!device_is_ready(dht)) {
		return -ENODEV;
	}

	ret = sensor_sample_fetch(dht);
	if (ret < 0) {
		return ret;
	}

	sensor_channel_get(dht, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	sensor_channel_get(dht, SENSOR_CHAN_HUMIDITY, &humidity);

	out->temp_1  = temp.val1;     out->temp_2  = temp.val2;
	out->humid_1 = humidity.val1; out->humid_2 = humidity.val2;

	return 0;
}
