#ifndef _GXCODEC_I2C_H_
#define _GXCODEC_I2C_H_

int gxcodec_i2c_init(void);
int gxcodec_i2c_deinit(void);

int i2c_read_gxcodec_reg(unsigned char reg);
int i2c_write_gxcodec_reg(unsigned char reg, unsigned char val);
int i2c_read_gxcodec_data(char *buf, int count);
int i2c_write_gxcodec_data(const char *buf, int count);

#endif