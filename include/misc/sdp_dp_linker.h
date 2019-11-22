#ifndef __SDP_DP_LINKER_H__
#define __SDP_DP_LINKER_H__

#define DP_SET_MFC_STRIDEMODE_FUNC	"dpext_set_mfc_stridemode"
#define DP_SET_MFC_STRIDEMODE_PARAM	"%u %u %u"

#define DP_SET_MFC0_FB_FUNC		"dpext_set_mfc0_frmbuffer"
#define DP_SET_MFC0_FB_PARAM		"%u %u %u %u %u %u"

#define DP_SET_MFC1_FB_FUNC		"dpext_set_mfc1_frmbuffer"
#define DP_SET_MFC1_FB_PARAM		"%u %u %u %u %u %u"

#define MFC_GET_BUF_STATUS_FUNC	"mfcext_get_buffer_status"
#define MFC_GET_BUF_STATUS_PARAM "%lu %lu %lu %p %p %p"

#define MFC_SET_ABNORMALWRAP_FUNC "mfcext_set_abnormalwrap"
#define MFC_SET_ABNORMALWRAP_PARAM "%lu %lu"

#define MFC_GET_PIC_INFO_FUNC "mfcext_get_picture_info"
#define MFC_GET_PIC_INFO_PARAM "%lu %lu *p *p *p *p"

#define MFC_SET_DISP_PTR_FUNC "mfcext_set_display_ptr"
#define MFC_SET_DISP_PTR_PARAM "%lu %lu %lu %lu"

#endif
