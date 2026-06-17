#include "modules/vl6180x_module.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vl6180x_module, LOG_LEVEL_INF);

#define VL6180X_ADDR              0x29

/* Identification */
#define REG_IDENTIFICATION_ID     0x0000  /* expected: 0xB4 */
#define REG_FRESH_OUT_OF_RESET    0x0016

/* Range control */
#define REG_SYSRANGE_START        0x0018
#define REG_RESULT_RANGE_STATUS   0x004D
#define REG_RESULT_INTR_STATUS    0x004F  /* RESULT__INTERRUPT_STATUS_GPIO */
#define REG_RESULT_RANGE_VAL      0x0062
#define REG_INTR_CLEAR            0x0015

static const struct device *i2c_bus = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static int reg_write(uint16_t reg, uint8_t val)
{
	uint8_t buf[3] = { reg >> 8, reg & 0xFF, val };

	return i2c_write(i2c_bus, buf, sizeof(buf), VL6180X_ADDR);
}

static int reg_read(uint16_t reg, uint8_t *val)
{
	uint8_t addr[2] = { reg >> 8, reg & 0xFF };

	return i2c_write_read(i2c_bus, VL6180X_ADDR, addr, sizeof(addr), val, 1);
}

static int vl6180x_mandatory_init(void)
{
	/* Mandatory private settings — ST AN4545 p.24 */
	static const struct { uint16_t reg; uint8_t val; } settings[] = {
		{0x0207, 0x01}, {0x0208, 0x01}, {0x0096, 0x00}, {0x0097, 0xfd},
		{0x00e3, 0x00}, {0x00e4, 0x04}, {0x00e5, 0x02}, {0x00e6, 0x01},
		{0x00e7, 0x03}, {0x00f5, 0x02}, {0x00d9, 0x05}, {0x00db, 0xce},
		{0x00dc, 0x03}, {0x00dd, 0xf8}, {0x009f, 0x00}, {0x00a3, 0x3c},
		{0x00b7, 0x00}, {0x00bb, 0x3c}, {0x00b2, 0x09}, {0x00ca, 0x09},
		{0x0198, 0x01}, {0x01b0, 0x17}, {0x01ad, 0x00}, {0x00ff, 0x05},
		{0x0100, 0x05}, {0x0199, 0x05}, {0x01a6, 0x1b}, {0x01ac, 0x3e},
		{0x01a7, 0x1f}, {0x0030, 0x00},
	};

	for (int i = 0; i < ARRAY_SIZE(settings); i++) {
		int ret = reg_write(settings[i].reg, settings[i].val);

		if (ret < 0) {
			return ret;
		}
	}

	reg_write(REG_FRESH_OUT_OF_RESET, 0x00);
	return 0;
}

static void vl6180x_public_config(void)
{
	reg_write(0x0011, 0x10); /* enable new-sample-ready polling */
	reg_write(0x010a, 0x30); /* avg sample period */
	reg_write(0x003f, 0x46); /* light/dark gain */
	reg_write(0x0031, 0xFF); /* range auto-calibration period */
	reg_write(0x0040, 0x63); /* ALS integration 100ms */
	reg_write(0x002e, 0x01); /* temperature calibration */
	reg_write(0x001b, 0x09); /* ranging inter-measurement 100ms */
	reg_write(0x003e, 0x31); /* ALS inter-measurement 500ms */
	reg_write(0x0014, 0x04); /* range interrupt: new sample ready */
}

int vl6180x_module_read_range(uint8_t *range_mm)
{
	uint8_t status;
	int ret;
	int timeout = 50;

	/* Start single-shot range measurement */
	ret = reg_write(REG_SYSRANGE_START, 0x01);
	if (ret < 0) {
		return ret;
	}

	/* Poll until measurement ready (bit 2 of interrupt status) */
	do {
		k_msleep(2);
		ret = reg_read(REG_RESULT_INTR_STATUS, &status);
		if (ret < 0) {
			return ret;
		}
	} while ((status & 0x07) == 0 && --timeout > 0);

	if (timeout == 0) {
		LOG_WRN("VL6180X measurement timeout");
		return -ETIMEDOUT;
	}

	ret = reg_read(REG_RESULT_RANGE_VAL, range_mm);
	if (ret < 0) {
		return ret;
	}

	/* Clear interrupts */
	reg_write(REG_INTR_CLEAR, 0x07);

	return 0;
}


int vl6180x_module_init(void)
{
	uint8_t id;
	uint8_t fresh;
	int ret;

	if (!device_is_ready(i2c_bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	ret = reg_read(REG_IDENTIFICATION_ID, &id);
	if (ret < 0) {
		LOG_ERR("VL6180X not found on I2C (addr=0x29): %d", ret);
		return ret;
	}

	if (id != 0xB4) {
		LOG_ERR("Unexpected chip ID: 0x%02X (expected 0xB4)", id);
		return -ENODEV;
	}

	LOG_INF("VL6180X found (chip ID=0x%02X)", id);

	ret = reg_read(REG_FRESH_OUT_OF_RESET, &fresh);
	if (ret == 0 && fresh == 0x01) {
		ret = vl6180x_mandatory_init();
		if (ret < 0) {
			LOG_ERR("VL6180X mandatory init failed: %d", ret);
			return ret;
		}
		LOG_INF("VL6180X mandatory init done (fresh boot)");
	}

	/* Always apply public config — sets up interrupt on new-sample-ready */
	vl6180x_public_config();

	LOG_INF("VL6180X ready — read range via GET /api/v1/sensors/tof");
	return 0;
}
