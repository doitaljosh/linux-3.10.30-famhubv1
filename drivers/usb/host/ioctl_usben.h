#ifndef __IOCTL_USBEN__
#define __IOCTL_USBEN__

#include <linux/ioctl.h>

#define DEVICE_FILE_NAME			"/dev/usben"
#define MAJOR_NUM					400
#define IOCTL_USBEN_SET				_IOW(MAJOR_NUM, 1, int)
#define IOCTL_USBEN_GET				_IOR(MAJOR_NUM, 2, int)
#define IOCTL_USBEN_RESET_BT		_IOW(MAJOR_NUM, 3, int)
#define IOCTL_USBEN_RESET_WIFI		_IOW(MAJOR_NUM, 4, int)
#define IOCTL_USBEN_RESET_MOIP		_IOW(MAJOR_NUM, 5, int)
#define IOCTL_USBEN_SET_BT_LOW		_IOW(MAJOR_NUM, 6, int)
#define IOCTL_USBEN_SET_WIFI_LOW	_IOW(MAJOR_NUM, 7, int)

#endif

