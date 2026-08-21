/*
 * FocalTech Touchscreen I2C transport — ported from focaltech_spi.c
 * for the EEBBK A3 (ums512_1h180) tablet (FTS on i2c-3 @0x38).
 */
#include <linux/i2c.h>
#include <linux/mutex.h>
#include "focaltech_common.h"
#include "focaltech_core.h"

static struct i2c_client *fts_i2c_client;
static DEFINE_MUTEX(fts_i2c_mutex);

int fts_write(u8 *writebuf, u32 writelen)
{
	int ret;

	if (!fts_i2c_client)
		return -EINVAL;

	mutex_lock(&fts_i2c_mutex);
	ret = i2c_master_send(fts_i2c_client, writebuf, writelen);
	mutex_unlock(&fts_i2c_mutex);

	return (ret == writelen) ? 0 : ((ret < 0) ? ret : -EIO);
}

int fts_read(u8 *cmd, u32 cmdlen, u8 *data, u32 datalen)
{
	int ret;

	if (!fts_i2c_client)
		return -EINVAL;

	mutex_lock(&fts_i2c_mutex);
	if (cmdlen > 0) {
		ret = i2c_master_send(fts_i2c_client, cmd, cmdlen);
		if (ret != cmdlen) {
			mutex_unlock(&fts_i2c_mutex);
			return (ret < 0) ? ret : -EIO;
		}
	}

	if (datalen > 0) {
		ret = i2c_master_recv(fts_i2c_client, data, datalen);
		if (ret != datalen) {
			mutex_unlock(&fts_i2c_mutex);
			return (ret < 0) ? ret : -EIO;
		}
	}
	mutex_unlock(&fts_i2c_mutex);

	return 0;
}

int fts_write_reg(u8 addr, u8 value)
{
	u8 buf[2] = { addr, value };

	return fts_write(buf, 2);
}

int fts_read_reg(u8 addr, u8 *value)
{
	return fts_read(&addr, 1, value, 1);
}

int fts_bus_init(struct fts_ts_data *ts_data)
{
	FTS_INFO("I2C bus init");
	if (!ts_data || !ts_data->client)
		return -EINVAL;

	fts_i2c_client = ts_data->client;
	return 0;
}

int fts_bus_exit(struct fts_ts_data *ts_data)
{
	fts_i2c_client = NULL;
	return 0;
}
