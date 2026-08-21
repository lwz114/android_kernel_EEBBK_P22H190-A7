#ifndef _GXCODEC_SPI_H_
#define _GXCODEC_SPI_H_

int gxcodec_spi_init(void);
int gxcodec_spi_deinit(void);

unsigned int spi_write_gxcodec_data(unsigned char *buf, unsigned int len);
unsigned int spi_read_gxcodec_data(unsigned char *buf, unsigned int len);

#endif