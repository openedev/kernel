// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Amarula Solutions
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <video/mipi_display.h>

struct s070wv20 {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct backlight_device	*backlight;
	struct gpio_desc	*enable_gpio;
	struct gpio_desc	*reset_gpio;
	struct regulator	*power;
	const struct drm_display_mode *mode;
};

static inline struct s070wv20 *panel_to_s070wv20(struct drm_panel *panel)
{
	return container_of(panel, struct s070wv20, panel);
}

static inline int chipone_dsi_write(struct s070wv20 *icn,  const void *seq,
				    size_t len)
{
	struct mipi_dsi_device *dsi = icn->dsi;

	return mipi_dsi_generic_write(dsi, seq, len);
}

#define CHIPONE_DSI(icn, seq...)				\
	{							\
		const u8 d[] = { seq };				\
		chipone_dsi_write(icn, d, ARRAY_SIZE(d));	\
	}

static void icn6211_bridge_init(struct s070wv20 *icn)
{
	const struct drm_display_mode *mode = icn->mode;

	CHIPONE_DSI(icn, 0x7A, 0xC1);

	/* lower 8 bits of hdisplay */
	CHIPONE_DSI(icn, 0x20, mode->hdisplay & 0xff);

	/* lower 8 bits of vdisplay */
	CHIPONE_DSI(icn, 0x21, mode->vdisplay & 0xff);

	/**
	 * lsb nibble: 2nd nibble of hdisplay
	 * msb nibble: 2nd nibble of vdisplay
	 */
	CHIPONE_DSI(icn, 0x22, (((mode->hdisplay >> 8) & 0xf) |
		    (((mode->vdisplay >> 8) & 0xf) << 4)));

	/* HFP */
	CHIPONE_DSI(icn, 0x23, mode->hsync_start - mode->hdisplay);

	/* HSYNC */
	CHIPONE_DSI(icn, 0x24, mode->hsync_end - mode->hsync_start);

	/* HBP */
	CHIPONE_DSI(icn, 0x25, mode->htotal - mode->hsync_end);

	CHIPONE_DSI(icn, 0x26, 0x00);

	/* VFP */
	CHIPONE_DSI(icn, 0x27, mode->vsync_start - mode->vdisplay);

	/* VSYNC */
	CHIPONE_DSI(icn, 0x28, mode->vsync_end - mode->vsync_start);

	/* VBP */
	CHIPONE_DSI(icn, 0x29, mode->vtotal - mode->vsync_end);

	/* dsi specific sequence */
	CHIPONE_DSI(icn, MIPI_DCS_SET_TEAR_OFF, 0x80);
	CHIPONE_DSI(icn, MIPI_DCS_SET_ADDRESS_MODE, 0x28);
	CHIPONE_DSI(icn, 0xB5, 0xA0);
	CHIPONE_DSI(icn, 0x5C, 0xFF);
	CHIPONE_DSI(icn, MIPI_DCS_SET_COLUMN_ADDRESS, 0x01);
	CHIPONE_DSI(icn, MIPI_DCS_GET_POWER_SAVE, 0x92);
	CHIPONE_DSI(icn, 0x6B, 0x71);
	CHIPONE_DSI(icn, 0x69, 0x2B);
	CHIPONE_DSI(icn, MIPI_DCS_ENTER_SLEEP_MODE, 0x40);
	CHIPONE_DSI(icn, MIPI_DCS_EXIT_SLEEP_MODE, 0x98);

	/* icn6211 specific sequence */
	CHIPONE_DSI(icn, 0xB6, 0x20);
	CHIPONE_DSI(icn, 0x51, 0x20);
	CHIPONE_DSI(icn, 0x09, 0x10);
}

static int s070wv20_prepare(struct drm_panel *panel)
{
	struct s070wv20 *ctx = panel_to_s070wv20(panel);
	int ret;

	printk("%s\n", __func__);
	ret = regulator_enable(ctx->power);
	if (ret)
		dev_err(panel->dev, "failed to enable VDD1 regulator: %d\n", ret);

	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(50);

	gpiod_set_value(ctx->enable_gpio, 1);
	msleep(50);

	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(50);

	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(20);

	icn6211_bridge_init(ctx);

	printk("%s: done!\n", __func__);
	return 0;
}

static int s070wv20_enable(struct drm_panel *panel)
{
	struct s070wv20 *ctx = panel_to_s070wv20(panel);
	printk("%s\n", __func__);

	msleep(120);

	mipi_dsi_dcs_set_display_on(ctx->dsi);
	backlight_enable(ctx->backlight);

	printk("%s: done!\n", __func__);
	return 0;
}

static int s070wv20_disable(struct drm_panel *panel)
{
	struct s070wv20 *ctx = panel_to_s070wv20(panel);
	printk("%s\n", __func__);

	backlight_disable(ctx->backlight);
	return mipi_dsi_dcs_set_display_on(ctx->dsi);
}

static int s070wv20_unprepare(struct drm_panel *panel)
{
	struct s070wv20 *ctx = panel_to_s070wv20(panel);
	int ret;
	printk("%s\n", __func__);

	ret = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", ret);

	msleep(100);

	gpiod_set_value(ctx->reset_gpio, 0);

	gpiod_set_value(ctx->reset_gpio, 1);

	gpiod_set_value(ctx->enable_gpio, 0);

	gpiod_set_value(ctx->reset_gpio, 0);
	printk("%s: done!\n", __func__);

	return 0;
}

static const struct drm_display_mode s070wv20_default_mode = {
	.clock 		= 9000,

	.hdisplay 	= 320,
	.hsync_start	= 320 + 70,
	.hsync_end	= 320 + 70 + 20,
	.htotal		= 320 + 70 + 20 + 20,

	.vdisplay	= 240,
	.vsync_start	= 240 + 70,
	.vsync_end	= 240 + 70 + 20,
	.vtotal		= 240 + 70 + 20 + 20,
};

static int s070wv20_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	struct s070wv20 *ctx = panel_to_s070wv20(panel);
	struct drm_display_mode *mode;
	printk("%s\n", __func__);

	mode = drm_mode_duplicate(connector->dev, &s070wv20_default_mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%ux@%u\n",
			s070wv20_default_mode.hdisplay,
			s070wv20_default_mode.vdisplay,
			drm_mode_vrefresh(&s070wv20_default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	ctx->mode = mode;

	printk("%s: done!\n", __func__);
	return 1;
}

static const struct drm_panel_funcs s070wv20_funcs = {
	.disable = s070wv20_disable,
	.unprepare = s070wv20_unprepare,
	.prepare = s070wv20_prepare,
	.enable = s070wv20_enable,
	.get_modes = s070wv20_get_modes,
};

static int s070wv20_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct s070wv20 *ctx;
	int ret;

	printk("%s: In\n", __func__);
	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	drm_panel_init(&ctx->panel, &dsi->dev, &s070wv20_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->enable_gpio = devm_gpiod_get(&dsi->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable_gpio)) {
		dev_err(&dsi->dev, "Couldn't get our enable GPIO\n");
		return PTR_ERR(ctx->enable_gpio);
	}

	ctx->reset_gpio = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(&dsi->dev, "Couldn't get our reset GPIO\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->power = devm_regulator_get_optional(&dsi->dev, "power");
	if (IS_ERR(ctx->power)) {
		ret = PTR_ERR(ctx->power);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		ctx->power = NULL;
		dev_err(&dsi->dev, "failed to get power regulator: %d\n", ret);
	}

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		printk("%s failed! (ret=%d)\n", __func__, ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	};

	printk("%s done!\n", __func__);
	return 0;
}

static int s070wv20_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct s070wv20 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	if (ctx->backlight)
		put_device(&ctx->backlight->dev);

	return 0;
}

static const struct of_device_id s070wv20_of_match[] = {
	{ .compatible = "panel,icn6211", },
	{ }
};
MODULE_DEVICE_TABLE(of, s070wv20_of_match);

static struct mipi_dsi_driver s070wv20_driver = {
	.probe = s070wv20_dsi_probe,
	.remove = s070wv20_dsi_remove,
	.driver = {
		.name = "panel-icn6211",
		.of_match_table = s070wv20_of_match,
	},
};
module_mipi_dsi_driver(s070wv20_driver);

MODULE_AUTHOR("Jagan Teki <jagan@amarulasolutions.com>");
MODULE_DESCRIPTION("Panel ICN6211 MIPI-DSI to RGB");
MODULE_LICENSE("GPL v2");
