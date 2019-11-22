uvcvideo-objs  := uvc_driver.o uvc_queue.o uvc_video.o uvc_ctrl.o \
		  uvc_status.o uvc_isight.o uvc_debugfs.o
ifeq ($(CONFIG_USB_VIDEO_TV_CAMERA),y)
uvcvideo-objs  += uvc_v4l2_tvcam.o
else
uvcvideo-objs  += uvc_v4l2.o
endif

ifeq ($(CONFIG_MEDIA_CONTROLLER),y)
uvcvideo-objs  += uvc_entity.o
endif
obj-$(CONFIG_USB_VIDEO_CLASS) += uvcvideo.o
