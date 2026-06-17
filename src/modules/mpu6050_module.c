#include "modules/mpu6050_module.h"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mpu6050_module, LOG_LEVEL_INF);

static const struct device *mpu = DEVICE_DT_GET(DT_NODELABEL(mpu6050));

int mpu6050_module_init(void)
{
	if (!device_is_ready(mpu)) {
		LOG_ERR("MPU6050 not ready (check SDA=GPIO1, SCL=GPIO2, addr=0x68)");
		return -ENODEV;
	}

	LOG_INF("MPU6050 ready");
	return 0;
}

int mpu6050_module_read(struct mpu6050_reading *out)
{
	struct sensor_value accel[3], gyro[3], temp;
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	if (!device_is_ready(mpu)) {
		return -ENODEV;
	}

	ret = sensor_sample_fetch(mpu);
	if (ret < 0) {
		return ret;
	}

	sensor_channel_get(mpu, SENSOR_CHAN_ACCEL_X, &accel[0]);
	sensor_channel_get(mpu, SENSOR_CHAN_ACCEL_Y, &accel[1]);
	sensor_channel_get(mpu, SENSOR_CHAN_ACCEL_Z, &accel[2]);
	sensor_channel_get(mpu, SENSOR_CHAN_GYRO_X,  &gyro[0]);
	sensor_channel_get(mpu, SENSOR_CHAN_GYRO_Y,  &gyro[1]);
	sensor_channel_get(mpu, SENSOR_CHAN_GYRO_Z,  &gyro[2]);
	sensor_channel_get(mpu, SENSOR_CHAN_DIE_TEMP, &temp);

	out->accel_x_1 = accel[0].val1; out->accel_x_2 = accel[0].val2;
	out->accel_y_1 = accel[1].val1; out->accel_y_2 = accel[1].val2;
	out->accel_z_1 = accel[2].val1; out->accel_z_2 = accel[2].val2;
	out->gyro_x_1  = gyro[0].val1;  out->gyro_x_2  = gyro[0].val2;
	out->gyro_y_1  = gyro[1].val1;  out->gyro_y_2  = gyro[1].val2;
	out->gyro_z_1  = gyro[2].val1;  out->gyro_z_2  = gyro[2].val2;
	out->temp_1    = temp.val1;      out->temp_2    = temp.val2;

	return 0;
}
