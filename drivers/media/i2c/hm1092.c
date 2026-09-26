// SPDX-License-Identifier: GPL-2.0-only
/*
 * Himax HM1092 near-infrared image sensor driver
 *
 * The register sequences are the ones the Windows driver programs, as captured
 * for the out-of-tree driver in the svp7500-camera-fix-pack project.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define HM1092_MCLK_FREQ_19_2MHZ	19200000
#define HM1092_LINK_FREQ_180MHZ		180480000ULL
#define HM1092_DATA_LANES		1
#define HM1092_BPP			10

/*
 * The sensor does not auto-increment the register address across a multi-byte
 * access, so 16-bit registers are accessed as two 8-bit transfers (high byte
 * at the lower address).
 */
#define HM1092_REG_CHIP_ID		0x0000
#define HM1092_CHIP_ID			0x1091

#define HM1092_REG_MODE_SELECT		CCI_REG8(0x0100)
#define HM1092_MODE_STANDBY		0x00
#define HM1092_MODE_STREAMING		0x01

/* Latches the exposure, gain and frame length registers written before it */
#define HM1092_REG_COMMAND_UPDATE	CCI_REG8(0x0104)

#define HM1092_REG_EXPOSURE		0x0202
#define HM1092_EXPOSURE_MIN		2
#define HM1092_EXPOSURE_MAX_MARGIN	2
#define HM1092_EXPOSURE_STEP		1
#define HM1092_EXPOSURE_DEFAULT		668	/* 30 ms, as the Windows driver runs it */

/* Linear: gain = (16 + code) / 16, so 0 is 1x and 240 is 16x */
#define HM1092_REG_ANALOGUE_GAIN	CCI_REG8(0x0205)
#define HM1092_ANALOGUE_GAIN_MIN	0
#define HM1092_ANALOGUE_GAIN_MAX	240
#define HM1092_ANALOGUE_GAIN_STEP	1
#define HM1092_ANALOGUE_GAIN_DEFAULT	0x00

#define HM1092_REG_DIGITAL_GAIN		0x020e
#define HM1092_DIGITAL_GAIN_MIN		0x0100
#define HM1092_DIGITAL_GAIN_MAX		0x03ff
#define HM1092_DIGITAL_GAIN_STEP	1
#define HM1092_DIGITAL_GAIN_DEFAULT	0x0100

#define HM1092_REG_VTS			0x0340
#define HM1092_VTS_MAX			0xffff

/* Pixel array read out by the 2x2 binned mode */
#define HM1092_PIXEL_ARRAY_WIDTH	1296
#define HM1092_PIXEL_ARRAY_HEIGHT	736

#define to_hm1092(_sd)			container_of(_sd, struct hm1092, sd)

static const s64 hm1092_link_freq_menu[] = {
	HM1092_LINK_FREQ_180MHZ,
};

struct hm1092_reg_list {
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

struct hm1092_mode {
	u32 width;
	u32 height;
	u32 hts;
	u32 vts;
	const struct hm1092_reg_list reg_list;
};

static const char * const hm1092_supply_names[] = {
	"avdd",		/* Analogue power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define HM1092_NUM_SUPPLIES	ARRAY_SIZE(hm1092_supply_names)

struct hm1092 {
	struct device *dev;
	struct regmap *regmap;
	struct clk *mclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[HM1092_NUM_SUPPLIES];

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl_handler ctrl_handler;
};

static const struct cci_reg_sequence hm1092_common_regs[] = {
	{ CCI_REG8(0x0103), 0x00 },
	{ CCI_REG8(0x0307), 0x00 },
	{ CCI_REG8(0x0309), 0x01 },
	{ CCI_REG8(0x030a), 0x05 },
	{ CCI_REG8(0x030d), 0x0a },
	{ CCI_REG8(0x030f), 0x5e },
	{ CCI_REG8(0x0310), 0x00 },
	{ CCI_REG8(0x0350), 0x53 },
	{ CCI_REG8(0x3110), 0x02 },
	{ CCI_REG8(0x3735), 0xe2 },
	{ CCI_REG8(0x3704), 0x04 },
	{ CCI_REG8(0x4001), 0x00 },
	{ CCI_REG8(0x4002), 0x2b },
	{ CCI_REG8(0x4024), 0x40 },
	{ CCI_REG8(0x4131), 0x01 },
	{ CCI_REG8(0x4132), 0x20 },
	{ CCI_REG8(0x4265), 0x02 },
	{ CCI_REG8(0x4b04), 0x01 },
	{ CCI_REG8(0x4b0e), 0x0e },
	{ CCI_REG8(0x4b18), 0x00 },
	{ CCI_REG8(0x4b20), 0x9e },
	{ CCI_REG8(0x4b31), 0x06 },
	{ CCI_REG8(0x4b3b), 0x02 },
	{ CCI_REG8(0x4b3e), 0x00 },
	{ CCI_REG8(0x4b44), 0x0c },
	{ CCI_REG8(0x4b45), 0x01 },
	{ CCI_REG8(0x4b47), 0x00 },
	{ CCI_REG8(0x5004), 0x40 },
	{ CCI_REG8(0x5005), 0x28 },
	{ CCI_REG8(0x5006), 0x40 },
	{ CCI_REG8(0x5007), 0x28 },
	{ CCI_REG8(0x5010), 0x20 },
	{ CCI_REG8(0x5011), 0x00 },
	{ CCI_REG8(0x5013), 0x03 },
	{ CCI_REG8(0x5015), 0xb3 },
	{ CCI_REG8(0x501d), 0x4c },
	{ CCI_REG8(0x5098), 0x00 },
	{ CCI_REG8(0x5099), 0x11 },
	{ CCI_REG8(0x509b), 0x03 },
	{ CCI_REG8(0x50a0), 0x30 },
	{ CCI_REG8(0x50a2), 0x0b },
	{ CCI_REG8(0x50a6), 0x00 },
	{ CCI_REG8(0x50a7), 0x00 },
	{ CCI_REG8(0x50aa), 0x22 },
	{ CCI_REG8(0x50ab), 0x07 },
	{ CCI_REG8(0x50ac), 0x24 },
	{ CCI_REG8(0x50ad), 0x07 },
	{ CCI_REG8(0x50ae), 0x20 },
	{ CCI_REG8(0x50af), 0x40 },
	{ CCI_REG8(0x50b3), 0x04 },
	{ CCI_REG8(0x50b4), 0x00 },
	{ CCI_REG8(0x50b7), 0x00 },
	{ CCI_REG8(0x50b8), 0x70 },
	{ CCI_REG8(0x50b9), 0xff },
	{ CCI_REG8(0x50ba), 0xff },
	{ CCI_REG8(0x50bb), 0x14 },
	{ CCI_REG8(0x50cb), 0x21 },
	{ CCI_REG8(0x50d5), 0xe0 },
	{ CCI_REG8(0x50d7), 0x12 },
	{ CCI_REG8(0x50dd), 0x00 },
	{ CCI_REG8(0x50e8), 0x00 },
	{ CCI_REG8(0x50ea), 0x74 },
	{ CCI_REG8(0x50fa), 0x02 },
	{ CCI_REG8(0x5100), 0x03 },
	{ CCI_REG8(0x5101), 0x13 },
	{ CCI_REG8(0x5102), 0x23 },
	{ CCI_REG8(0x5103), 0x33 },
	{ CCI_REG8(0x5104), 0x43 },
	{ CCI_REG8(0x5105), 0x42 },
	{ CCI_REG8(0x5106), 0x40 },
	{ CCI_REG8(0x5118), 0x00 },
	{ CCI_REG8(0x5119), 0x00 },
	{ CCI_REG8(0x511a), 0x00 },
	{ CCI_REG8(0x511b), 0x00 },
	{ CCI_REG8(0x511c), 0x00 },
	{ CCI_REG8(0x511d), 0x00 },
	{ CCI_REG8(0x511e), 0x00 },
	{ CCI_REG8(0x5130), 0x13 },
	{ CCI_REG8(0x5131), 0x23 },
	{ CCI_REG8(0x5132), 0x33 },
	{ CCI_REG8(0x5133), 0x43 },
	{ CCI_REG8(0x5134), 0x42 },
	{ CCI_REG8(0x5135), 0x40 },
	{ CCI_REG8(0x5136), 0x40 },
	{ CCI_REG8(0x5148), 0x01 },
	{ CCI_REG8(0x5149), 0x01 },
	{ CCI_REG8(0x514a), 0x01 },
	{ CCI_REG8(0x514b), 0x01 },
	{ CCI_REG8(0x514c), 0x01 },
	{ CCI_REG8(0x514d), 0x01 },
	{ CCI_REG8(0x514e), 0x01 },
	{ CCI_REG8(0x51c0), 0x00 },
	{ CCI_REG8(0x51c1), 0x81 },
	{ CCI_REG8(0x51c2), 0xec },
	{ CCI_REG8(0x51c3), 0x00 },
	{ CCI_REG8(0x51c4), 0x55 },
	{ CCI_REG8(0x51c5), 0x44 },
	{ CCI_REG8(0x51c6), 0x00 },
	{ CCI_REG8(0x51c7), 0x81 },
	{ CCI_REG8(0x51c8), 0xec },
	{ CCI_REG8(0x51c9), 0x00 },
	{ CCI_REG8(0x51ca), 0x55 },
	{ CCI_REG8(0x51cb), 0x24 },
	{ CCI_REG8(0x51cc), 0x00 },
	{ CCI_REG8(0x51cd), 0x81 },
	{ CCI_REG8(0x51ce), 0xec },
	{ CCI_REG8(0x51cf), 0x00 },
	{ CCI_REG8(0x51d0), 0x54 },
	{ CCI_REG8(0x51d1), 0x24 },
	{ CCI_REG8(0x51d2), 0x00 },
	{ CCI_REG8(0x51d3), 0x81 },
	{ CCI_REG8(0x51d4), 0xec },
	{ CCI_REG8(0x51d5), 0x00 },
	{ CCI_REG8(0x51d6), 0x53 },
	{ CCI_REG8(0x51d7), 0x14 },
	{ CCI_REG8(0x51d8), 0x00 },
	{ CCI_REG8(0x51d9), 0x81 },
	{ CCI_REG8(0x51da), 0xec },
	{ CCI_REG8(0x51db), 0x00 },
	{ CCI_REG8(0x51dc), 0x53 },
	{ CCI_REG8(0x51dd), 0x14 },
	{ CCI_REG8(0x51e0), 0x09 },
	{ CCI_REG8(0x51e1), 0x03 },
	{ CCI_REG8(0x51e2), 0x04 },
	{ CCI_REG8(0x51e3), 0x03 },
	{ CCI_REG8(0x51e4), 0x08 },
	{ CCI_REG8(0x51e5), 0x07 },
	{ CCI_REG8(0x51e6), 0x08 },
	{ CCI_REG8(0x51e7), 0x07 },
	{ CCI_REG8(0x51e8), 0x04 },
	{ CCI_REG8(0x51e9), 0x46 },
	{ CCI_REG8(0x51ea), 0x43 },
	{ CCI_REG8(0x51eb), 0x62 },
	{ CCI_REG8(0x51ec), 0x61 },
	{ CCI_REG8(0x51ed), 0x00 },
	{ CCI_REG8(0x51ee), 0x00 },
	{ CCI_REG8(0x5200), 0x60 },
	{ CCI_REG8(0x5201), 0x80 },
	{ CCI_REG8(0x5202), 0x00 },
	{ CCI_REG8(0x5203), 0x01 },
	{ CCI_REG8(0x5206), 0x80 },
	{ CCI_REG8(0x5208), 0x0b },
	{ CCI_REG8(0x5209), 0x0c },
	{ CCI_REG8(0x520c), 0x15 },
	{ CCI_REG8(0x520d), 0x40 },
	{ CCI_REG8(0x5214), 0x28 },
	{ CCI_REG8(0x5215), 0x04 },
	{ CCI_REG8(0x5216), 0x02 },
	{ CCI_REG8(0x5217), 0x01 },
	{ CCI_REG8(0x5218), 0x07 },
	{ CCI_REG8(0x521e), 0x01 },
	{ CCI_REG8(0x5282), 0xff },
	{ CCI_REG8(0x5283), 0x03 },
};

static const struct cci_reg_sequence hm1092_648x368_regs[] = {
	{ CCI_REG8(0x0342), 0x06 },	/* line length 1620 pixel clocks */
	{ CCI_REG8(0x0343), 0x54 },
	{ CCI_REG8(0x0344), 0x00 },	/* analogue crop x start 0 */
	{ CCI_REG8(0x0345), 0x00 },
	{ CCI_REG8(0x0346), 0x00 },	/* analogue crop y start 0 */
	{ CCI_REG8(0x0347), 0x00 },
	{ CCI_REG8(0x0348), 0x05 },	/* analogue crop x end 0x050d */
	{ CCI_REG8(0x0349), 0x0d },
	{ CCI_REG8(0x034a), 0x02 },	/* analogue crop y end 0x02dd */
	{ CCI_REG8(0x034b), 0xdd },
	{ CCI_REG8(0x034c), 0x02 },	/* output width 648 */
	{ CCI_REG8(0x034d), 0x88 },
	{ CCI_REG8(0x034e), 0x01 },	/* output height 368 */
	{ CCI_REG8(0x034f), 0x70 },
	{ CCI_REG8(0x0383), 0x00 },
	{ CCI_REG8(0x0387), 0x10 },
	{ CCI_REG8(0x0390), 0x03 },
	{ CCI_REG8(0x4800), 0xac },
	{ HM1092_REG_COMMAND_UPDATE, 0x01 },
	{ HM1092_REG_COMMAND_UPDATE, 0x00 },
	{ CCI_REG8(0x4801), 0xae },
	{ CCI_REG8(0x4b20), 0x9e },
	{ CCI_REG8(0x0101), 0x03 },
};

static const struct hm1092_mode supported_modes[] = {
	{
		.width = 648,
		.height = 368,
		.hts = 1620,
		.vts = 741,
		.reg_list = {
			.regs = hm1092_648x368_regs,
			.num_regs = ARRAY_SIZE(hm1092_648x368_regs),
		},
	},
};

static void hm1092_write16(struct hm1092 *hm1092, u16 reg, u16 val, int *err)
{
	cci_write(hm1092->regmap, CCI_REG8(reg), val >> 8, err);
	cci_write(hm1092->regmap, CCI_REG8(reg + 1), val & 0xff, err);
}

static int hm1092_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hm1092 *hm1092 = container_of(ctrl->handler, struct hm1092,
					     ctrl_handler);
	const struct hm1092_mode *mode = &supported_modes[0];
	s64 exposure_max;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = ctrl->val + mode->height -
			       HM1092_EXPOSURE_MAX_MARGIN;
		ret = __v4l2_ctrl_modify_range(hm1092->exposure,
					       hm1092->exposure->minimum,
					       exposure_max,
					       hm1092->exposure->step,
					       hm1092->exposure->default_value);
		if (ret)
			return ret;
	}

	/* Controls are applied when the sensor is powered up for streaming */
	if (!pm_runtime_get_if_active(hm1092->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		hm1092_write16(hm1092, HM1092_REG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(hm1092->regmap, HM1092_REG_ANALOGUE_GAIN, ctrl->val,
			  &ret);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		hm1092_write16(hm1092, HM1092_REG_DIGITAL_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_VBLANK:
		hm1092_write16(hm1092, HM1092_REG_VTS, ctrl->val + mode->height,
			       &ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	cci_write(hm1092->regmap, HM1092_REG_COMMAND_UPDATE, 0x01, &ret);

	pm_runtime_put(hm1092->dev);

	return ret;
}

static const struct v4l2_ctrl_ops hm1092_ctrl_ops = {
	.s_ctrl = hm1092_set_ctrl,
};

static int hm1092_init_controls(struct hm1092 *hm1092)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &hm1092->ctrl_handler;
	const struct hm1092_mode *mode = &supported_modes[0];
	s64 exposure_max, pixel_rate, h_blank, v_blank;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *ctrl;
	int ret;

	v4l2_ctrl_handler_init(ctrl_hdlr, 10);

	ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &hm1092_ctrl_ops,
				      V4L2_CID_LINK_FREQ,
				      ARRAY_SIZE(hm1092_link_freq_menu) - 1,
				      0, hm1092_link_freq_menu);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = div_u64(hm1092_link_freq_menu[0] * 2 * HM1092_DATA_LANES,
			     HM1092_BPP);
	v4l2_ctrl_new_std(ctrl_hdlr, &hm1092_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);

	h_blank = mode->hts - mode->width;
	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &hm1092_ctrl_ops, V4L2_CID_HBLANK,
				 h_blank, h_blank, 1, h_blank);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v_blank = mode->vts - mode->height;
	hm1092->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &hm1092_ctrl_ops,
					   V4L2_CID_VBLANK, v_blank,
					   HM1092_VTS_MAX - mode->height, 1,
					   v_blank);

	exposure_max = mode->vts - HM1092_EXPOSURE_MAX_MARGIN;
	hm1092->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &hm1092_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     HM1092_EXPOSURE_MIN,
					     exposure_max,
					     HM1092_EXPOSURE_STEP,
					     HM1092_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &hm1092_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  HM1092_ANALOGUE_GAIN_MIN, HM1092_ANALOGUE_GAIN_MAX,
			  HM1092_ANALOGUE_GAIN_STEP,
			  HM1092_ANALOGUE_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &hm1092_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  HM1092_DIGITAL_GAIN_MIN, HM1092_DIGITAL_GAIN_MAX,
			  HM1092_DIGITAL_GAIN_STEP,
			  HM1092_DIGITAL_GAIN_DEFAULT);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		goto error_free_hdlr;
	}

	ret = v4l2_fwnode_device_parse(hm1092->dev, &props);
	if (ret)
		goto error_free_hdlr;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &hm1092_ctrl_ops,
					      &props);
	if (ret)
		goto error_free_hdlr;

	hm1092->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error_free_hdlr:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static void hm1092_update_pad_format(const struct hm1092_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_Y10_1X10;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int hm1092_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	const struct hm1092_reg_list *reg_list = &supported_modes[0].reg_list;
	struct hm1092 *hm1092 = to_hm1092(sd);
	int ret;

	ret = pm_runtime_resume_and_get(hm1092->dev);
	if (ret)
		return ret;

	ret = cci_multi_reg_write(hm1092->regmap, hm1092_common_regs,
				  ARRAY_SIZE(hm1092_common_regs), NULL);
	if (ret) {
		dev_err(hm1092->dev, "failed to write common registers: %d\n",
			ret);
		goto error;
	}

	ret = cci_multi_reg_write(hm1092->regmap, reg_list->regs,
				  reg_list->num_regs, NULL);
	if (ret) {
		dev_err(hm1092->dev, "failed to set mode: %d\n", ret);
		goto error;
	}

	ret = __v4l2_ctrl_handler_setup(hm1092->sd.ctrl_handler);
	if (ret)
		goto error;

	ret = cci_write(hm1092->regmap, HM1092_REG_MODE_SELECT,
			HM1092_MODE_STREAMING, NULL);
	if (ret) {
		dev_err(hm1092->dev, "failed to start streaming: %d\n", ret);
		goto error;
	}

	return 0;

error:
	pm_runtime_put_autosuspend(hm1092->dev);

	return ret;
}

static int hm1092_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct hm1092 *hm1092 = to_hm1092(sd);
	int ret;

	ret = cci_write(hm1092->regmap, HM1092_REG_MODE_SELECT,
			HM1092_MODE_STANDBY, NULL);
	if (ret)
		dev_err(hm1092->dev, "failed to stop streaming: %d\n", ret);

	pm_runtime_put_autosuspend(hm1092->dev);

	return ret;
}

static int hm1092_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;
	const struct hm1092_mode *mode;

	format = v4l2_subdev_state_get_format(state, 0);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width,
				      fmt->format.height);

	hm1092_update_pad_format(mode, &fmt->format);
	*format = fmt->format;

	return 0;
}

static int hm1092_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_Y10_1X10;

	return 0;
}

static int hm1092_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_Y10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int hm1092_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = HM1092_PIXEL_ARRAY_WIDTH;
		sel->r.height = HM1092_PIXEL_ARRAY_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static int hm1092_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			.code = MEDIA_BUS_FMT_Y10_1X10,
			.width = supported_modes[0].width,
			.height = supported_modes[0].height,
		},
	};

	return hm1092_set_pad_format(sd, state, &fmt);
}

static const struct v4l2_subdev_video_ops hm1092_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops hm1092_pad_ops = {
	.set_fmt = hm1092_set_pad_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.get_selection = hm1092_get_selection,
	.enum_mbus_code = hm1092_enum_mbus_code,
	.enum_frame_size = hm1092_enum_frame_size,
	.enable_streams = hm1092_enable_streams,
	.disable_streams = hm1092_disable_streams,
};

static const struct v4l2_subdev_ops hm1092_subdev_ops = {
	.video = &hm1092_video_ops,
	.pad = &hm1092_pad_ops,
};

static const struct v4l2_subdev_internal_ops hm1092_internal_ops = {
	.init_state = hm1092_init_state,
};

static const struct media_entity_operations hm1092_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int hm1092_identify_sensor(struct hm1092 *hm1092)
{
	u64 hi, lo;
	int ret = 0;

	cci_read(hm1092->regmap, CCI_REG8(HM1092_REG_CHIP_ID), &hi, &ret);
	cci_read(hm1092->regmap, CCI_REG8(HM1092_REG_CHIP_ID + 1), &lo, &ret);
	if (ret)
		return dev_err_probe(hm1092->dev, ret,
				     "failed to read chip id\n");

	if ((hi << 8 | lo) != HM1092_CHIP_ID)
		return dev_err_probe(hm1092->dev, -ENODEV,
				     "chip id mismatch: %x != %llx\n",
				     HM1092_CHIP_ID, hi << 8 | lo);

	return 0;
}

static int hm1092_check_hwcfg(struct hm1092 *hm1092)
{
	struct fwnode_handle *fwnode = dev_fwnode(hm1092->dev), *ep;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	unsigned long freq_bitmap;
	int ret;

	if (!fwnode)
		return -ENODEV;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != HM1092_DATA_LANES) {
		ret = dev_err_probe(hm1092->dev, -EINVAL,
				    "only %u data lane is supported\n",
				    HM1092_DATA_LANES);
		goto out_free;
	}

	ret = v4l2_link_freq_to_bitmap(hm1092->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       hm1092_link_freq_menu,
				       ARRAY_SIZE(hm1092_link_freq_menu),
				       &freq_bitmap);

out_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int hm1092_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct hm1092 *hm1092 = to_hm1092(sd);
	int ret;

	ret = regulator_bulk_enable(HM1092_NUM_SUPPLIES, hm1092->supplies);
	if (ret)
		return ret;

	ret = clk_prepare_enable(hm1092->mclk);
	if (ret)
		goto disable_regulators;

	gpiod_set_value_cansleep(hm1092->reset_gpio, 0);
	usleep_range(2000, 3000);

	return 0;

disable_regulators:
	regulator_bulk_disable(HM1092_NUM_SUPPLIES, hm1092->supplies);

	return ret;
}

static int hm1092_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct hm1092 *hm1092 = to_hm1092(sd);

	gpiod_set_value_cansleep(hm1092->reset_gpio, 1);
	clk_disable_unprepare(hm1092->mclk);
	regulator_bulk_disable(HM1092_NUM_SUPPLIES, hm1092->supplies);

	return 0;
}

static int hm1092_probe(struct i2c_client *client)
{
	struct hm1092 *hm1092;
	unsigned long freq;
	unsigned int i;
	int ret;

	hm1092 = devm_kzalloc(&client->dev, sizeof(*hm1092), GFP_KERNEL);
	if (!hm1092)
		return -ENOMEM;

	hm1092->dev = &client->dev;

	v4l2_i2c_subdev_init(&hm1092->sd, client, &hm1092_subdev_ops);

	hm1092->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(hm1092->regmap))
		return dev_err_probe(hm1092->dev, PTR_ERR(hm1092->regmap),
				     "failed to init CCI\n");

	hm1092->mclk = devm_v4l2_sensor_clk_get(hm1092->dev, NULL);
	if (IS_ERR(hm1092->mclk))
		return dev_err_probe(hm1092->dev, PTR_ERR(hm1092->mclk),
				     "failed to get MCLK\n");

	/* The PLL settings in the register tables assume this input clock */
	freq = clk_get_rate(hm1092->mclk);
	if (freq != HM1092_MCLK_FREQ_19_2MHZ)
		return dev_err_probe(hm1092->dev, -EINVAL,
				     "MCLK frequency %lu Hz is not supported\n",
				     freq);

	ret = hm1092_check_hwcfg(hm1092);
	if (ret)
		return dev_err_probe(hm1092->dev, ret,
				     "failed to check HW configuration\n");

	hm1092->reset_gpio = devm_gpiod_get_optional(hm1092->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(hm1092->reset_gpio))
		return dev_err_probe(hm1092->dev, PTR_ERR(hm1092->reset_gpio),
				     "failed to get reset GPIO\n");

	for (i = 0; i < HM1092_NUM_SUPPLIES; i++)
		hm1092->supplies[i].supply = hm1092_supply_names[i];

	ret = devm_regulator_bulk_get(hm1092->dev, HM1092_NUM_SUPPLIES,
				      hm1092->supplies);
	if (ret)
		return dev_err_probe(hm1092->dev, ret,
				     "failed to get supply regulators\n");

	ret = hm1092_power_on(hm1092->dev);
	if (ret)
		return ret;

	ret = hm1092_identify_sensor(hm1092);
	if (ret)
		goto power_off;

	ret = hm1092_init_controls(hm1092);
	if (ret) {
		dev_err_probe(hm1092->dev, ret, "failed to init controls\n");
		goto power_off;
	}

	hm1092->sd.state_lock = hm1092->ctrl_handler.lock;
	hm1092->sd.internal_ops = &hm1092_internal_ops;
	hm1092->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	hm1092->sd.entity.ops = &hm1092_subdev_entity_ops;
	hm1092->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	hm1092->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&hm1092->sd.entity, 1, &hm1092->pad);
	if (ret) {
		dev_err_probe(hm1092->dev, ret,
			      "failed to init media entity pads\n");
		goto v4l2_ctrl_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&hm1092->sd);
	if (ret) {
		dev_err_probe(hm1092->dev, ret,
			      "failed to finalize subdev init\n");
		goto media_entity_cleanup;
	}

	pm_runtime_set_active(hm1092->dev);
	pm_runtime_enable(hm1092->dev);

	ret = v4l2_async_register_subdev_sensor(&hm1092->sd);
	if (ret) {
		dev_err_probe(hm1092->dev, ret,
			      "failed to register V4L2 subdev\n");
		goto subdev_cleanup;
	}

	pm_runtime_set_autosuspend_delay(hm1092->dev, 1000);
	pm_runtime_use_autosuspend(hm1092->dev);
	pm_runtime_idle(hm1092->dev);

	return 0;

subdev_cleanup:
	v4l2_subdev_cleanup(&hm1092->sd);
	pm_runtime_disable(hm1092->dev);
	pm_runtime_set_suspended(hm1092->dev);

media_entity_cleanup:
	media_entity_cleanup(&hm1092->sd.entity);

v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(hm1092->sd.ctrl_handler);

power_off:
	hm1092_power_off(hm1092->dev);

	return ret;
}

static void hm1092_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hm1092 *hm1092 = to_hm1092(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(hm1092->dev);

	if (!pm_runtime_status_suspended(hm1092->dev)) {
		hm1092_power_off(hm1092->dev);
		pm_runtime_set_suspended(hm1092->dev);
	}
}

static DEFINE_RUNTIME_DEV_PM_OPS(hm1092_pm_ops, hm1092_power_off,
				 hm1092_power_on, NULL);

static const struct of_device_id hm1092_of_match[] = {
	{ .compatible = "himax,hm1092" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hm1092_of_match);

static struct i2c_driver hm1092_i2c_driver = {
	.driver = {
		.name = "hm1092",
		.pm = pm_ptr(&hm1092_pm_ops),
		.of_match_table = hm1092_of_match,
	},
	.probe = hm1092_probe,
	.remove = hm1092_remove,
};

module_i2c_driver(hm1092_i2c_driver);

MODULE_AUTHOR("Tyler Baker <forcedinductionz@gmail.com>");
MODULE_DESCRIPTION("Himax HM1092 near-infrared image sensor driver");
MODULE_LICENSE("GPL");
