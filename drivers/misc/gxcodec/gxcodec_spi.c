#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/gpio.h>

#include "gxcodec_spi.h"

static struct spi_device *s_gxcodec_spi_client;

unsigned int spi_write_gxcodec_data(unsigned char *buf, unsigned int len)
{
    unsigned int ret = 0;

    if (!s_gxcodec_spi_client)
        return -ENODEV;

    ret = spi_write(s_gxcodec_spi_client, buf, len);

    if (ret != 0) {
        printk(KERN_INFO "%s(): error %d writing SR\n",
                __func__, ret);
        return ret;
    } else
        return len;
}

unsigned int spi_read_gxcodec_data(unsigned char *buf, unsigned int len)
{

    int i, ret;

    if (!s_gxcodec_spi_client)
        return -ENODEV;

    ret =  spi_read(s_gxcodec_spi_client, buf, len);
    if (ret < 0) {
        printk("%s: spi_read failed\n", __func__);
        i = -EIO;
        goto out;
    }

    return len;
out:
    return i;
}

static int gxcodec_spi_probe(struct spi_device *client)
{
    printk(KERN_INFO "gxcodec_spi_probe!\n");
    printk(KERN_INFO "max_speed_hz | %d\n", client->max_speed_hz);
    printk(KERN_INFO "chip_select  | %d\n", client->chip_select);
    printk(KERN_INFO "mode         | %d\n", client->mode);
    printk(KERN_INFO "bits_per_word| %d\n", client->bits_per_word);

    s_gxcodec_spi_client = client;
    return 0;
}

static int gxcodec_spi_remove(struct spi_device *client)
{
    printk(KERN_INFO "gxcodec_spi_remove!\n");
    return 0;
}

static const struct of_device_id gxcodec_spi_of_match[] = {
    { .compatible = "nationalchip,gxcodec-spi", },
    {},
};
MODULE_DEVICE_TABLE(of, gxcodec_spi_of_match);

static const struct spi_device_id gxcodec_spi_id[] = {
    { "gxcodec-spi", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, gxcodec_spi_id);

static struct spi_driver gxcodec_spi_driver = {
    .driver = {
        .name = "gxcodec-spi",
        .bus    = &spi_bus_type,
        .owner = THIS_MODULE,
        .of_match_table = gxcodec_spi_of_match,
    },
    .probe =    gxcodec_spi_probe,
    .remove =   gxcodec_spi_remove,
    .id_table = gxcodec_spi_id,
};


int gxcodec_spi_init(void)
{
    return spi_register_driver(&gxcodec_spi_driver);
}

int gxcodec_spi_deinit(void)
{
    spi_unregister_driver(&gxcodec_spi_driver);
    return 0;
}
