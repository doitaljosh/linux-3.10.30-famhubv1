#ifndef __UAPI__TZTV__FRC__TCON__H__
#define __UAPI__TZTV__FRC__TCON__H__

#ifdef  __cplusplus
extern "C" {
#endif

#define FRC_IOCTL_BASE			'f'
#define FRC_IO(nr)			_IO(FRC_IOCTL_BASE, nr)
#define FRC_IOR(nr, type)		_IOR(FRC_IOCTL_BASE, nr, type)
#define FRC_IOW(nr, type)		_IOW(FRC_IOCTL_BASE, nr, type)
#define FRC_IOWR(nr, type)		_IOWR(FRC_IOCTL_BASE, nr, type)

#define TCON_IOCTL_BASE			't'
#define TCON_IO(nr)			_IO(TCON_IOCTL_BASE, nr)
#define TCON_IOR(nr, type)		_IOR(TCON_IOCTL_BASE, nr, type)
#define TCON_IOW(nr, type)		_IOW(TCON_IOCTL_BASE, nr, type)
#define TCON_IOWR(nr, type)		_IOWR(TCON_IOCTL_BASE, nr, type)

#define EXTENDED_NUMBER_OF_LDCC_MAP_SIZE	8
#define EXTENDED_NUMBER_OF_LOCAL_AREA		48

/*
 *********************** STRUCT FOR FRC IOCTLs ***************************
 */

struct fi_CD3dControlInfo_t {
	int e3dMode;
	int bGameModeOnOff;
	int bInternalTest;
	int eSourceMode;
	int b3DFlickerless;
	int e3dFormat;
	int bPcMode;
	int eFilmMode;
	int eAutoMotionMode;
	int eDetected3dMode;
};

struct fi_CDVideoExtensionInfo_t {
	int iBitRate;
	int eVideoFormat;
	int bLevelType;
};

struct fi_TDResolutionInfo_t {
	unsigned int		hResolution;		/* active horizontal resolution */
	unsigned int		vResolution;		/* active vertical resolution */
	unsigned int		hStart;			/* horizontal active resolution start */
	unsigned int		vStart;			/* vertical active resolution start */
	unsigned int		hTotal;			/* total horizontal resolution */
	unsigned int		vTotal;			/* total vertical resolution */
	unsigned int		hFreq;			/* horizontal frequency */
	unsigned int		vFreq;			/* vertical frequency (Hz * 100) */
	int			bProgressScan;		/* progress / interace */
	int			bHSyncPositive;		/* horizontal sync polarity */
	int			bVSyncPositive;		/* vertical sync polarity */
	int			bNearFrequency;		/* near frequency, PC resolution only */
	int 			bNotOptimumMode; 	/* [MFM prodcut] In PC/DVI source, if current source resolution is larger than panel resolution, this flag is set as true. */
	struct fi_CD3dControlInfo_t		t3dControlInfo;
	struct fi_CDVideoExtensionInfo_t	tVideoExInfo; 		/* [For WiseLink Movie] Video Extension Information */
};

struct fi_resolution_info {
	int eRes;
	struct fi_TDResolutionInfo_t tSetResInfo;
};

struct fi_3d_info {
	int e3dMode;
	int e3dFormat;
};

struct fi_src_info {
	int iSourceMode;
	int iResolution;
};

struct fi_factory_data {
	int iUID;
	int iValue;
};

struct fi_TDWhiteBalance_t {
	unsigned int rGain;
	unsigned int gGain;
	unsigned int bGain;
	unsigned int rOffset;
	unsigned int gOffset;
	unsigned int bOffset;
};

struct fi_sharpness {
	int eType;
	unsigned int uValue;
};

struct fi_osd_region {
	int bIndex;
	int bOnOff;
	int x;
	int y;
	int width;
	int height;
};

struct fi_3d_auto_setting {
	int bOnOff;
	int iStrength;
	int iViewpoint;
};

struct fi_3d_light_control {
	unsigned int startPos;
	unsigned int dimPosi2;
	unsigned int dimPosi3;
	unsigned int dimPosi4;
};

struct fi_TDRect_t {
	int x;
	int y;
	int width;
	int height;
};

struct fi_blt_ori_width {
	int iOutputResolutionFreq;
	unsigned int uiBLT_ORI_WIDTH;
};

struct fi_blt_pulse_width {
	unsigned int uiPWM_PULSE_WIDTH[4];
	unsigned int uiCommonBlock;
	unsigned int uiCommonFrame;
};

struct fi_bound {
	unsigned int uiB_BOT;
	unsigned int uiB_TOP;
};

struct fi_pwm_cmn_setting {
	unsigned int uiCommonBlock;
	unsigned int uiCommonFrame;

};

struct fi_pwm_poff_width {
	unsigned int uiTop;
	unsigned int uiBot;
};

struct fi_tcon_update {
	unsigned int u32Addr;
	unsigned int u32Value;
};

struct fi_sub_tcon_update {
	unsigned short u32Addr;
	unsigned short u32Value;
};

struct fi_tcon_command {
	int eCommand;
	unsigned short uValue;
};

struct fi_picture_test_pattern {
	int eFRCPattern;
	int RGBPattern;
};

struct fi_cinema_enhancer {
	unsigned int uiParam1;
	unsigned int uiParam2;
};

struct fi_cinema_position {
	unsigned int uiTopBot;
	unsigned int uiPosX;
	unsigned int uiPosY;
};

struct fi_cinema_block {
	unsigned int uiTopBot;
	unsigned int uiR;
	unsigned int uiG;
	unsigned int uiB;
};

struct fi_backend_command {
	int eCommand;
	unsigned int uiCnt;
	unsigned short value;
};

struct fi_3d_in_out_mode {
	int e3DInMode;
	int e3DOutMode;
};

struct fi_i2c_data {
	unsigned int u32Addr;
	unsigned int u32Value;
};

struct fi_i2c_data_mask {
	unsigned int u32Addr;
	unsigned int u32Mask;
	unsigned int u32Value;
};

struct fi_i2c_data_burst {
	unsigned int u32Addr;
	unsigned int len;
	unsigned char *pu32Value;
};

struct fi_i2c_data_sub {
	unsigned short Addr;
	unsigned short buffer;
};

struct fi_write_command {
	unsigned char command;
	unsigned char sub_cmd;
	unsigned short data;
};

struct fi_send_command {
	unsigned int u32Addr;
	unsigned int u32Value;
};

struct fi_backend_info {
	char *strGetInfo;
	int iMaxLength;
};

struct fi_flickerless_off {
	int ret_val;
	struct fi_TDResolutionInfo_t tSetResInfo;
};

struct fi_TDSourceResInfo_t {
	 int SourceMode;
	 int Resolution;
	 int PictureMode;
	 int ColorFormat;
	 int Backlight;
	 int bSportsMode;
};

struct fi_TDAutoMotionPlus_t {
	int bIsTTXOn;
	int bIsPipOn;
	int bFrameDoubling;
	int bIsGameMode;
	int bIsFactoryMode;
	int e3dMode;
	int uBlurReduction;
	int uJudderReduction;
};

struct fi_auto_motion_plus {
	int eAutoMotionMode;
	struct fi_TDAutoMotionPlus_t AutoMotionPlus;
};

struct fi_detected_3d_mode {
	int autoviewStartType;
	struct fi_TDRect_t rect;
	int *pDetectedMode;
};

struct fi_3d_inout_mode_data {
	int iResolution;
	int i3Dformat;
	int i3DIn_Scaler;
	int i3DOut_Scaler;
	int iWidth_Scaler;
	int iHeight_Scaler;
	int i3DIn_Graphic;
	int i3DOut_Graphic;
	int iWidth_Graphic;
	int iHeight_Graphic;
	int iTemp_1;
	int iTemp_2;
};

struct fi_system_config {
	int region;
	int hv_flip;
	int fdisplay_on_off;
	int pc_mode_on_off;
	int home_panel_frc;
	int shop_3d_cube;
	int panel_cell_init_time;
	int tcon_init_time;
	int btemidel_50_bon;
	int btemidel_50_mov;
	int btemidel_60_dyn;
	int btemidel_60_mov;
	int slavdelay48_bon;
	int slavdelay60_bon;
	int slavdelay48;
	int slavdelay60;
	int ssc_vx1_on_off;
	int ssc_vx1_period;
	int ssc_vx1_modulation;
	int ssc_lvds_on_off;
	int ssc_lvds_mfr;
	int ssc_lvds_mrr;
	int ssc_ddr_on_off;
	int ssc_ddr_period;
	int ssc_ddr_modulation;
	int ssc_ddr_mfr;
	int ssc_ddr_mrr;
	int dimming_type;
	int support_3d;
	int frc_vx1_rx_eq;
	int reserved1;
	int reserved2;
};

struct fi_panel_info {
	int width;
	int height;
	int vfreq_type;
	int support_3d;
	int support_pc_3d;
	int mute_for_gamemode;
};

struct fi_start_stop_autoview {
	int start_stop;
	int start_type;
	int video_x;
	int video_y;
	int video_width;
	int video_height;
};

struct fi_pip_info {
	int pip;
	int res;
	int source;
	int hresolution;
	int vresolution;
};

struct fi_dimming_register_info {
	unsigned int cmd_num;
	unsigned int *cmd_address;
	unsigned int register_num;
	unsigned int *register_address;
};

struct fi_info_to_check_panel_mute {
	unsigned int res;
	unsigned int vfreq;
	unsigned int *mute;
};

struct fi_flash_status {
	unsigned int frc;
	unsigned int tcon;
	unsigned int reserved;
};

struct fi_video_size_info {
	unsigned int crop_x;
	unsigned int crop_y;
	unsigned int crop_w;
	unsigned int crop_h;
	unsigned int geo_x;
	unsigned int geo_y;
	unsigned int geo_w;
	unsigned int geo_h;
};

struct fi_cmr_control_reg
{
	unsigned int MainRegDummy38;
	unsigned int MainRegDummy39;
	unsigned int MainRegDummy40;
	unsigned int MainRegDummy41;
};

struct fi_avsr_control_reg
{
	unsigned int MainRegDummy20;
	unsigned int MainRegDummy21;
};

/*
 ***************************** STRUCTS FOR TCON IOCTLs *********************************
 */

struct ti_checksum {
	int len;
	char *pValueStr;
};

struct ti_lut_data {
	int eCurrentLDCCPhase;
	int bDebugMode;
	int eUpdateMode;
};

struct ti_3d_glimit {
	int eCurrentLDCCPhase;
	int bSlowSetMode;
};

struct ti_special_glimit {
	int iGlimit;
	int iSourceMode;
};

struct ti_3d_effect {
	int eCurrentLDCCPhase;
	int b3DOnOff;
	int bDebugMode;
};

struct ti_update_eeprom {
	int len;
	char *pstrTconPath;
};

struct ti_ctr {
	unsigned int UniqueID;
	signed int iSetValue;
};

struct ti_test_pattern {
	signed int iSetValue;
	int ePatternMode;
};

struct ti_flash_version {
	int len;
	char *pStr;
};

struct ti_panel_wp {
	int eWPType;
	int bOnOff;
	unsigned int u32Delay;
};

struct ti_i2c_sw {
	int bOnOff;
	unsigned int u32Delay;
};

struct ti_factory_tcon_partition {
	unsigned int UniqueID;
	signed int iSetValue;
};

struct ti_factory_dcc_debug {
	unsigned int UniqueID;
	signed int iSetValue;
};

struct ti_glimit {
	int b3DOnOff;
	int iPanelTemp;
	int iCurrentTcon;
};

struct ti_is_lut_data{
	char *pTconPath;
	int tcon_len;
	int pbIsPossible;
	int bDebugMode;
	const char *path;
	int path_len;
};

struct ti_multi_acc_data {
	char *pTconPath;
	int len;
	int pbIsPossible;
};

struct ti_tcon_bctr_t {
	int iDynCtrOn;
	int iMovCtrOn;
	int iIirOption;
	int iIirVel;
	int iSlpFac;
	int iIirLth;
	int iIirLCoef;
	int iIirHCoef;
	int iUMaxDynValue;
	int iUMaxMovValue;
	int iUMinDynValue;
	int iUMinMovValue;
	int iMaxDynDrop;
	int iMaxMovDrop;
	int iMinDynDrop;
	int iMinMovDrop;
	int iMaxDownTh1;
	int iMaxDownTh2;
	int iMaxHDynTh;
	int iMaxHMovTh;
	int iMinHDynTh;
	int iMinHMovTh;
	int iHistoDynBin;
	int iHistoMovBin;
	int iCtrOnGlimit;
};

struct ti_factory_bctr {
	unsigned int UniqueID;
	signed int iSetValue;
};

struct ti_panel_info {
	char *strPanelType;
	int type_len;
	char *strPanelName;
	int name_len;
};

struct ti_panel_string {
	char *strPanelString;
	int len;
};

struct ti_factory_glimit {
	int iPanelTemp;
	int iGLimitValue;
};

struct ti_Tcon_Map_t {
    int DCCSELMAP[EXTENDED_NUMBER_OF_LDCC_MAP_SIZE];
    int POSISEL[EXTENDED_NUMBER_OF_LOCAL_AREA];
};

struct ti_TconDCCDebug_t {
    int bLDCCDebug;
    int bUSBDebug;
    struct ti_Tcon_Map_t tTconMap;
};

struct ti_get_factory_dcc {
	int bReadValue;
	struct ti_TconDCCDebug_t tTconDCCDebug;
};

struct ti_TconPartion_t {
	int DCCX1;
	int DCCX2;
	int DCCX3;
	int DCCX4;
	int DCCX5;
	int DCCX6;
	int DCCX7;

	int DCCY1;
	int DCCY2;
	int DCCY3;
	int DCCY4;
	int DCCY5;

	int DCCh1;
	int DCCh2;
	int DCCh3;
	int DCCh4;
	int DCCh5;
	int DCCh6;
	int DCCh7;

	int DCCv1;
	int DCCv2;
	int DCCv3;
	int DCCv4;
	int DCCv5;
};

struct ti_get_factory_tcon {
	int bReadValue;
	struct ti_TconPartion_t tTconPartition;
};

struct fti_i2c {
	unsigned char	slaveAddr;
	unsigned char 	*pSubAddr;
	unsigned char	*pDataBuffer;
	unsigned int	dataSize;
};

struct ti_TconTemperature_t {
	int TEMP_READ;
	int TEMP_LAST;
	int TEMP_DELTA[EXTENDED_NUMBER_OF_LDCC_MAP_SIZE];
	int TEMP_SEL[EXTENDED_NUMBER_OF_LOCAL_AREA];
	int DEL_STEP_200;
	int TIME_TO_COLD;
};

struct ti_TconGlimitLBT_t {
	int LBT0;
	int LBT1;
	int LBT2;
	int LBT3;
	int LBT4;
	int LBT5;
	int LBT6;
	int LBT7;
	int LBT8;
	int LBT9;
	int LBT10;
	int LBT11;
	int LBT12;
	int LBT13;
	int LBT14;
	int LBT15;
	int LBT16;
	int LBT17;
	int LBT18;
	int LBT19;
	int LBT20;
};

struct ti_tcon_factory_data {
	struct ti_tcon_bctr_t bctr;
	struct ti_TconDCCDebug_t dcc_on;
	struct ti_TconDCCDebug_t dcc_off;
	struct ti_TconPartion_t partition;
	struct ti_TconTemperature_t temp_on;
	struct ti_TconTemperature_t temp_off;
	struct ti_TconGlimitLBT_t glimit_lbt;
};

/*
 ***************************** FRC IOCTLs *********************************
 */

/* UpgradeFirmware */
#define FRC_IOCTL_UPGRADE_FIRMAWARE			FRC_IOWR(0x01, int)

/* UpgradeLDTable */
#define FRC_IOCTL_UPGRADE_LD_TABLE			FRC_IOWR(0x02, int)

/* FlagInit */
#define FRC_IOCTL_FLAG_INIT				FRC_IOWR(0x03, int)

/* SetDefaultData */
#define FRC_IOCTL_SET_DEFAULT_DATA			FRC_IOWR(0x04, int)

/* ExecuteMajorCmd */
#define FRC_IOCTL_EXECUTE_MAJOR_COMMAND			FRC_IOWR(0x05, int)

/* ExecuteMonitorTask */
#define FRC_IOCTL_EXECUTE_MONITOR_TASK			FRC_IOWR(0x06, int)

/* DebugBackend */
#define FRC_IOCTL_DEBUG_BACKEND				FRC_IOWR(0x07, int)

/* SetDebugPrint */
#define FRC_IOCTL_SET_DEBUG_PRINT			FRC_IOWR(0x08, int)

/* GetDebugPrint */
#define FRC_IOCTL_GET_DEBUG_PRINT			FRC_IOWR(0x09, int)

/* SetDCCDebugFlag */
#define FRC_IOCTL_SET_DCC_DEBUG_FLAG			FRC_IOWR(0x0A, int)

/* SetDCCMode */
#define FRC_IOCTL_SET_DCC_MODE				FRC_IOWR(0x0B, int)

/* GetDCCDebugFlag */
#define FRC_IOCTL_GET_DCC_DEBUG_FLAG			FRC_IOWR(0x0C, int)

/* GetDCCMode */
#define FRC_IOCTL_GET_DCC_MODE				FRC_IOWR(0x0D, int)

/* SetShop3DCube */
#define FRC_IOCTL_SET_SHOP_3D_CUBE			FRC_IOWR(0x0E, int)

/* CtrlLVDSOnOff */
#define FRC_IOCTL_LVDS_ON_OFF				FRC_IOWR(0x0F, int)

/* SetInputSize */
#define FRC_IOCTL_SET_INPUT_SIZE			FRC_IOWR(0x10, int)

/* SetResolutionInfo */
#define FRC_IOCTL_SET_RESOLUTION_INFO			FRC_IOWR(0x11, struct fi_resolution_info)

/* Set3DInfo */
#define FRC_IOCTL_SET_3D_INFO				FRC_IOWR(0x12, struct fi_3d_info)

/* ReadSourceInfo */
#define FRC_IOCTL_READ_SOURCE_INFO			FRC_IOWR(0x13, struct fi_src_info)

/* CheckCurrentPanelType */
#define FRC_IOCTL_CHECK_PANEL_TYPE			FRC_IOWR(0x14, int)

/* SetMute */
#define FRC_IOCTL_SET_MUTE				FRC_IOWR(0x15, int)

/* SetSeamlessMute */
#define FRC_IOCTL_SET_SEAMLESS_MUTE			FRC_IOWR(0x16, int)

/* SetAgingPattern */
#define FRC_IOCTL_SET_AGING_PATTERN			FRC_IOWR(0x17, int)

/* CtrlPatternBeforeDDR */
#define FRC_IOCTL_CTRL_PATTERN_BEFORE_DDR		FRC_IOWR(0x18, int)

/* CtrlPatternAfterDDR */
#define FRC_IOCTL_CTRL_PATTERN_AFTER_DDR		FRC_IOWR(0x19, int)

/* CtrlOSDPatternBeforeDDR */
#define FRC_IOCTL_CTRL_OSD_PATTERN_BEFORE_DDR		FRC_IOWR(0x1A, int)

/* CtrlOSDPatternAfterDDR */
#define FRC_IOCTL_CTRL_OSD_PATTERN_AFTER_DDR		FRC_IOWR(0x1B, int)

/* SetFactoryInit */
#define FRC_IOCTL_SET_FACTORY_INIT			FRC_IOWR(0x1C, int)

/* SetFactoryData */
#define FRC_IOCTL_SET_FACTORY_DATA			FRC_IOWR(0x1D, struct fi_factory_data)

/* SetWhiteBalance */
#define FRC_IOCTL_SET_WHITE_BALANCE			FRC_IOWR(0x1E, struct fi_TDWhiteBalance_t)

/* SetWhiteBalanceThreshold */
#define FRC_IOCTL_SET_WHITE_BALANCE_THRESHOLD		FRC_IOWR(0x1F, struct fi_TDWhiteBalance_t)

/* SetSharpness */
#define FRC_IOCTL_SET_SHARPNESS				FRC_IOWR(0x20, struct fi_sharpness)

/* SetDigitalNR */
#define FRC_IOCTL_SET_DIGITAL_NR			FRC_IOWR(0x21, int)

/* GetFlipMode */
#define FRC_IOCTL_GET_FLIP_MODE				FRC_IOWR(0x22, int)

/* SetEOSD */
#define FRC_IOCTL_SET_EOSD				FRC_IOWR(0x23, int)

/* SetOSDRegion */
#define FRC_IOCTL_SET_OSD_REGION			FRC_IOWR(0x24, struct fi_osd_region)

/* ExecuteCellDepthDemo */
#define FRC_IOCTL_EXECUTE_CELL_DEPTH_DEMO		FRC_IOWR(0x25, int)

/* SetAutoMotionPlus */
#define FRC_IOCTL_SET_AUTO_MOTION_PLUS			FRC_IOWR(0x26, struct fi_auto_motion_plus)

/* GetPCMode */
#define FRC_IOCTL_GET_PC_MODE				FRC_IOWR(0x27, int)

/* SetPCMode */
#define FRC_IOCTL_SET_PC_MODE				FRC_IOWR(0x28, int)

/* SetGameMode */
#define FRC_IOCTL_SET_GAME_MODE				FRC_IOWR(0x29, int)

/* GetGameMode */
#define FRC_IOCTL_GET_GAME_MODE				FRC_IOWR(0x2A, int)

/* SetJPEGMode */
#define FRC_IOCTL_SET_JPEG_MODE				FRC_IOWR(0x2B, int)

/* SetFilmMode */
#define FRC_IOCTL_SET_FILM_MODE				FRC_IOWR(0x2C, int)

/* GetFilmMode */
#define FRC_IOCTL_GET_FILM_MODE				FRC_IOWR(0x2D, int)

/* CheckMJCOnOffAsResolution */
#define FRC_IOCTL_CHECK_MJC_RESOLUTION			FRC_IOWR(0x2E, int)

/* CheckMJCOnOffAsMotion */
#define FRC_IOCTL_CHECK_MJC_MOTION			FRC_IOWR(0x2F, int)

/* CheckFlickerlessOff */
#define FRC_IOCTL_CHECK_FLICKERLESS_OFF			FRC_IOWR(0x30, struct fi_flickerless_off)

/* SetDimming */
#define FRC_IOCTL_SET_DIMMING				FRC_IOWR(0x31, int)

/* SetPictureTable */
#define FRC_IOCTL_SET_PICTURE_TABLE			FRC_IOWR(0x32, int)

/* SetPictureInfo */
#define FRC_IOCTL_SET_PICTURE_INFO			FRC_IOWR(0x33, struct fi_TDSourceResInfo_t)

/* SetVideoSignalActivity */
#define FRC_IOCTL_SET_VIDEO_SIGNAL_ACTIVITY		FRC_IOWR(0x34, int)

/* SetEmitterPulse */
#define FRC_IOCTL_SET_EMMITER_PULSE			FRC_IOWR(0x35, int)

/* Set3DEmitterSync */
#define FRC_IOCTL_SET_3D_EMITTER_SYNC			FRC_IOWR(0x36, int)

/* Set3DEmitterSyncTest */
#define FRC_IOCTL_SET_3D_EMITTER_SYNC_TEST		FRC_IOWR(0x37, int)

/* CtrlEmitterSync */
#define FRC_IOCTL_CTRL_EMITTER_SYNC			FRC_IOWR(0x38, int)

/* Set3DDutyDelay */
#define FRC_IOCTL_SET_3D_DUTY_DELAY			FRC_IOWR(0x39, int)

/* SetVideoSyncMode */
#define FRC_IOCTL_SET_VIDEO_SYNC_MODE			FRC_IOWR(0x40, int)

/* Get3DSyncOnOff */
#define FRC_IOCTL_GET_3D_SYNC_ON_OFF			FRC_IOWR(0x41, int)

/* Set3DSyncOnOff */
#define FRC_IOCTL_SET_3D_SYNC_ON_OFF			FRC_IOWR(0x42, int)

/* Set3DMode */
#define FRC_IOCTL_SET_3D_MODE				FRC_IOWR(0x43, int)

/* Set2DMode */
#define FRC_IOCTL_SET_2D_MODE				FRC_IOWR(0x44, int)

/* Set3DStrength */
#define FRC_IOCTL_SET_3D_STRENGTH			FRC_IOWR(0x45, int)

/* Set3DBlackBar */
#define FRC_IOCTL_SET_3D_BLACK_BAR			FRC_IOWR(0x46, int)

/* Set3DLRControl */
#define FRC_IOCTL_SET_3D_LR_CONTROL			FRC_IOWR(0x47, int)

/* Set3DAutoSetting */
#define FRC_IOCTL_SET_3D_AUTO_SETTING			FRC_IOWR(0x48, struct fi_3d_auto_setting)

/* Set3DLightControl */
#define FRC_IOCTL_SET_3D_LIGHT_CONTROL			FRC_IOWR(0x49, struct fi_3d_light_control)

/* SetFlickerless */
#define FRC_IOCTL_SET_FLICKERLESS			FRC_IOWR(0x4A, int)

/* GetFlickerless */
#define FRC_IOCTL_GET_FLICKERLESS			FRC_IOWR(0x4B, int)

/* SetFlickerlessChangedFlag */
#define FRC_IOCTL_SET_FLICKERLESS_FLAG			FRC_IOWR(0x4C, int)

/* GetFlickerlessChangedFlag */
#define FRC_IOCTL_GET_FLICKERLESS_FLAG			FRC_IOWR(0x4D, int)

/* Set3DViewpoint */
#define FRC_IOCTL_SET_3D_VIEWPOINT			FRC_IOWR(0x4E, int)

/* Set3DBlackInsertion */
#define FRC_IOCTL_SET_3D_BLACK_INSERTION		FRC_IOWR(0x4F, int)

/* SetBypass */
#define FRC_IOCTL_SET_BYPASS				FRC_IOWR(0x50, int)

/* Get3DSetValue */
#define FRC_IOCTL_GET_3D_SET_VALUE			FRC_IOWR(0x51, int)

/* Get3DLRControl */
#define FRC_IOCTL_GET_3D_LR_CONTROL			FRC_IOWR(0x52, int)

/* SetGlimit */
#define FRC_IOCTL_SET_GLIMIT				FRC_IOWR(0x53, int)

/* Convert3DInModeForSDAL */
#define FRC_IOCTL_CONVERT_3D_IN_MODE			FRC_IOWR(0x54, int)

/* Convert3DOutModeForSDAL */
#define FRC_IOCTL_CONVERT_3D_OUT_MODE			FRC_IOWR(0x55, int)

/* Get3dModeForOSDStereoscopic */
#define FRC_IOCTL_GET_3D_MODE_OSD_STEREO		FRC_IOWR(0x56, int)

/* SetSelfDiagnosisPattern */
#define FRC_IOCTL_SET_SELF_DIAGNOSIS_PATTERN		FRC_IOWR(0x57, int)

/* StartAutoViewOsdDetection */
#define FRC_IOCTL_START_AV_OSD_DETECTION		FRC_IOWR(0x58, int)

/* GetAutoViewOsdLevel */
#define FRC_IOCTL_GET_AV_OSD_LEVEL			FRC_IOWR(0x59, int)

/* ResetAutoViewDetection */
#define FRC_IOCTL_RESET_AV_DETECTION			FRC_IOWR(0x5A, int)

/* SetAutoViewOnOff */
#define FRC_IOCTL_SET_AV_ON_OFF				FRC_IOWR(0x5B, int)

/* SetHistogramForAutoView */
#define FRC_IOCTL_SET_HISTOGRAM_FOR_AV			FRC_IOWR(0x5C, int)

/* RestoreHistogramForAutoView */
#define FRC_IOCTL_RESTORE_HISTOGRAM_FOR_AV		FRC_IOWR(0xB9, int)

/* SetAutoViewInterval */
#define FRC_IOCTL_SET_AV_INTERVAL			FRC_IOWR(0x5D, int)

/* SetFocalZForceEnableOnOff */
#define FRC_IOCTL_SET_FOCAL_Z_FORCE_ENABLE		FRC_IOWR(0x5E, int)

/* SetAutoViewModeSelect */
#define FRC_IOCTL_SET_AV_MODE_SELECT			FRC_IOWR(0x5F, int)

/* SetScreenModeForAutoView */
#define FRC_IOCTL_SET_SCREEN_MODE_FOR_AV		FRC_IOWR(0x60, int)

/* SetScreenRectForAutoView */
#define FRC_IOCTL_SET_SCREEN_RECT_FOR_AV		FRC_IOWR(0x61, struct fi_TDRect_t)

/* StartAutoViewDetection */
#define FRC_IOCTL_START_AV_DETECTION			FRC_IOWR(0x62, int)

/* StopAutoViewDetection */
#define FRC_IOCTL_STOP_AV_DETECTION			FRC_IOWR(0x63, int)

/* GetAutoViewDetectedMode */
#define FRC_IOCTL_GET_AV_DETECT_MODE			FRC_IOWR(0x64, int)

/* CheckEndAutoViewDetection */
#define FRC_IOCTL_CHECK_END_AV_DETECTION		FRC_IOWR(0x65, int)

/* SetAfdStart */
#define FRC_IOCTL_SET_AFD_START				FRC_IOWR(0x66, int)

/* SetAfdStop */
#define FRC_IOCTL_SET_AFD_STOP				FRC_IOWR(0x67, int)

/* WriteI2CData */
#define FRC_IOCTL_WRITE_I2C_DATA			FRC_IOWR(0x68, struct fi_i2c_data)

/* WriteI2CMaskData */
#define FRC_IOCTL_WRITE_I2C_MASK_DATA			FRC_IOWR(0x69, struct fi_i2c_data_mask)

/* WriteBurstI2CData */
#define FRC_IOCTL_WRITE_BURST_I2C_DATA			FRC_IOWR(0x6A, struct fi_i2c_data_burst)

/* WriteBurstI2CDataTconUpdate */
#define FRC_IOCTL_WRITE_BURST_I2C_DATA_TCON		FRC_IOWR(0x6B, struct fi_i2c_data_burst)

/* ReadBurstI2CData */
#define FRC_IOCTL_READ_BURST_I2C_DATA			FRC_IOWR(0x6C, struct fi_i2c_data_burst)

/* SendCommand */
#define FRC_IOCTL_SEND_COMMAND				FRC_IOWR(0x6D, struct fi_send_command)

/* ReadI2CData */
#define FRC_IOCTL_READ_I2C_DATA				FRC_IOWR(0x6E, struct fi_i2c_data)

/* WriteSubI2CData */
#define FRC_IOCTL_WRITE_SUB_I2C_DATA			FRC_IOWR(0x6F, struct fi_i2c_data_sub)

/* GetVersion */
#define FRC_IOCTL_GET_VERSION				FRC_IOWR(0x70, int)

/* GetDataVersion */
#define FRC_IOCTL_GET_DATA_VERSION			FRC_IOWR(0x71, int)

/* GetBackendInfo */
#define FRC_IOCTL_GET_BACKEND_INFO			FRC_IOWR(0x72, struct fi_backend_info)

/* GetEchoFpCoreVersion */
#define FRC_IOCTL_GET_ECHO_FP_CORE_VERSION		FRC_IOWR(0x73, int)

/* GetEchoFpLutVersion */
#define FRC_IOCTL_GET_ECHO_FP_LUT_VERSION		FRC_IOWR(0x74, int)

/* GetEchoFpDspVersion */
#define FRC_IOCTL_GET_ECHO_FP_DSP_VERSION		FRC_IOWR(0x75, int)

/* GetEchoFpFirmwareVersion */
#define FRC_IOCTL_GET_ECHO_FP_FW_VERSION		FRC_IOWR(0x76, int)

/* SetTCONWpLevel */
#define FRC_IOCTL_SET_TCON_WP_LEVEL			FRC_IOWR(0x77, int)

/* SetTCON1EnableLevel */
#define FRC_IOCTL_SET_TCON1_ENABLE_LEVEL		FRC_IOWR(0x78, int)

/* SetTCON2EnableLevel */
#define FRC_IOCTL_SET_TCON2_ENABLE_LEVEL		FRC_IOWR(0x79, int)

/* SupportTCONI2CEnable */
#define FRC_IOCTL_SUPPORT_TCON_I2C_ENABLE		FRC_IOWR(0x7A, int)

/* WriteCommand */
#define FRC_IOCTL_WRITE_COMMAND				FRC_IOWR(0x7B, struct fi_write_command)

/* ReadCommand */
#define FRC_IOCTL_READ_COMMAND				FRC_IOWR(0x7C, int)

/* ChangeUdFhdDisplayMode */
#define FRC_IOCTL_CHANGE_UDFHD_DISPLAY_MODE		FRC_IOWR(0x7D, int)

/* ChangeUdFhdDisplayModeByRes */
#define FRC_IOCTL_CHANGE_UDFHD_DISPLAY_MODE_RES		FRC_IOWR(0x7E, int)

/* SetOSDWhiteBalance */
#define FRC_IOCTL_SET_OSD_WHITE_BALANCE			FRC_IOWR(0x7F, int)

/* SetPWM_ANA_MUX */
#define FRC_IOCTL_SET_PWM_ANA_MUX			FRC_IOWR(0x80, int)

/* SetPWM_BRI_GAIN */
#define FRC_IOCTL_SET_PWM_BRI_GAIN			FRC_IOWR(0x81, int)

/* GetBLT_ORI_WIDTH */
#define FRC_IOCTL_GET_BLT_ORI_WIDTH			FRC_IOWR(0x82, struct fi_blt_ori_width)

/* SetBLT_PULSE_WIDTH */
#define FRC_IOCTL_SET_BLT_PULSE_WIDTH			FRC_IOWR(0x83, struct fi_blt_pulse_width)

/* SetPWM_WIDTH_OFFSET */
#define FRC_IOCTL_SET_PWM_WIDTH_OFFSET			FRC_IOWR(0x84, unsigned int[4])

/* SetPWM_LIMIT */
#define FRC_IOCTL_SET_PWM_LIMIT				FRC_IOWR(0x85, int)

/* GetBOUND */
#define FRC_IOCTL_GET_BOUND				FRC_IOWR(0x86, struct fi_bound)

/* SetPWM_CommonSetting */
#define FRC_IOCTL_SET_PWM_COMMON_SETTING		FRC_IOWR(0x87, struct fi_pwm_cmn_setting)

/* SetPWM_POFF_WIDTH */
#define FRC_IOCTL_SET_PWM_POFF_WIDTH			FRC_IOWR(0x88, struct fi_pwm_poff_width)

/* GetPWM_POFF_WIDTH */
#define FRC_IOCTL_GET_PWM_POFF_WIDTH			FRC_IOWR(0x89, struct fi_pwm_poff_width)

/* Set_3D_LATCH_VCNT */
#define FRC_IOCTL_SET_3D_LATCH_VCNT			FRC_IOWR(0x8A, unsigned int[4])

/* GetPanelFreq */
#define FRC_IOCTL_GET_PANEL_FREQ			FRC_IOWR(0x8B, int)

/* SetSmartLedPicture */
#define FRC_IOCTL_SET_SMART_LED_PICTURE			FRC_IOWR(0x8C, int)

/* MonitorCPUControl */
#define FRC_IOCTL_MONITOR_CPU_CONTROL			FRC_IOWR(0x8D, int)

/* SetDSPMode */
#define FRC_IOCTL_SET_DSP_MODE				FRC_IOWR(0x8E, int)

/* GetDSPMode */
#define FRC_IOCTL_GET_DSP_MODE				FRC_IOWR(0x8F, int)

/* CheckCrcForLvds */
#define FRC_IOCTL_CHECK_CRC_FOR_LVDS			FRC_IOWR(0x90, int)

/* CheckCrcForLvds */
#define FRC_IOCTL_CHECK_CRC_FOR_LVDS_2			FRC_IOWR(0x91, int)

/* CheckCrcForTCon */
#define FRC_IOCTL_CHECK_CRC_FOR_TCON			FRC_IOWR(0x92, int)

/* RunSmartInspection */
#define FRC_IOCTL_RUN_SMART_INSPECTION			FRC_IOWR(0x93, int)

/* Set3DBrightnessInformation */
#define FRC_IOCTL_SET_3D_BRI_INFO			FRC_IOWR(0x94, int)

/* SetPictureSize */
#define FRC_IOCTL_SET_PICTURE_SIZE			FRC_IOWR(0x95, int)

/* SetMicomUpdateFlag */
#define FRC_IOCTL_SET_MICOM_UPDATE_FLAG			FRC_IOWR(0x96, int)

/* GetMicomUpdateFlag */
#define FRC_IOCTL_GET_MICOM_UPDATE_FLAG			FRC_IOWR(0x97, int)

/* IsSupportedFunc */
#define FRC_IOCTL_IS_SUPPORTED_FUNC			FRC_IOWR(0x98, int)

/* WriteI2CDataTconUpdate */
#define FRC_IOCTL_WRITE_I2C_DATA_TCON_UPDATE		FRC_IOWR(0x99, struct fi_tcon_update)

/* ReadI2CDataTconUpdate */
#define FRC_IOCTL_READ_I2C_DATA_TCON_UPDATE		FRC_IOWR(0x9A, struct fi_tcon_update)

/* ReadSubI2CDataTconUpdate */
#define FRC_IOCTL_READ_SUB_I2C_DATA_TCON_UPDATE		FRC_IOWR(0x9B, struct fi_sub_tcon_update)

/* SendTconCommand */
#define FRC_IOCTL_SEND_TCON_COMMAND			FRC_IOWR(0x9C, struct fi_tcon_command)

/* IsSupportFRCPattern */
#define FRC_IOCTL_IS_SUPPORT_FRC_PATTERN		FRC_IOWR(0x9D, int)

/* GetPictureTestPatternNum */
#define FRC_IOCTL_GET_PIC_TEST_PATTERN_NUM		FRC_IOWR(0x9E, struct fi_picture_test_pattern)

/* SetPictureTestPatternNum */
#define FRC_IOCTL_SET_PIC_TEST_PATTERN_NUM		FRC_IOWR(0x9F, struct fi_picture_test_pattern)

/* SetHomePanelMove */
#define FRC_IOCTL_SET_HOME_PANEL_MOVE			FRC_IOWR(0xA0, int)

/* Set3DViewType */
#define FRC_IOCTL_SET_3D_VIEW_TYPE			FRC_IOWR(0xA1, int)

/* Get3DViewType */
#define FRC_IOCTL_GET_3D_VIEW_TYPE			FRC_IOWR(0xA2, int)

/* GetBTViewMode */
#define FRC_IOCTL_GET_BT_VIEW_MODE			FRC_IOWR(0xA3, int)

/* SetSpreadSpectrum */
#define FRC_IOCTL_SET_SPREAD_SPECTRUM			FRC_IOWR(0xA4, int)

/* GetFrcInitTime */
#define FRC_IOCTL_GET_FRC_INIT_TIME			FRC_IOWR(0xA5, int)

/* RecoveryFRCSetting */
#define FRC_IOCTL_RECOVERY_FRC_SETTING			FRC_IOWR(0xA6, int)

/* SetCinemaBlackEnhancer */
#define FRC_IOCTL_SET_CINEMA_BLACK_ENHANCER		FRC_IOWR(0xA7, struct fi_cinema_enhancer)

/* SetCinemaBlackReadPosition */
#define FRC_IOCTL_SET_CINEMA_BLACK_READ_POS		FRC_IOWR(0xA8, struct fi_cinema_position)

/* GetCinemaBlackBlockRead */
#define FRC_IOCTL_GET_CINEMA_BLACK_BLOCK_READ		FRC_IOWR(0xA9, struct fi_cinema_block)

/* GetPanelVfreqType */
#define FRC_IOCTL_GET_PANEL_V_FREQ_TYPE			FRC_IOWR(0xAA, int)

/* SetLedMotionPlus */
#define FRC_IOCTL_SET_LED_MOTION_PLUS			FRC_IOWR(0xAB, int)

/* CtrlTconReset */
#define FRC_IOCTL_CTRL_TCON_RESET			FRC_IOWR(0xAC, int)

/* CtrlPVCC */
#define FRC_IOCTL_CTRL_PVCC				FRC_IOWR(0xAD, int)

/* GetPanelModel */
#define FRC_IOCTL_GET_PANEL_MODEL			FRC_IOWR(0xAE, int)

/* SendBackendCommand */
#define FRC_IOCTL_SEND_BACKEND_COMMAND			FRC_IOWR(0xAF, struct fi_backend_command)

/* SendBackendCommand */
#define FRC_IOCTL_SEND_BACKEND_COMMAND_2		FRC_IOWR(0xB0, struct fi_backend_command)

/* ReadBackendCommand */
#define FRC_IOCTL_READ_BACKEND_COMMAND			FRC_IOWR(0xB1, struct fi_backend_command)

/* SetMuteDepth */
#define FRC_IOCTL_SET_MUTE_DEPTH			FRC_IOWR(0xB2, int)

/* SetPvccFlag */
#define FRC_IOCTL_SET_PVCC_FLAG				FRC_IOWR(0xB3, int)

/* GetPvccFlag */
#define FRC_IOCTL_GET_PVCC_FLAG				FRC_IOWR(0xB4, int)

/* SetHomePanelStatus */
#define FRC_IOCTL_SET_HOME_PANEL_STATUS			FRC_IOWR(0xB5, int)

/* SetMultiScreenStatus */
#define FRC_IOCTL_SET_MULTI_SCREEN_STATUS		FRC_IOWR(0xB6, int)

/* CheckPanelMuteForUHD */
#define FRC_IOCTL_CHECK_PANEL_MUTE_FOR_UHD		FRC_IOWR(0xB7, int)

/* Get3DInOutMode */
#define FRC_IOCTL_GET_3D_IN_OUT_MODE			FRC_IOWR(0xB8, struct fi_3d_in_out_mode)

/* get frc type */
#define FRC_IOCTL_GET_FRC_TYPE				FRC_IOWR(0xBA, int)

/* SetPivotSettingData */
#define FRC_IOCTL_SET_PIVOT_MODE			FRC_IOWR(0xBB, int)

/* tztv_i2c_read_frc, tztv_i2c_read_frc_sub1, tztv_i2c_read_frc_sub2 */
#define FRC_IOCTL_I2C_READ				FRC_IOWR(0xBC, struct fti_i2c)

/* tztv_i2c_write_frc, tztv_i2c_write_frc_sub1, tztv_i2c_write_frc_sub2 */
#define FRC_IOCTL_I2C_WRITE				FRC_IOWR(0xBD, struct fti_i2c)

/* GetDetected3DMode */
#define FRC_IOCTL_GET_DETECTED_3D_MODE  FRC_IOWR(0xBE, struct fi_detected_3d_mode)

/* Get 3D InOut Mode */
#define FRC_IOCTL_GET_3D_INOUT_MODE  FRC_IOWR(0xBF, struct fi_3d_inout_mode_data)

/* Set System Config */
#define FRC_IOCTL_SET_SYSTEM_CONFIG  FRC_IOWR(0xC0, struct fi_system_config)

/* Get Panel Info */
#define FRC_IOCTL_GET_PANEL_INFO  FRC_IOWR(0xC1, struct fi_panel_info)

/* Start/Stop Autoview */
#define FRC_IOCTL_START_STOP_AUTOVIEW  FRC_IOWR(0xC2, struct fi_start_stop_autoview)

/* Set Pip Info*/
#define FRC_IOCTL_SET_PIP_INFO  FRC_IOWR(0xC3, struct fi_pip_info)

/* CheckAutoMotionPlus */
#define FRC_IOCTL_CHECK_AUTO_MOTION_PLUS	FRC_IOWR(0xC4, int)

/* Set Dimming register info */
#define FRC_IOCTL_SET_DIMMING_REGISTER_INFO	FRC_IOWR(0xC5, struct fi_dimming_register_info)

/* CheckPanelMuteForUHD */
#define FRC_IOCTL_CHECK_PANEL_MUTE_BY_RES	FRC_IOWR(0xC6, struct fi_info_to_check_panel_mute)

/* CheckPanelMuteForMLS */
#define FRC_IOCTL_CHECK_PANEL_MUTE_FOR_MLS	FRC_IOWR(0xC7, int)

/* SetMpegNR */
#define FRC_IOCTL_SET_MPEG_NR				FRC_IOWR(0xC8, int)

/* Read SPI Flash Status */
#define FRC_IOCTL_READ_SPI_FLASH_STATUS		FRC_IOWR(0xC9, struct fi_flash_status)

/* Read FRC PROBE DONE */
#define FRC_IOCTL_PROBE_DONE				FRC_IOWR(0xCA, int)

/* Set Video Size Info */
#define FRC_IOCTL_SET_VIDEO_SIZE_INFO		FRC_IOWR(0xCB, struct fi_video_size_info)

/* Set Dual View Scenario  */
#define FRC_IOCTL_SET_VIEW_PATH_INFO		FRC_IOWR(0xCC, int)

/* Check KR3D2111 values */
#define FRC_IOCTL_KR3D2111					FRC_IOWR(0xCD, int)

/* Check AVSR CPU Control */ 
#define FRC_IOCTL_AVSR_CONTROL_REGISTER		FRC_IOWR(0xCE, struct fi_avsr_control_reg)

/* Check CMR CPU Control */ 
#define FRC_IOCTL_CMR_CONTROL_REGISTER		FRC_IOWR(0xCF, struct fi_cmr_control_reg)

/* Control TCON DCC Data*/ 
#define FRC_IOCTL_CONTROL_TCON_DCC_DATA		FRC_IOWR(0xD0, unsigned int[3])

/* Set 21:9 mode */
#define FRC_IOCTL_SET_21_9_MODE				FRC_IOWR(0xD1, int)

/*
 ***************************** TCON IOCTLs *********************************
 */

/* t_Tcon_InitDevice */
#define TCON_IOCTL_INIT_DEVICE				TCON_IOWR(0x5F, int)

/* t_Tcon_HasATemperSenosor */
#define TCON_IOCTL_HAS_A_TEMPER_SENSOR			TCON_IOWR(0x01, int)

/* t_Tcon_IsASupportLDCC */
#define TCON_IOCTL_IS_A_SUPPORT_LDCC			TCON_IOWR(0x02, int)

/* t_Tcon_GetChecksum */
#define TCON_IOCTL_GET_CHECKSUM				TCON_IOWR(0x03, struct ti_checksum)

/* t_Tcon_SetLUTData */
#define TCON_IOCTL_SET_LUT_DATA				TCON_IOWR(0x04, struct ti_lut_data)

/* t_Tcon_Set3DGLimit */
#define TCON_IOCTL_SET_3D_GLIMIT			TCON_IOWR(0x05, struct ti_3d_glimit)

/* t_Tcon_SetSpecialGlimit */
#define TCON_IOCTL_SET_SPECIAL_GLIMIT			TCON_IOWR(0x06, struct ti_special_glimit)

/* t_Tcon_SetPartition */
#define TCON_IOCTL_SET_PARTITION			TCON_IOWR(0x07, int)

/* t_Tcon_GetTemperature */
#define TCON_IOCTL_GET_TEMPERATURE			TCON_IOWR(0x08, int)

/* t_Tcon_SetGamma */
#define TCON_IOCTL_SET_GAMMA				TCON_IOWR(0x09, int)

/* t_Tcon_Set3DOptimize */
#define TCON_IOCTL_SET_3D_OPTIMIZE			TCON_IOWR(0x0A, int)

/* t_Tcon_Set3DEffect */
#define TCON_IOCTL_SET_3D_EFFECT			TCON_IOWR(0x0B, struct ti_3d_effect)

/* t_Tcon_CheckDCCSel */
#define TCON_IOCTL_CHECK_DCC_SEL			TCON_IOWR(0x0C, int)

/* t_Tcon_GetMultiACCChecksum */
#define TCON_IOCTL_GET_MULTI_ACC_CHECKSUM		TCON_IOWR(0x0D, struct ti_checksum)

/* t_Tcon_GetDccVersion */
#define TCON_IOCTL_GET_DCC_VERSION			TCON_IOWR(0x0E, int)

/* t_Tcon_UpdateEeprom */
#define TCON_IOCTL_UPDATE_EEPROM			TCON_IOWR(0x0F, struct ti_update_eeprom)

/* t_Tcon_Debug */
#define TCON_IOCTL_DEBUG				TCON_IOWR(0x10, int)

/* t_Tcon_SetCTRInit */
#define TCON_IOCTL_SET_CTR_INIT				TCON_IOWR(0x11, int)

/* t_Tcon_SetCTR */
#define TCON_IOCTL_SET_CTR				TCON_IOWR(0x12, struct ti_ctr)

/* t_Tcon_SetCTRValue */
#define TCON_IOCTL_SET_CTR_VALUE			TCON_IOWR(0x13, int)

/* t_Tcon_SetTestPattern */
#define TCON_IOCTL_SET_TEST_PATTERN			TCON_IOWR(0x14, struct ti_test_pattern)

/* t_Tcon_WriteDCCLUTData */
#define TCON_IOCTL_WRITE_DCC_LUT_DATA			TCON_IOWR(0x15, int)

/* t_Tcon_WriteACCLUTData */
#define TCON_IOCTL_WRITE_ACC_LUT_DATA			TCON_IOWR(0x16, int)

/* t_Tcon_OperateLUTData */
#define TCON_IOCTL_OPERATE_LUT_DATA			TCON_IOWR(0x17, int)

/* t_Tcon_ControlDCCLevel */
#define TCON_IOCTL_CONTROL_DCC_LEVEL			TCON_IOWR(0x18, int)

/* t_Tcon_CheckTCONStatus */
#define TCON_IOCTL_CHECK_TCON_STATUS			TCON_IOWR(0x19, int)

/* t_Tcon_SetPanelMute */
#define TCON_IOCTL_SET_PANEL_MUTE			TCON_IOWR(0x1A, int)

/* t_Tcon_SetPanelInverter */
#define TCON_IOCTL_SET_PANEL_INVERTER			TCON_IOWR(0x1B, int)

/* t_Tcon_SetPanelSystem */
#define TCON_IOCTL_SET_PANEL_SYSTEM			TCON_IOWR(0x1C, int)

/* t_Tcon_GetI2CDelay */
#define TCON_IOCTL_GET_I2C_DELAY			TCON_IOWR(0x1D, int)

/* t_Tcon_SetMultiACC */
#define TCON_IOCTL_SET_MULTI_ACC			TCON_IOWR(0x1E, int)

/* t_Tcon_WriteDefaultGammaData */
#define TCON_IOCTL_WRITE_DEFAULT_GAMMA_DATA		TCON_IOWR(0x1F, int)

/* t_Tcon_SetI2CFlagOnOff */
#define TCON_IOCTL_SET_I2C_FLAG_ON_OFF			TCON_IOWR(0x20, int)

/* t_Tcon_SetAsyncDccUpdate */
#define TCON_IOCTL_SET_ASYNC_DCC_UPDATE			TCON_IOWR(0x21, int)

/* t_Tcon_CheckCrcTcon */
#define TCON_IOCTL_CHECK_CRC_TCON			TCON_IOWR(0x22, int)

/* t_Tcon_DccDitherOnOff */
#define TCON_IOCTL_DCC_DITHER_ON_OFF			TCON_IOWR(0x23, int)

/* t_Tcon_GetTCONFlashVersion */
#define TCON_IOCTL_GET_TCON_FLASH_VERSION		TCON_IOWR(0x24, struct ti_flash_version)

/* t_Tcon_Set3DOnOff */
#define TCON_IOCTL_SET_3D_ON_OFF			TCON_IOWR(0x25, int)

/* t_Tcon_SetMute */
#define TCON_IOCTL_SET_MUTE				TCON_IOWR(0x26, int)

/* t_Tcon_WaitTconRecoveryTime */
#define TCON_IOCTL_WAIT_TCON_RECOVERY_TIME		TCON_IOWR(0x27, int)

/* t_Tcon_Set3DViewType */
#define TCON_IOCTL_SET_3D_VIEW_TYPE			TCON_IOWR(0x28, int)

/* t_Tcon_SetDetail3DViewMode */
#define TCON_IOCTL_SET_DETAIL_3D_VIEW_MODE		TCON_IOWR(0x29, int)

/* t_Tcon_SetEmitterPulse */
#define TCON_IOCTL_SET_EMMITER_PULSE			TCON_IOWR(0x2A, int)

/* t_Tcon_SetLedMotionPlus */
#define TCON_IOCTL_SET_LED_MOTION_PLUS			TCON_IOWR(0x2B, int)

/* t_Tcon_UpdatePanelEeprom */
#define TCON_IOCTL_UPDATE_PANEL_EEPROM			TCON_IOWR(0x2C, int)

/* t_Tcon_UpdatePanelFPGA */
#define TCON_IOCTL_UPDATE_PANELFPGA			TCON_IOWR(0x2D, int)

/* t_Tcon_SetVFreq50Hz */
#define TCON_IOCTL_SET_V_FREQ_50_HZ			TCON_IOWR(0x2E, int)

/* t_Tcon_SetPanelDrive */
#define TCON_IOCTL_SET_PANEL_DRIVE			TCON_IOWR(0x2F, int)

/* t_Tcon_SetGameModeOnOff */
#define TCON_IOCTL_SET_GAME_MODE_ON_OFF			TCON_IOWR(0x30, int)

/* t_Tcon_GetTconRecoveryTime */
#define TCON_IOCTL_GET_TCON_RECOVERY_TIME		TCON_IOWR(0x31, int)

/* t_Tcon_SetMicomICContext */
#define TCON_IOCTL_SET_MINICOM_IC_CONTEXT		TCON_IOWR(0x32, int)

/* t_Tcon_SetPanelWp */
#define TCON_IOCTL_SET_PANEL_WP				TCON_IOWR(0x33, struct ti_panel_wp)

/* t_Tcon_SetI2CSW */
#define TCON_IOCTL_SET_I2C_SW				TCON_IOWR(0x34, struct ti_i2c_sw)

/* t_Tcon_GetFactoryTconPartition */
#define TCON_IOCTL_GET_FACTORY_TCON_PARTITION		TCON_IOWR(0x35, struct ti_get_factory_tcon)

/* t_Tcon_SetFactoryTconPartition */
#define TCON_IOCTL_SET_FACTORY_TCON_PARTITION		TCON_IOWR(0x36, struct ti_factory_tcon_partition)

/* t_Tcon_GetFactoryDebugMode */
#define TCON_IOCTL_GET_FACTORY_DEBUG_MODE		TCON_IOWR(0x37, int)

/* t_Tcon_GetFactoryDCCSmallDebug */
#define TCON_IOCTL_GET_FACTORY_DCC_SMALL_DEBUG		TCON_IOWR(0x38, int)

/* t_Tcon_GetFactoryDCCDebug */
#define TCON_IOCTL_GET_FACTORY_DCC_DEBUG		TCON_IOWR(0x39, struct ti_get_factory_dcc)

/* t_Tcon_SetFactoryDCCDebug */
#define TCON_IOCTL_SET_FACTORY_DCC_DEBUG		TCON_IOWR(0x3A, struct ti_factory_dcc_debug)

/* t_Tcon_GetFacatoryTemperature */
#define TCON_IOCTL_GET_FACTORY_TEMPERATURE		TCON_IOWR(0x3B, int)

/* t_Tcon_GetFacatoryTemperature_Small */
#define TCON_IOCTL_GET_FACTORY_TEMP_SMALL		TCON_IOWR(0x3C, int)

/* t_Tcon_Set3DOptimizeOffset */
#define TCON_IOCTL_SET_3D_OPTIMIZER_OFFSET		TCON_IOWR(0x3D, int)

/* t_Tcon_Get3DOptimizeOffset */
#define TCON_IOCTL_GET_3D_OPTIMIZER_OFFSET		TCON_IOWR(0x3E, int)

/* t_Tcon_Get3DOnOff */
#define TCON_IOCTL_GET_3D_ON_OFF			TCON_IOWR(0x3F, int)

/* t_Tcon_SetResolutionFreq */
#define TCON_IOCTL_SET_RESOLUTION_FREQ			TCON_IOWR(0x40, int)

/* t_Tcon_GetResolutionFreq */
#define TCON_IOCTL_GET_RESOLUTION_FREQ			TCON_IOWR(0x41, int)

/* t_Tcon_SetGameOnOff */
#define TCON_IOCTL_SET_GAME_ON_OFF			TCON_IOWR(0x42, int)

/* t_Tcon_GetGameOnOff */
#define TCON_IOCTL_GET_GAME_ON_OFF			TCON_IOWR(0x43, int)

/* t_SetTCONReset */
#define TCON_IOCTL_SET_TCON_RESET			TCON_IOWR(0x44, int)

/* t_Tcon_SetGlimit_Small */
#define TCON_IOCTL_SET_GLIMIT_SMALL			TCON_IOWR(0x45, int)

/* t_Tcon_SetGlimit */
#define TCON_IOCTL_SET_GLIMIT				TCON_IOWR(0x46, struct ti_glimit)

/* t_Tcon_SetDebugPrint */
#define TCON_IOCTL_SET_DEBUG_PRINT			TCON_IOWR(0x47, int)

/* t_Tcon_GetDebugPrint */
#define TCON_IOCTL_GET_DEBUG_PRINT			TCON_IOWR(0x48, int)

/* t_Tcon_SetPictureInformation */
#define TCON_IOCTL_SET_PICTURE_INFO			TCON_IOWR(0x49, struct fi_TDSourceResInfo_t)

/* t_Tcon_GetPictureInformation */
#define TCON_IOCTL_GET_PICTURE_INFO			TCON_IOWR(0x4A, int)

/* t_Tcon_Set3DTransportMode */
#define TCON_IOCTL_SET_3D_TRANSPORT_MODE		TCON_IOWR(0x4B, int)

/* t_Tcon_Get3DTransportMode */
#define TCON_IOCTL_GET_3D_TRANSPORT_MODE		TCON_IOWR(0x4C, int)

/* t_Tcon_IsLUTData */
#define TCON_IOCTL_IS_LUT_DATA				TCON_IOWR(0x4D, struct ti_is_lut_data)

/* t_Tcon_IsLUTDataOfBrightness */
#define TCON_IOCTL_IS_LUT_DATA_OF_BRI			TCON_IOWR(0x4E, struct ti_is_lut_data)

/* t_Tcon_IsMultiACCData */
#define TCON_IOCTL_IS_MULTI_ACC_DATA			TCON_IOWR(0x4F, struct ti_multi_acc_data)

/* t_Tcon_GetMultiACCMode */
#define TCON_IOCTL_GET_MULTI_ACC_MODE			TCON_IOWR(0x50, int)

/* t_Tcon_SetVideoMuteState */
#define TCON_IOCTL_SET_VIDEO_MUTE_STATE			TCON_IOWR(0x51, int)

/* t_Tcon_GetFactoryUSBDebugMode */
#define TCON_IOCTL_GET_FACTORY_USB_DEBUG_MODE		TCON_IOWR(0x52, int)

/* t_Tcon_GetFactoryBCTR */
#define TCON_IOCTL_GET_FACTORY_BCTR			TCON_IOWR(0x53, struct ti_tcon_bctr_t)

/* t_Tcon_SetFactoryBCTR */
#define TCON_IOCTL_SET_FACTORY_BCTR			TCON_IOWR(0x54, struct ti_factory_bctr)

/* t_Tcon_SetPanelInfo */
#define TCON_IOCTL_SET_PANEL_INFO			TCON_IOWR(0x55, struct ti_panel_info)

/* t_Tcon_Set3DBrightnessInformation */
#define TCON_IOCTL_SET_3D_BRI_INFO			TCON_IOWR(0x56, int)

/* t_Tcon_Get3DBrightnessInformation */
#define TCON_IOCTL_GET_3D_BRI_INFO			TCON_IOWR(0x57, int)

/* t_Tcon_Set3dLightControlSupport */
#define TCON_IOCTL_SET_3D_LIGHT_CTRL_SUPPORT		TCON_IOWR(0x58, int)

/* t_Tcon_PanelString */
#define TCON_IOCTL_PANEL_STRING				TCON_IOWR(0x59, struct ti_panel_string)

/* t_Tcon_GetPanelYear */
#define TCON_IOCTL_GET_PANEL_YEAR			TCON_IOWR(0x5A, int)

/* t_Tcon_CalCTRValue */
#define TCON_IOCTL_CAL_CTL_VALUE			TCON_IOWR(0x5B, int)

/* t_Tcon_GetGlimitFactoryValue */
#define TCON_IOCTL_GET_GLIMIT_FACTORY_VALUE		TCON_IOWR(0x5C, struct ti_factory_glimit)

/* t_Tcon_GetGlimitValue */
#define TCON_IOCTL_GET_GLIMIT_VALUE			TCON_IOWR(0x5D, int)

/* t_Tcon_CalcGlimit */
#define TCON_IOCTL_CALC_GLIMIT				TCON_IOWR(0x5E, int)

/* get tcon type */
#define TCON_IOCTL_GET_TCON_TYPE			TCON_IOWR(0x60, int)

/* tztv_i2c_read_tcon, tztv_i2c_read_tcon_gamma, tztv_i2c_read_tcon_temp_sensor, tztv_i2c_read_tcon_eeprom */
#define TCON_IOCTL_I2C_READ				TCON_IOWR(0x61, struct fti_i2c)

/* tztv_i2c_write_tcon, tztv_i2c_write_tcon_gamma, tztv_i2c_write_tcon_temp_sensor, tztv_i2c_write_tcon_eeprom */
#define TCON_IOCTL_I2C_WRITE				TCON_IOWR(0x62, struct fti_i2c)

/* factory data */
#define TCON_IOCTL_FACTORY_DATA				TCON_IOWR(0x63, struct ti_tcon_factory_data)

/* Get Time To Cold */
#define TCON_IOCTL_GET_TIME_TO_COLD				TCON_IOWR(0x64, int)

/* Load TCON Driver */
#define TCON_IOCTL_LOAD_DRIVER				TCON_IOWR(0x65, int)

/* Set InstantOn Flag */
#define TCON_IOCTL_SET_INSTANT_ON_FLAG				TCON_IOWR(0x66, int)

/* Get InstantOn Flag */
#define TCON_IOCTL_GET_INSTANT_ON_FLAG				TCON_IOWR(0x67, int)

/* Get Cooling Mode */
#define TCON_IOCTL_GET_COOLING_MODE				TCON_IOWR(0x68, int)

/* Get Warming Time */
#define TCON_IOCTL_GET_WARMING_TIME				TCON_IOWR(0x69, int)

/* Set Current Temperature */
#define TCON_IOCTL_SET_CURRENT_TEMPERATURE			TCON_IOWR(0x6A, int)

/* Set Last Temperature */
#define TCON_IOCTL_SET_LAST_TEMPERATURE			TCON_IOWR(0x6B, int)

/* Demura Upgrade */
#define TCON_IOCTL_UPGRADE_DEMURA_FIRMWARE			TCON_IOWR(0x6C, int)

/* Set Demura Bypass */
#define TCON_IOCTL_SET_DEMURA_BYPASS					TCON_IOWR(0x6D, int)

/* Set Panel Curved Mode */
#define TCON_IOCTL_SET_PANEL_CURVED_MODE			TCON_IOWR(0x6E, int)

#ifdef  __cplusplus
}
#endif


#endif	/* __UAPI__TZTV__FRC__TCON__H__ */
