/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2022 Amarula Solutions(India)
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#ifndef __SAMSUNG_DSIM__
#define __SAMSUNG_DSIM__

#include <drm/drm_atomic_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_mipi_dsi.h>

struct samsung_dsim;

#define DSIM_STATE_ENABLED		BIT(0)
#define DSIM_STATE_INITIALIZED		BIT(1)
#define DSIM_STATE_CMD_LPM		BIT(2)
#define DSIM_STATE_VIDOUT_AVAILABLE	BIT(3)

struct samsung_dsim_transfer {
	struct list_head list;
	struct completion completed;
	int result;
	struct mipi_dsi_packet packet;
	u16 flags;
	u16 tx_done;

	u8 *rx_payload;
	u16 rx_len;
	u16 rx_done;
};

struct samsung_dsim_driver_data {
	const unsigned int *reg_ofs;
	unsigned int plltmr_reg;
	unsigned int has_freqband:1;
	unsigned int has_clklane_stop:1;
	unsigned int num_clks;
	unsigned int max_freq;
	unsigned int wait_for_reset;
	unsigned int num_bits_resol;
	const unsigned int *reg_values;
};

struct samsung_dsim_host_ops {
	int (*attach)(struct samsung_dsim *priv, struct mipi_dsi_device *device);
	int (*detach)(struct samsung_dsim *priv, struct mipi_dsi_device *device);
};

struct samsung_dsim_irq_ops {
	void (*enable)(struct samsung_dsim *priv);
	void (*disable)(struct samsung_dsim *priv);
};

struct samsung_dsim_plat_data {
	const struct samsung_dsim_host_ops *host_ops;
	const struct samsung_dsim_irq_ops *irq_ops;

	void *priv;
};

struct samsung_dsim {
	struct mipi_dsi_host dsi_host;
	struct drm_bridge bridge;
	struct drm_bridge *out_bridge;
	struct device *dev;
	struct drm_display_mode mode;

	void __iomem *reg_base;
	struct phy *phy;
	struct clk **clks;
	struct regulator_bulk_data supplies[2];
	int irq;

	u32 pll_clk_rate;
	u32 burst_clk_rate;
	u32 esc_clk_rate;
	u32 lanes;
	u32 mode_flags;
	u32 format;

	int state;
	struct drm_property *brightness;
	struct completion completed;

	spinlock_t transfer_lock; /* protects transfer_list */
	struct list_head transfer_list;

	const struct samsung_dsim_driver_data *driver_data;
	const struct samsung_dsim_plat_data *plat_data;
};

const struct samsung_dsim_plat_data *samsung_dsim_plat_probe(struct samsung_dsim *priv);
void samsung_dsim_plat_remove(struct samsung_dsim *priv);

#endif /* __SAMSUNG_DSIM__ */
