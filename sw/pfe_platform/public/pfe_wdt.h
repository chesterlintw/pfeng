/* =========================================================================
 *  
 *  Copyright (c) 2019 Imagination Technologies Limited
 *  Copyright 2020-2021 NXP
 *
 *  SPDX-License-Identifier: GPL-2.0
 *
 * ========================================================================= */

#ifndef PUBLIC_PFE_WDT_H_
#define PUBLIC_PFE_WDT_H_

typedef struct pfe_wdt_tag pfe_wdt_t;

typedef struct
{
	void *pool_pa;
	void *pool_va;
} pfe_wdt_cfg_t;


pfe_wdt_t *pfe_wdt_create(addr_t cbus_base_va, addr_t wdt_base);
void pfe_wdt_destroy(pfe_wdt_t *wdt);
errno_t pfe_wdt_isr(pfe_wdt_t *wdt);
void pfe_wdt_irq_mask(pfe_wdt_t *wdt);
void pfe_wdt_irq_unmask(pfe_wdt_t *wdt);
uint32_t pfe_wdt_get_text_statistics(const pfe_wdt_t *wdt, char_t *buf, uint32_t buf_len, uint8_t verb_level);

#endif /* PUBLIC_PFE_WDT_H_ */
