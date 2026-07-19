/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * Minimal DHCPv4 server for MediaTek bootmenu and web failsafe.
 */

#ifndef __NET_MTK_DHCPD_H__
#define __NET_MTK_DHCPD_H__

#if CONFIG_IS_ENABLED(MTK_DHCPD)
int mtk_dhcpd_start(void);
void mtk_dhcpd_poll(void);
void mtk_dhcpd_stop(void);
bool mtk_dhcpd_is_running(void);
#else
static inline int mtk_dhcpd_start(void)
{
	return 0;
}

static inline void mtk_dhcpd_poll(void)
{
}

static inline void mtk_dhcpd_stop(void)
{
}

static inline bool mtk_dhcpd_is_running(void)
{
	return false;
}
#endif

#endif /* __NET_MTK_DHCPD_H__ */
