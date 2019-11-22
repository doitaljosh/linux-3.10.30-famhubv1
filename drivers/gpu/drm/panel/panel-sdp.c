/*
 * Copyright (C) 2014 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/backlight.h>
#include <drm/drmP.h>
#include <drm/drm_panel.h>

#define DA_FHUB_LCD_WIDTH_MM    477
#define DA_FHUB_LCD_HEIGHT_MM   268

struct sdp_panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	struct {
		unsigned int width;
		unsigned int height;
	} size;
};

struct sdp_panel_data {
	struct drm_panel base;
	struct backlight_device *backlight;
	const struct sdp_panel_desc *desc;
	bool enabled;
};

static const struct drm_display_mode sdp_panel_mode = {
	.clock = 0,
	.hdisplay = 1920,
	.hsync_start = 1920,
	.hsync_end = 1920,
	.htotal = 1920,
	.vdisplay = 1080,
	.vsync_start = 1080,
	.vsync_end = 1080,
	.vtotal = 1080,
	.vrefresh = 60,
};

static const struct sdp_panel_desc sdp_panel_desc = {
	.modes = &sdp_panel_mode,
	.num_modes = 1,
	.size = {
		.width = 1920,
		.height = 1080,
	},
};

static inline struct sdp_panel_data *to_sdp_panel_data(struct drm_panel *panel)
{
	return container_of(panel, struct sdp_panel_data, base);
}

static int sdp_panel_disable(struct drm_panel *panel)
{
	struct sdp_panel_data *p = to_sdp_panel_data(panel);

	if (!p->enabled)
		return 0;

	if (p->backlight) {
		p->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(p->backlight);
	}

	p->enabled = false;

	return 0;
}

static int sdp_panel_enable(struct drm_panel *panel)
{
	struct sdp_panel_data *p = to_sdp_panel_data(panel);

	if (p->enabled)
		return 0;

	if (p->backlight) {
		p->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(p->backlight);
	}

	p->enabled = true;

	return 0;
}

static int sdp_panel_get_modes(struct drm_panel *panel)
{
	struct sdp_panel_data *p = to_sdp_panel_data(panel);
	struct drm_connector *connector = p->base.connector;
	struct drm_device *drm = p->base.drm;
	struct drm_display_mode *mode;
	unsigned int i, num = 0;

	for (i = 0; i < p->desc->num_modes; i++) {
		const struct drm_display_mode *m = &p->desc->modes[i];

		mode = drm_mode_duplicate(drm, m);
		if (!mode) {
			dev_err(drm->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, m->vrefresh);
			continue;
		}

		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		num++;
	}

	connector->display_info.width_mm = DA_FHUB_LCD_WIDTH_MM;    //p->desc->size.width;
	connector->display_info.height_mm = DA_FHUB_LCD_HEIGHT_MM;  //p->desc->size.height;

	return num;
}

static const struct drm_panel_funcs sdp_panel_funcs = {
	.disable	= sdp_panel_disable,
	.enable		= sdp_panel_enable,
	.get_modes	= sdp_panel_get_modes,
};

static int sdp_panel_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *backlight;
	struct sdp_panel_data *panel;
	int ret;

	if (!node) {
		dev_err(&pdev->dev, "device tree node not found\n");
		return -ENXIO;
	}

	panel = devm_kzalloc(&pdev->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	panel->desc = &sdp_panel_desc;

	backlight = of_parse_phandle(node, "backlight", 0);
	if (backlight) {
		panel->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!panel->backlight)
			return -EPROBE_DEFER;

		if (panel->backlight->props.power == FB_BLANK_UNBLANK)
			panel->enabled = true;
	}

	drm_panel_init(&panel->base);
	panel->base.dev = &pdev->dev;
	panel->base.funcs = &sdp_panel_funcs;

	ret = drm_panel_add(&panel->base);
	if (ret < 0)
		goto err;

	platform_set_drvdata(pdev, panel);

	return 0;

err:
	if (panel->backlight)
		put_device(&panel->backlight->dev);

	return ret;
}

static int sdp_panel_remove(struct platform_device *pdev)
{
	struct sdp_panel_data *panel = platform_get_drvdata(pdev);

	drm_panel_detach(&panel->base);
	drm_panel_remove(&panel->base);

	if (panel->backlight)
		put_device(&panel->backlight->dev);

	return 0;
}

static const struct of_device_id sdp_panel_dt_match[] = {
	{ .compatible = "samsung,sdp1202-panel", },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_panel_dt_match);

static struct platform_driver sdp_panel_driver = {
	.probe		= sdp_panel_probe,
	.remove		= sdp_panel_remove,
	.driver = {
		.name	= "sdp-panel",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_panel_dt_match),
	},
};

module_platform_driver(sdp_panel_driver);

MODULE_DESCRIPTION("Samsung SDP SoCs DRM panel driver");
MODULE_LICENSE("GPL v2");
