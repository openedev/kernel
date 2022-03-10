// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2016 Allwinnertech Co., Ltd.
 * Copyright (C) 2017-2018 Bootlin
 *
 * Maxime Ripard <maxime.ripard@bootlin.com>
 */

#ifndef _SUN6I_MIPI_DSI_H_
#define _SUN6I_MIPI_DSI_H_

#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mipi_dsi.h>
#include <linux/gpio/consumer.h>

#define SUN6I_DSI_TCON_DIV	4

struct sun6i_dsi {
	struct drm_bridge	bridge;
	struct drm_encoder	encoder;
	struct mipi_dsi_host	host;

	struct clk		*bus_clk;
	struct clk		*mod_clk;
	struct regmap		*regs;
	struct regulator	*regulator;
	struct reset_control	*reset;
	struct gpio_desc	*switch_gpio;
	struct gpio_desc	*enable_gpio;
	struct phy		*dphy;

	struct device		*dev;
	struct mipi_dsi_device	*device;
	struct drm_bridge	*next_bridge;
};

static inline struct sun6i_dsi *bridge_to_sun6i_dsi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct sun6i_dsi, bridge);
}

static inline struct sun6i_dsi *host_to_sun6i_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct sun6i_dsi, host);
};

static inline struct sun6i_dsi *encoder_to_sun6i_dsi(const struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun6i_dsi, encoder);
};

#endif /* _SUN6I_MIPI_DSI_H_ */
