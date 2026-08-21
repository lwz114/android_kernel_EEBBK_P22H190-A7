#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>

#include "gxcodec_i2c.h"

#define GXCODEC_I2C_DATA_ADDRESS    0x36 //boot i2c addr, decision: GPIO2 low->0x35, GPIO2 high->0x36

static struct i2c_client *s_gxcodec_i2c_client;

int i2c_read_gxcodec_reg(unsigned char reg)
{
    if (!s_gxcodec_i2c_client)
        return -ENODEV;

    return i2c_smbus_read_byte_data(s_gxcodec_i2c_client, reg);
}

int i2c_write_gxcodec_reg(unsigned char reg, unsigned char val)
{
    if (!s_gxcodec_i2c_client)
        return -ENODEV;

    return i2c_smbus_write_byte_data(s_gxcodec_i2c_client, reg, val);
}

int i2c_read_gxcodec_data(char *buf, int count)
{
    if (!s_gxcodec_i2c_client)
        return -ENODEV;

    struct i2c_adapter *adap=s_gxcodec_i2c_client->adapter;  // 获取adapter信息
    struct i2c_msg msg;                        // 定义一个临时的数据包
    int ret;

    msg.addr = GXCODEC_I2C_DATA_ADDRESS;                   // 将从机地址写入数据包
    msg.flags = s_gxcodec_i2c_client->flags & I2C_M_TEN;     // 将从机标志并入数据包
    msg.flags |= I2C_M_RD;                     // 将此次通信的标志并入数据包
    msg.len = count;                           // 将此次接收的数据字节数写入数据包
    msg.buf = buf;

    ret = i2c_transfer(adap, &msg, 1);         // 调用平台接口接收数据

    /* If everything went ok (i.e. 1 msg transmitted), return #bytes
       transmitted, else error code. */
    return (ret == 1) ? count : ret;           // 如果接收成功就返回字节数
}

int i2c_write_gxcodec_data(const char *buf, int count)
{
    if (!s_gxcodec_i2c_client)
        return -ENODEV;

    int ret;
    struct i2c_adapter *adap = s_gxcodec_i2c_client->adapter;  // 获取adapter信息
    struct i2c_msg msg;                        // 定义一个临时的数据包

    msg.addr = GXCODEC_I2C_DATA_ADDRESS;                   // 将从机地址写入数据包
    msg.flags = s_gxcodec_i2c_client->flags & I2C_M_TEN;     // 将从机标志并入数据包
    msg.len = count;                           // 将此次发送的数据字节数写入数据包
    msg.buf = (char *)buf;                     // 将发送数据指针写入数据包

    ret = i2c_transfer(adap, &msg, 1);         // 调用平台接口发送数据

    /* If everything went ok (i.e. 1 msg transmitted), return #bytes
       transmitted, else error code. */
    return (ret == 1) ? count : ret;           // 如果发送成功就返回字节数
}

//--------------------------------------------------------------------------------------//
static int gxcodec_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    printk(KERN_INFO "gxcodec_i2c_probe!  %x\n", client->addr);
    s_gxcodec_i2c_client = client;
    return 0;
}

static int gxcodec_i2c_remove(struct i2c_client *client)
{
    printk(KERN_INFO "gxcodec_i2c_remove!\n");
    return 0;
}

static const struct of_device_id gxcodec_i2c_of_match[] = {
    { .compatible = "nationalchip,gxcodec-i2c",},
    {},
};
MODULE_DEVICE_TABLE(of, gxcodec_i2c_of_match);

static const struct i2c_device_id gxcodec_i2c_id[] = {
    { "gxcodec-i2c", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, gxcodec_i2c_id);

static struct i2c_driver gxcodec_i2c_driver = {
    .driver = {
        .name = "gxcodec-i2c",
        .owner = THIS_MODULE,
        .of_match_table = gxcodec_i2c_of_match,
    },
    .probe = gxcodec_i2c_probe,
    .remove = gxcodec_i2c_remove,
    .id_table = gxcodec_i2c_id,
};

int gxcodec_i2c_init(void)
{
    return i2c_add_driver(&gxcodec_i2c_driver);
}

int gxcodec_i2c_deinit(void)
{
    i2c_del_driver(&gxcodec_i2c_driver);
    return 0;
}
