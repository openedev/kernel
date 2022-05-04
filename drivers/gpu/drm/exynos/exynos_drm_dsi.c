// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung MIPI DSIM glue for Exynos SoCs.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd
 *
 * Contacts: Tomasz Figa <t.figa@samsung.com>
 */

#include <linux/component.h>
#include <linux/gpio/consumer.h>

#include <drm/bridge/samsung-dsim.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "exynos_drm_crtc.h"
#include "exynos_drm_drv.h"

struct exynos_dsi {
	struct drm_encoder encoder;
	struct samsung_dsim *priv;

	struct gpio_desc *te_gpio;

	struct samsung_dsim_plat_data pdata;
};

static inline struct  exynos_dsi *pdata_to_dsi(const struct samsung_dsim_plat_data *p)
{
	return container_of(p, struct  exynos_dsi, pdata);
}

static void exynos_dsi_enable_irq(struct samsung_dsim *priv)
{
	const struct samsung_dsim_plat_data *pdata = priv->plat_data;
	struct exynos_dsi *dsi = pdata_to_dsi(pdata);

	if (dsi->te_gpio)
		enable_irq(gpiod_to_irq(dsi->te_gpio));
}

static void exynos_dsi_disable_irq(struct samsung_dsim *priv)
{
	const struct samsung_dsim_plat_data *pdata = priv->plat_data;
	struct exynos_dsi *dsi = pdata_to_dsi(pdata);

	if (dsi->te_gpio)
		disable_irq(gpiod_to_irq(dsi->te_gpio));
}

static const struct samsung_dsim_irq_ops samsung_dsim_exynos_host_irq = {
	.enable = exynos_dsi_enable_irq,
	.disable = exynos_dsi_disable_irq,
};

static irqreturn_t exynos_dsi_te_irq_handler(int irq, void *dev_id)
{
	struct exynos_dsi *dsi = (struct exynos_dsi *)dev_id;
	struct samsung_dsim *priv = dsi->priv;
	struct drm_encoder *encoder = &dsi->encoder;

	if (priv->state & DSIM_STATE_VIDOUT_AVAILABLE)
		exynos_drm_crtc_te_handler(encoder->crtc);

	return IRQ_HANDLED;
}

static int exynos_dsi_register_te_irq(struct exynos_dsi *dsi, struct device *panel)
{
	struct samsung_dsim *priv = dsi->priv;
	int te_gpio_irq;
	int ret;

	dsi->te_gpio = devm_gpiod_get_optional(priv->dev, "te", GPIOD_IN);
	if (IS_ERR(dsi->te_gpio)) {
		dev_err(priv->dev, "gpio request failed with %ld\n",
				PTR_ERR(dsi->te_gpio));
		return PTR_ERR(dsi->te_gpio);
	}

	te_gpio_irq = gpiod_to_irq(dsi->te_gpio);

	ret = request_threaded_irq(te_gpio_irq, exynos_dsi_te_irq_handler, NULL,
				   IRQF_TRIGGER_RISING | IRQF_NO_AUTOEN, "TE", dsi);
	if (ret) {
		dev_err(priv->dev, "request interrupt failed with %d\n", ret);
		gpiod_put(dsi->te_gpio);
		return ret;
	}

	return 0;
}

static void exynos_dsi_unregister_te_irq(struct exynos_dsi *dsi)
{
	if (dsi->te_gpio) {
		free_irq(gpiod_to_irq(dsi->te_gpio), dsi);
		gpiod_put(dsi->te_gpio);
	}
}

static int exynos_dsi_host_attach(struct samsung_dsim *priv, struct mipi_dsi_device *device)
{
	const struct samsung_dsim_plat_data *pdata = priv->plat_data;
	struct exynos_dsi *dsi = pdata_to_dsi(pdata);
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_device *drm = encoder->dev;
	int ret;

	drm_bridge_attach(encoder, &priv->bridge, NULL, 0);

	/*
	 * This is a temporary solution and should be made by more generic way.
	 *
	 * If attached panel device is for command mode one, dsi should register
	 * TE interrupt handler.
	 */
	if (!(device->mode_flags & MIPI_DSI_MODE_VIDEO)) {
		ret = exynos_dsi_register_te_irq(dsi, &device->dev);
		if (ret)
			return ret;
	}

	mutex_lock(&drm->mode_config.mutex);

	priv->lanes = device->lanes;
	priv->format = device->format;
	priv->mode_flags = device->mode_flags;
	exynos_drm_crtc_get_by_type(drm, EXYNOS_DISPLAY_TYPE_LCD)->i80_mode =
			!(priv->mode_flags & MIPI_DSI_MODE_VIDEO);

	mutex_unlock(&drm->mode_config.mutex);

	if (drm->mode_config.poll_enabled)
		drm_kms_helper_hotplug_event(drm);

	return 0;
}

static int exynos_dsi_host_detach(struct samsung_dsim *priv, struct mipi_dsi_device *device)
{
	const struct samsung_dsim_plat_data *pdata = priv->plat_data;
	struct exynos_dsi *dsi = pdata_to_dsi(pdata);
	struct drm_device *drm = dsi->encoder.dev;

	if (drm->mode_config.poll_enabled)
		drm_kms_helper_hotplug_event(drm);

	exynos_dsi_unregister_te_irq(dsi);

	return 0;
}

static const struct samsung_dsim_host_ops samsung_dsim_exynos_host_ops = {
	.attach = exynos_dsi_host_attach,
	.detach = exynos_dsi_host_detach,
};

static int exynos_dsi_bind(struct device *dev, struct device *master, void *data)
{
	struct samsung_dsim *priv = dev_get_drvdata(dev);
	const struct samsung_dsim_plat_data *pdata = priv->plat_data;
	struct exynos_dsi *dsi = pdata_to_dsi(pdata);
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_device *drm_dev = data;
	int ret;

	drm_simple_encoder_init(drm_dev, encoder, DRM_MODE_ENCODER_TMDS);

	ret = exynos_drm_set_possible_crtcs(encoder, EXYNOS_DISPLAY_TYPE_LCD);
	if (ret < 0)
		return ret;

	return mipi_dsi_host_register(&priv->dsi_host);
}

static void exynos_dsi_unbind(struct device *dev, struct device *master, void *data)
{
	struct samsung_dsim *priv = dev_get_drvdata(dev);

	priv->bridge.funcs->atomic_disable(&priv->bridge, NULL);

	mipi_dsi_host_unregister(&priv->dsi_host);
}

static const struct component_ops exynos_dsi_component_ops = {
	.bind	= exynos_dsi_bind,
	.unbind	= exynos_dsi_unbind,
};

const struct samsung_dsim_plat_data *samsung_dsim_plat_probe(struct samsung_dsim *priv)
{
	struct exynos_dsi *dsi;
	int ret;

	dsi = devm_kzalloc(priv->dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return ERR_PTR(-ENOMEM);

	dsi->pdata.host_ops = &samsung_dsim_exynos_host_ops;
	dsi->pdata.irq_ops = &samsung_dsim_exynos_host_irq;
	dsi->priv = priv;

	ret = component_add(priv->dev, &exynos_dsi_component_ops);
	if (ret)
		return ERR_PTR(ret);

	return &dsi->pdata;
}

void samsung_dsim_plat_remove(struct samsung_dsim *priv)
{
	component_del(priv->dev, &exynos_dsi_component_ops);
}

MODULE_AUTHOR("Tomasz Figa <t.figa@samsung.com>");
MODULE_AUTHOR("Andrzej Hajda <a.hajda@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC MIPI DSI Master");
MODULE_LICENSE("GPL v2");
