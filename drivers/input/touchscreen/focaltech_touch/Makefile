# Makefile for the focaltech touchscreen drivers.
# EEBBK A3: single module (focaltech_touch.ko) - the per-object obj- lines
# would split the driver into many modules when built as =m.

obj-$(CONFIG_TOUCHSCREEN_FTS)	+=  focaltech_touch.o

focaltech_touch-objs := focaltech_core.o focaltech_ex_fun.o \
	focaltech_ex_mode.o focaltech_gesture.o focaltech_esdcheck.o \
	focaltech_point_report_check.o focaltech_flash.o focaltech_i2c.o \
	focaltech_test/focaltech_test.o focaltech_test/focaltech_test_ini.o \
	focaltech_test/supported_ic/focaltech_test_ft8006s_ab.o \
	focaltech_test/supported_ic/focaltech_test_ft8722.o
