#ifndef MODULES_MPU6050_MODULE_H_
#define MODULES_MPU6050_MODULE_H_

#include <stdint.h>

struct mpu6050_reading {
	/* Each field is val1 + val2/1e6 (val2 shares sign with val1) */
	int32_t accel_x_1, accel_x_2; /* m/s² */
	int32_t accel_y_1, accel_y_2;
	int32_t accel_z_1, accel_z_2;
	int32_t gyro_x_1,  gyro_x_2;  /* rad/s */
	int32_t gyro_y_1,  gyro_y_2;
	int32_t gyro_z_1,  gyro_z_2;
	int32_t temp_1,    temp_2;     /* °C */
};

int mpu6050_module_init(void);
int mpu6050_module_read(struct mpu6050_reading *out);

#endif /* MODULES_MPU6050_MODULE_H_ */
