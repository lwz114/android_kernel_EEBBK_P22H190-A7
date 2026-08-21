#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>

#include "gxcodec_i2c.h"
#include "gxcodec_spi.h"
#include "gxcodec_upgrade.h"
#include "adpcm.h"

#define DRIVER_VERSION      "v0.0.1"

#define GET_WAKEUP_ID      0x0800
#define GET_FW_VERSION     0x0801
#define GET_MIC_STATUS     0x0802
#define SET_CHIP_POWER     0x0803
#define UPGRADE_FIRMWARE   0x0888

#define CHIP_POWER_BY_GPIO
#ifdef CHIP_POWER_BY_GPIO
#define CHIP_POWER_ENABLE_SET_VALUE     0
struct gpio_desc *power_gpio_desc;
#endif

#define PCM_BUFFER_SIZE             (1024*64)
#define ENCODEC_DATA_PACKET_SIZE    (PCM_BUFFER_SIZE / 4)
#define PCM_VERIFICATION_FLAG       (0x38303032)
#define PCM_VERIFICATION_FLAG_SIZE  (16)

typedef enum {
    CHIP_POWER_STATUS_ENABLE,
    CHIP_POWER_STATUS_DISABLE,
    CHIP_POWER_STATUS_RESET,
} CHIP_POWER_STATUS;

struct img_info {
    unsigned char *data;
    unsigned int size;
};
struct img_info img_info_t;

struct gpio_desc *wakeup_gpio_desc;
struct work_struct sv_work;
struct mutex p_lock;
static int buffering = 0; // 0 代表当前没有缓存音频，1 代表有缓存音频可供读取

unsigned char *pcm_buf = NULL;
unsigned char *adpcm_buf = NULL;

// #define GXCODEC_DEBUG
#ifdef GXCODEC_DEBUG
static unsigned int _make_sum(unsigned char *s, unsigned int l)
{
    unsigned int sum = 0;
    for (int i = 0; i < l; i++) {
        sum += s[i];
    }
    pr_info("size %u sum: %u\n", l, sum);
    return sum;
}

static void _printk_buff(unsigned char *buf)
{
    int i;
    for (i = ENCODEC_DATA_PACKET_SIZE+PCM_VERIFICATION_FLAG_SIZE - 6; i < ENCODEC_DATA_PACKET_SIZE+PCM_VERIFICATION_FLAG_SIZE; i++)
        pr_info("kbuf[%d] = 0x%x\n", i, buf[i]);
    pr_info("=======================\n");
    _make_sum(buf, ENCODEC_DATA_PACKET_SIZE+PCM_VERIFICATION_FLAG_SIZE);
}
#endif

static void enable_chip(void)
{
#ifdef CHIP_POWER_BY_GPIO
    if (!power_gpio_desc)
        return;

    pr_info("enable chip\n");
    gpiod_set_value(power_gpio_desc, CHIP_POWER_ENABLE_SET_VALUE);
#else
    /*
       code...
    */
#endif
}

static void disable_chip(void)
{
#ifdef CHIP_POWER_BY_GPIO
    if (!power_gpio_desc)
        return;

    pr_info("disable chip\n");
    gpiod_set_value(power_gpio_desc, !CHIP_POWER_ENABLE_SET_VALUE);
#else
    /*
       code...
    */
#endif
}

static void reboot_chip(void)
{
#ifdef CHIP_POWER_BY_GPIO
    if (!power_gpio_desc)
        return;

    pr_info("reboot chip ...\n");
    gpiod_set_value(power_gpio_desc, !CHIP_POWER_ENABLE_SET_VALUE);
    msleep(100);
    gpiod_set_value(power_gpio_desc, CHIP_POWER_ENABLE_SET_VALUE);
#else
    /*
       code...
    */
#endif
}

static void i2c_set_power_status(CHIP_POWER_STATUS s)
{
    switch (s)
    {
    case CHIP_POWER_STATUS_ENABLE:
        enable_chip();
        break;
    case CHIP_POWER_STATUS_DISABLE:
        disable_chip();
        break;
    case CHIP_POWER_STATUS_RESET:
        reboot_chip();
        break;

    default:
        break;
    }
}


static int i2c_get_event_id(void)
{
#define HW_I2C_REG_WAKEUP_ID       0xA0
#define HW_I2C_REG_ISR_CONFIRM     0xC4
#define ISR_CONFIRM_CLEAR_EVENT    0x10
    int event_id = i2c_read_gxcodec_reg(HW_I2C_REG_WAKEUP_ID);
    pr_info("R(%X) get %d\n", HW_I2C_REG_WAKEUP_ID, event_id);
    if (event_id >= 0)
        i2c_write_gxcodec_reg(HW_I2C_REG_ISR_CONFIRM, ISR_CONFIRM_CLEAR_EVENT);
    return event_id;
}

static int i2c_get_firmware_version(void)
{
#define HW_I2C_REG_ISR_CONFIRM      0xC4
#define HW_I2C_REG_MAIN_VER         0xA0
#define HW_I2C_REG_SECOND_VER       0xA4
#define HW_I2C_REG_CURRECT_VER      0xA8
#define HW_I2C_REG_BUILD_VER        0xAC
#define ISR_GET_SOFT_VERSTION       0x68
    int version = 0;
    int ret = i2c_write_gxcodec_reg(HW_I2C_REG_ISR_CONFIRM, ISR_GET_SOFT_VERSTION);
    if (ret >= 0) {
        msleep(100);
        char main_ver       = i2c_read_gxcodec_reg(HW_I2C_REG_MAIN_VER);
        char second_ver     = i2c_read_gxcodec_reg(HW_I2C_REG_SECOND_VER);
        char currect_ver    = i2c_read_gxcodec_reg(HW_I2C_REG_CURRECT_VER);
        char build_ver      = i2c_read_gxcodec_reg(HW_I2C_REG_BUILD_VER);
        version |= main_ver << 24;
        version |= second_ver << 16;
        version |= currect_ver << 8;
        version |= build_ver << 0;
    }
    pr_info("Firmware Version: %04x\n", version);

    return version;
}

static char i2c_get_mic_status(void)
{
#define HW_I2C_REG_MIC_STATUS       0xA4
#define HW_I2C_REG_ISR_CONFIRM      0xC4
#define ISR_CONFIRM_GET_MIC         0x20
#define ISR_CONFIRM_CLEAR_MIC       0x21
    int mic_status = 0;
    i2c_write_gxcodec_reg(HW_I2C_REG_ISR_CONFIRM, ISR_CONFIRM_GET_MIC);
    msleep(100);
    mic_status = i2c_read_gxcodec_reg(HW_I2C_REG_MIC_STATUS);
    pr_info("R(%X) get %d\n", HW_I2C_REG_WAKEUP_ID, mic_status);
    i2c_write_gxcodec_reg(HW_I2C_REG_ISR_CONFIRM, ISR_CONFIRM_CLEAR_MIC);
    return mic_status;
}

static int misc_dev_open(struct inode *inode, struct file *file)
{
    pr_info("misc_dev_open() is called.\n");
    return 0;
}

static int misc_dev_close(struct inode *inode, struct file *file)
{
    pr_info("misc_dev_close() is called.\n");
    return 0;
}

static long misc_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int res;
    pr_info("misc_dev_ioctl() is called. cmd = %x, arg = %ld\n", cmd, arg);

    switch(cmd) {
    case GET_WAKEUP_ID:
    {
        int event_id = i2c_get_event_id();
        res = copy_to_user((void *)arg, (void *)&event_id, sizeof(event_id));
        break;
    }

    case GET_FW_VERSION:
    {
        int firmware_version = i2c_get_firmware_version();
        res = copy_to_user((void *)arg, (void *)&firmware_version, sizeof(firmware_version));
        break;
    }

    case GET_MIC_STATUS:
    {
        char mic_status = i2c_get_mic_status();
        res = copy_to_user((void *)arg, (void *)&mic_status, sizeof(mic_status));
        break;
    }

    case SET_CHIP_POWER:
    {
        i2c_set_power_status(arg);
        res = 0;
        break;
    }

    case UPGRADE_FIRMWARE:
    {
        if (copy_from_user(&img_info_t, (void *)arg, sizeof(img_info_t))) {
            pr_err("copy_from_user failed!\n");
            return -1;
        }
        pr_info("D:%p, L:%d\n", img_info_t.data, img_info_t.size);
        unsigned char *firmware = kmalloc(img_info_t.size, GFP_KERNEL);
        if (!firmware) {
            pr_err("Can't allocate param buffer (size = %d)!\n", img_info_t.size);
            return -1;
        }
        if (copy_from_user(firmware, img_info_t.data, img_info_t.size)) {
            pr_err("copy_from_user failed!\n");
            return -1;
        }

        res = gxcodec_upgrade_firmware(reboot_chip, firmware, img_info_t.size);
        kfree(firmware);
        break;
    }

    default:
        pr_info("invalid cmd\n");
        break;
    }

    return res;
}

static ssize_t misc_dev_read(struct file *file, char __user *buf, size_t count_want, loff_t *f_pos)
{
    int ret = 0;
    int count = 100;

    pr_info("misc_dev_read() is called\n");

    if (count_want != PCM_BUFFER_SIZE)
    {
        pr_info("count_want must be 1024*64 bytes\n");
        return 0;
    }

    while(count > 0)
    {
        count--;
        mutex_lock(&p_lock);

        if (buffering == 0)
        {
            mutex_unlock(&p_lock);
            msleep(10);
        }
        else if (buffering == 1)
        {
            goto END;
        }
    }

    pr_info("read time out\n");
    return 0;

END:
    if (copy_to_user((void *)buf, (void *)pcm_buf, PCM_BUFFER_SIZE))
    {
        ret = 0;
    }
    else
    {
        ret = PCM_BUFFER_SIZE;
        buffering = 0;
    }
    mutex_unlock(&p_lock);

    return ret;
}

static ssize_t misc_dev_write(struct file *file, const char __user *buf, size_t count_want, loff_t *f_pos)
{
    pr_info("misc_dev_write() is called\n");

    return 0;
}

static const struct file_operations misc_dev_fops = {
    .owner = THIS_MODULE,
    .open = misc_dev_open,
    .release = misc_dev_close,
    .unlocked_ioctl = misc_dev_ioctl,
    .read = misc_dev_read,
    .write = misc_dev_write,
};

static struct miscdevice gx_miscdevice = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "gxcodec",
    .fops = &misc_dev_fops,
};

static irqreturn_t active_irq(int irq, void *dev_id) {

    //struct platform_device *pdev = (struct platform_device *)dev_id;

    pr_info("======irq:%d======\n", irq);

    schedule_work(&sv_work);
    return IRQ_HANDLED;
}

static void gx_sv_work(struct work_struct *work)
{
    int res = 0;
    short *in = NULL;
    short *out = NULL;
    unsigned char *p_verif_flag = NULL;
    int len = 0;
    unsigned int verif_flag = 0;

    pr_info("gx_sv_work is called\n");

    mutex_lock(&p_lock);
#if 1
    //spi master read
    res = spi_read_gxcodec_data(adpcm_buf, ENCODEC_DATA_PACKET_SIZE+PCM_VERIFICATION_FLAG_SIZE);
    if (res != ENCODEC_DATA_PACKET_SIZE+PCM_VERIFICATION_FLAG_SIZE)
    {
        pr_err("Res is %d, but recv size %d\n", res, ENCODEC_DATA_PACKET_SIZE+PCM_VERIFICATION_FLAG_SIZE);
        mutex_unlock(&p_lock);
        return;
    }

    //check verification flag
    p_verif_flag = &adpcm_buf[0] + ENCODEC_DATA_PACKET_SIZE;
    verif_flag |= p_verif_flag[12] << 24;
    verif_flag |= p_verif_flag[13] << 16;
    verif_flag |= p_verif_flag[14] << 8;
    verif_flag |= p_verif_flag[15] << 0;
    if (verif_flag != PCM_VERIFICATION_FLAG) {
        pr_err("check verification flag failed! PCM_VERIFICATION_FLAG is 0x%x, but get 0x%x\n", PCM_VERIFICATION_FLAG, verif_flag);
        goto out;
    }
#ifdef GXCODEC_DEBUG
    _printk_buff(adpcm_buf);
#endif
    in = (short *)adpcm_buf;
    len = PCM_BUFFER_SIZE / 2;
    out = (short *)pcm_buf;
    AdpcmClearDecode();
    Adpcm2Pcm (in, out, len);
#endif
    buffering = 1;

out:
    mutex_unlock(&p_lock);
}

/* Add probe() function */
static int __init gxcodec_platform_probe(struct platform_device *pdev)
{
    int ret_val, irq;

    pr_info("gxcodec_platform_probe() function is called.\n");
    pr_info("gxcodec driver version - %s\n", DRIVER_VERSION);

    adpcm_buf = devm_kmalloc(&pdev->dev, ENCODEC_DATA_PACKET_SIZE+PCM_VERIFICATION_FLAG_SIZE, GFP_KERNEL);
    if (!adpcm_buf) {
        pr_err("Can't allocate param buffer (size = %d)!\n", ENCODEC_DATA_PACKET_SIZE+PCM_VERIFICATION_FLAG_SIZE);
        return -1;
    }
    pcm_buf = devm_kmalloc(&pdev->dev, PCM_BUFFER_SIZE, GFP_KERNEL);
    if (!pcm_buf) {
        pr_err("Can't allocate param buffer (size = %d)!\n", PCM_BUFFER_SIZE);
        return -1;
    }

    mutex_init(&p_lock); // 对读取缓存音频进行包含的锁

    ret_val = misc_register(&gx_miscdevice); // 杂项设备驱动注册

    if (ret_val != 0) {
        pr_err("could not register the misc device mydev\n");
        return ret_val;
    }

    pr_info("mydev: got minor %i\n",gx_miscdevice.minor);

    ret_val = gxcodec_i2c_init(); // I2C driver
    if (ret_val) {
        pr_err("failed to register gxcodec i2c driver: %d\n", ret_val);
        goto err_misc;
    }

    ret_val = gxcodec_spi_init(); // SPI driver
    if (ret_val) {
        pr_err("failed to register gxcodec spi driver: %d\n", ret_val);
        goto err_i2c;
    }

#ifdef CONFIG_SND_SOC_DBMDX
    /*
     * A3 routes the same reset/power and wakeup IRQ GPIOs to DBMDX and
     * GX8002. Let the DBMDX codec driver own those pins; GXCODEC still
     * exposes its misc/I2C interface for vendor userspace.
     */
    pr_info("DBMDX enabled; skip shared gxcodec GPIO/IRQ ownership\n");
    return 0;
#endif

    // reset chip
#ifdef CHIP_POWER_BY_GPIO
    power_gpio_desc = devm_gpiod_get_optional(&pdev->dev, "power", GPIOD_OUT_LOW);
    if (IS_ERR(power_gpio_desc)) {
        ret_val = PTR_ERR(power_gpio_desc);
        pr_err("failed to get power gpio: %d\n", ret_val);
        goto err_spi;
    }
#endif
    reboot_chip();

 //  初始化录音工作任务
    INIT_WORK(&sv_work, gx_sv_work);

//  唤醒中断源注册
    wakeup_gpio_desc = devm_gpiod_get_optional(&pdev->dev, "wakeup", GPIOD_IN);
    if (!wakeup_gpio_desc)
        wakeup_gpio_desc = devm_gpiod_get_optional(&pdev->dev, "irq", GPIOD_IN);
    if (IS_ERR(wakeup_gpio_desc)) {
        ret_val = PTR_ERR(wakeup_gpio_desc);
        pr_err("failed to get wakeup/irq gpio: %d\n", ret_val);
        goto err_spi;
    }
    if (!wakeup_gpio_desc) {
        pr_err("failed to find wakeup-gpio or irq-gpio\n");
        ret_val = -ENODEV;
        goto err_spi;
    }

    irq = gpiod_to_irq(wakeup_gpio_desc);
    if (irq < 0) {
        pr_err("%s get irq error: %d.\n", pdev->name, irq);
        ret_val = irq;
        goto err_spi;
    }

    pr_info("%d %s get irq.\n", irq, pdev->name);

    ret_val = devm_request_irq(&pdev->dev, irq, active_irq,IRQF_TRIGGER_FALLING, "active", pdev);
    if (ret_val) {
        pr_err("failed to devm_request_irq irq error:%d\n", ret_val);
        goto err_spi;
    }

    return 0;

err_spi:
    gxcodec_spi_deinit();
err_i2c:
    gxcodec_i2c_deinit();
err_misc:
    misc_deregister(&gx_miscdevice);
    return ret_val;
}

/* Add remove() function */
static int __exit gxcodec_platform_remove(struct platform_device *pdev)
{
    pr_info("gxcodec_platform_remove() function is called.\n");

    flush_scheduled_work();

    gxcodec_spi_deinit();

    gxcodec_i2c_deinit();

    misc_deregister(&gx_miscdevice);
    return 0;
}

/* Declare a list of devices supported by the driver */
static const struct of_device_id gxcodec_of_match[] = {
    { .compatible = "nationalchip,gxcodec", 0},
    {}
};

MODULE_DEVICE_TABLE(of, gxcodec_of_match);

/* Define platform driver structure */
static struct platform_driver gxcodec_platform_driver = {
    .probe = gxcodec_platform_probe,
    .remove = gxcodec_platform_remove,
    .driver = {
        .name = "gxcodec",
        .of_match_table = gxcodec_of_match,
        .owner = THIS_MODULE,
    }
};

/* Register our platform driver */
module_platform_driver(gxcodec_platform_driver);

MODULE_DESCRIPTION("Nationalchip 8002 driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("GX8002");
MODULE_VERSION("1.0.0");
