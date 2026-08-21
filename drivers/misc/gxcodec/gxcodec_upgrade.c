#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/uaccess.h>

#include "gxscpu_boot.h"
#include "gxcodec_i2c.h"
#include "gxcodec_upgrade.h"

#define UPGRADE_DATA_BLOCK_SIZE    256
#define UPGRADE_FLASH_BLOCK_SIZE    (1024*8)
#pragma pack(1)
typedef struct {
    unsigned short chip_id;
    unsigned char  chip_type;
    unsigned char  chip_version;

    unsigned short boot_delay;
    unsigned char  baud_rate;
    unsigned char  reserved_1;

    unsigned int   stage1_size;
    unsigned int   stage2_baud_rate;
    unsigned int   stage2_size;
    unsigned int   stage2_checksum;
    unsigned char  reserved[8];
} boot_header_t;
#pragma pack()
static boot_header_t boot_header;

static unsigned char *temp_buff;

static const unsigned char *boot_buf;
static const unsigned char *p_boot;
static int boot_len = 0;

static const unsigned char *fw_buf;
static const unsigned char *p_fw;
static int fw_len = 0;

static int boot_info_baudrate[] = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};

static void print_bootheader(const boot_header_t *header)
{
    pr_info("chip_id %x\n", header->chip_id);
    pr_info("chip_type %x\n", header->chip_type);
    pr_info("chip_version %x\n", header->chip_version);
    pr_info("boot_delay %x\n", header->boot_delay);
    pr_info("baud_rate %d\n", boot_info_baudrate[header->baud_rate]);
    pr_info("stage1_size %d\n", header->stage1_size);
    pr_info("stage2_baud_rate %d\n", header->stage2_baud_rate);
    pr_info("stage2_size %d\n", header->stage2_size);
    pr_info("stage2_checksum %d\n", header->stage2_checksum);
}

static int parse_bootimg_header(void)
{
    pr_info("reading boot header ...\n");

    memcpy((unsigned char*)&boot_header, boot_buf, sizeof(boot_header));

    boot_header.stage1_size = be32_to_cpu(boot_header.stage1_size);
    boot_header.stage2_baud_rate = be32_to_cpu(boot_header.stage2_baud_rate);
    boot_header.stage2_size = be32_to_cpu(boot_header.stage2_size);
    boot_header.stage2_checksum = be32_to_cpu(boot_header.stage2_checksum);

    print_bootheader(&boot_header);
    return 0;
}

// 如果I2C写失败的话，建议继续重复尝试一定的次数，继续写，止到成功或者超时。目前给的例子只写一次，失败就返回错误
int upgrade_write_data(const unsigned char* buf, int buf_len)
{
    return i2c_write_gxcodec_data(buf, buf_len);
}

int upgrade_wait_reply(unsigned char reg, unsigned char reply, int timeout)
{
    int count = timeout; //ms
    int flag = 0;
    while (count > 0)
    {
        unsigned char data = i2c_read_gxcodec_reg(reg);
        // pr_info("%x\n", data);
        if (data)
        {
            if (data == reply)
            {
                flag = 1;
                break;
            }
        }

        count--;
        msleep(1);
    }

    if (flag == 0)
    {
        pr_info("upgrade_wait_reply timeout\n");
        return 0;
    }
    else
    {
        return 1;
    }
}

static int download_bootimg_stage1(void)
{
    pr_info("start boot stage1 ...\n");
    pr_info("send stage1 size: %d ...\n", boot_header.stage1_size);

    unsigned char *wbuffer = temp_buff;
    p_boot = boot_buf + sizeof(boot_header);
    unsigned char *temp = (unsigned char *)&boot_header.stage1_size;

    wbuffer[0] = temp[0];
    wbuffer[1] = temp[1];
    wbuffer[2] = temp[2];
    wbuffer[3] = temp[3];

    upgrade_write_data(wbuffer, 4);

    int wsize = 0;
    int len;

    wsize = 0;
    while(wsize < boot_header.stage1_size)
    {
        if((wsize + UPGRADE_DATA_BLOCK_SIZE) <= boot_header.stage1_size)
        {
            len = UPGRADE_DATA_BLOCK_SIZE;
        }
        else
        {
            len = boot_header.stage1_size - wsize;
        }

        memcpy(&wbuffer[0], p_boot, len);
        p_boot = p_boot + len;
        upgrade_write_data(wbuffer, len);

        wsize += len;
        pr_info("size = %d length = %d\n", wsize, len+1);
        msleep(100); // 这个延迟可能需要不同平台适配
    }

    pr_info("download size: %d, waiting 0x46 ...\n", wsize);
    if (upgrade_wait_reply(0xA4, 0x46, 5000) == 0)
    {
        pr_info("upgrade_wait_reply error\n");
        return 0;
    }

    pr_info("get 0x46 !\n");
    pr_info("send 0x59, waiting 0x55 ...\n");

    //Sleep(500);

    wbuffer[0] = 0x59;
    if (upgrade_write_data(wbuffer, 1))
    {
        if (upgrade_wait_reply(0xA0, 0x55, 1000))
        {
            pr_info("get 0x55 !\n");
            pr_info("boot stage1 ok !\n");
            return 1;
        }
        else
        {
            pr_info("wait 0x55 error\n");
            return 0;
        }
    }
    else
    {
        return 0;
    }
}

int download_bootimg_stage2(void)
{
    unsigned int checksum = 0;
    unsigned int stage2_size = 0;
    int wsize = 0;
    int len;

    unsigned char *wbuffer = temp_buff;

    pr_info("start boot stage2 ...\n");
    pr_info("send 0xef, waiting 0x78 ...\n");

    wbuffer[0] = 0xef;
    upgrade_write_data(wbuffer, 1);
    if (!upgrade_wait_reply(0xa0, 0x78, 1000))
    {
        pr_info("wait 0x78 err !\n");
        return 0;
    }

    pr_info("get 0x78 !\n");

    stage2_size = boot_header.stage2_size;
    checksum = boot_header.stage2_checksum;

    if (checksum == 0 || stage2_size == 0)
    {
        pr_info("stage2_size or checksum err ! stage2_size = %u, checksum = %u\n", stage2_size, checksum);
        return 0;
    }

    pr_info("send stage2 checksum: %d ...\n", checksum);
    unsigned char *temp = (unsigned char *)&checksum;
    wbuffer[0] = temp[0];
    wbuffer[1] = temp[1];
    wbuffer[2] = temp[2];
    wbuffer[3] = temp[3];
    upgrade_write_data(wbuffer, 4);

    pr_info("send stage2 size: %d ...\n", stage2_size);
    temp = (unsigned char *)&stage2_size;
    wbuffer[0] = temp[0];
    wbuffer[1] = temp[1];
    wbuffer[2] = temp[2];
    wbuffer[3] = temp[3];
    upgrade_write_data(wbuffer, 4);

    pr_info("download boot stage2 ...\n");

    wsize = 0;
    while (wsize < stage2_size)
    {
        if ((wsize + UPGRADE_DATA_BLOCK_SIZE) <= stage2_size)
        {
            len = UPGRADE_DATA_BLOCK_SIZE;
        }
        else
        {
            len = stage2_size - wsize;
        }

        memcpy(&wbuffer[0], p_boot, len);
        p_boot = p_boot + len;
        upgrade_write_data(wbuffer, len);

        wsize += len;
        pr_info("size = %d length = %d\n", wsize, len + 1);
        //Sleep(100); // 这个延迟可能需要不同平台适配
    }

    pr_info("download size: %d, waiting 0x46 ...\n", wsize);
    if (!upgrade_wait_reply(0xa4, 0x46, 1000))
    {
        pr_info("wait 0x46 err !\n");
        return 0;
    }
    pr_info("get 0x46 !\n");

    pr_info("send 0x58, waiting 0x55 ...\n");

    //Sleep(500);

    wbuffer[0] = 0x58;
    if (upgrade_write_data(wbuffer, 1))
    {
        if (upgrade_wait_reply(0xA0, 0x55, 1000))
        {
            pr_info("get 0x55 !\n");
            pr_info("boot stage2 ok !\n");
        }
        else
        {
            pr_info("wait 0x55 error\n");
            return 0;
        }
    }
    else
    {
        return 0;
    }

    return 1;
}

static int download_flashimg(void)
{
    int wsize = 0;
    int offset = 0;
    int flash_block_size = UPGRADE_FLASH_BLOCK_SIZE;
    int len;

    pr_info("start flash image ...\n");
    pr_info("flash image size = %d\n", fw_len);

    if (fw_len == 0)
    {
        pr_info("flash image size err !\n");
        return 0;
    }

    unsigned char *wbuffer = temp_buff;

    pr_info("send flash img offset: %d ...\n", offset);
    unsigned char *temp = (unsigned char *)&offset;
    wbuffer[0] = temp[0];
    wbuffer[1] = temp[1];
    wbuffer[2] = temp[2];
    wbuffer[3] = temp[3];
    upgrade_write_data(wbuffer, 4);// 0x36 write

    pr_info("send flash img size: %d ...\n", fw_len);
    temp = (unsigned char *)&fw_len;
    wbuffer[0] = temp[0];
    wbuffer[1] = temp[1];
    wbuffer[2] = temp[2];
    wbuffer[3] = temp[3];
    upgrade_write_data(wbuffer, 4);// 0x36 write

    pr_info("send flash block size: %d ...\n", flash_block_size);
    temp = (unsigned char *)&flash_block_size;
    wbuffer[0] = temp[0];
    wbuffer[1] = temp[1];
    wbuffer[2] = temp[2];
    wbuffer[3] = temp[3];
    upgrade_write_data(wbuffer, 4);// 0x36 write

    pr_info("waiting 0x43 ...\n");
    if (!upgrade_wait_reply(0xA4, 0x43, 10000))
    {
        pr_info("wait 0x43 err !\n");
        return 0;
    }
    pr_info("get 0x43 !\n");

    pr_info("download flash img ...\n");
    p_fw = fw_buf;

    wsize = 0;
    while (wsize < fw_len)
    {
        if ((wsize + UPGRADE_DATA_BLOCK_SIZE) <= fw_len)
        {
            len = UPGRADE_DATA_BLOCK_SIZE;
        }
        else
        {
            len = fw_len - wsize;
        }

        upgrade_write_data(p_fw + wsize, len);
        wsize += len;
        pr_info("size = %d length = %d\n", wsize, len + 1);
        //Sleep(100);

        if ((wsize % UPGRADE_FLASH_BLOCK_SIZE) == 0 && wsize < fw_len)
        {
            pr_info("download size: %d, waiting 0x44 ...\n", wsize);
            if (!upgrade_wait_reply(0xa4, 0x44, 1000))
            {
                pr_info("wait 0x44 err !\n");
                return 0;
            }
            pr_info("get 0x44 !\n");
        }
    }

    //Sleep(500);
    pr_info("download size: %d, waiting 0x46 ...\n", wsize);
    if (!upgrade_wait_reply(0xa4, 0x46, 1000))
    {
        pr_info("wait 0x46 err !\n");
        return 0;
    }

    pr_info("get 0x46 !\n");
    pr_info("flash image ok !\n");

    return 1;
}

// 我们在100K~400K速率都测试过，8002端是能够支持的
int gxcodec_upgrade_firmware(void (*reboot_chip)(void), const char *buf, int len)
{
    boot_buf = gxscpu_boot;
    boot_len = gxscpu_boot_len;
    fw_buf  = buf;
    fw_len  = len;

    temp_buff = kmalloc(UPGRADE_FLASH_BLOCK_SIZE + 16, GFP_KERNEL);
    if (!temp_buff) {
        pr_info("Can't allocate param buffer (size = %d)!\n", UPGRADE_FLASH_BLOCK_SIZE + 16);
        return -1;
    }

    unsigned char wbuffer =  0xEF;
    int try_count = 5000;
    while (try_count > 0) // handshake
    {
        reboot_chip();
        msleep(10);
        if (upgrade_write_data(&wbuffer, 1))
        {
            if (upgrade_wait_reply(0xA0, 0x78, 1))
            {
                break;
            }
        }

        try_count--;
        msleep(100);
    }

    if (try_count == 0)
    {
        pr_info("handshake error\n");
        return -1;
    }

    parse_bootimg_header();
    if (!download_bootimg_stage1()) return -2;
    if (!download_bootimg_stage2()) return -3;
    if (!download_flashimg()) return -4;

    kfree(temp_buff);
    return 0;
}
