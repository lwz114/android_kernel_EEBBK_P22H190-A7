#ifndef _GXCODEC_UPGRADE_H_
#define _GXCODEC_UPGRADE_H_

int gxcodec_upgrade_firmware(void (*reboot_chip)(void), const char *buf, int len);

#endif
