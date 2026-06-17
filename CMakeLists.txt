cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iot_interactive_kit)

target_sources(app PRIVATE
	src/main.c
	src/modules/api_module.c
	src/modules/config_module.c
	src/modules/device_identity.c
	src/modules/provision_module.c
	src/modules/rgb_module.c
	src/modules/buzzer_module.c
	src/modules/mpu6050_module.c
	src/modules/reset_button_module.c
	src/modules/vl6180x_module.c
	src/modules/servo_module.c
	src/modules/wifi_manager.c
)

target_include_directories(app PRIVATE include)
