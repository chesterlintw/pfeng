/*
 * Copyright 2020-2021 NXP
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <linux/prefetch.h>

#include "pfe_cfg.h"
#include "pfeng.h"

#define PFE_RXB_TRUESIZE	2048 /* PAGE_SIZE >> 1 */
#define PFE_RXB_PAD		NET_SKB_PAD /* add extra space if needed */
#define PFE_RXB_DMA_SIZE	(SKB_WITH_OVERHEAD(PFE_RXB_TRUESIZE) - PFE_RXB_PAD)

#define PFENG_BMAN_REFILL_THR	32

/* sanity check: we need RX buffering internal support disabled */
#if (TRUE == PFE_HIF_CHNL_CFG_RX_BUFFERS_ENABLED)
#error "Invalid PFE HIF channel mode"
#endif

struct pfeng_rx_map {
	dma_addr_t dma;
	struct page *page;
	u16 page_offset;
};

struct pfeng_rx_chnl_pool {
	pfe_hif_chnl_t	*chnl;
	struct device	*dev;
	u32				id;
	u32				depth;

	/* mappings of hif_drv rx ring */
	struct pfeng_rx_map 		*rx_tbl;
	u32				rd_idx;
	u32				wr_idx;
	u32				alloc_idx;
	u32				idx_mask;
};

struct pfeng_tx_map {

	void				*va_addr;
	addr_t				pa_addr;
	u32				size;
	bool				pages;
	struct sk_buff			*skb;
	u8				flags;
};

struct pfeng_tx_chnl_pool {
	u32				depth;

	/* mappings for hif_drv tx ring */
	struct pfeng_tx_map		*tx_tbl;
	u32				rd_idx;
	u32				wr_idx;
	u32				idx_mask;
};

int pfeng_bman_pool_create(struct pfeng_hif_chnl *chnl)
{
	struct pfeng_rx_chnl_pool *rx_pool;
	struct pfeng_tx_chnl_pool *tx_pool;

	/* RX pool */
	rx_pool = kzalloc(sizeof(*rx_pool), GFP_KERNEL);
	if (!rx_pool) {
		dev_err(chnl->dev, "chnl%d: No mem for bman rx_pool\n", pfe_hif_chnl_get_id(chnl->priv));
		return -ENOMEM;
	}

	rx_pool->id = pfe_hif_chnl_get_id(chnl->priv);
	rx_pool->depth = pfe_hif_chnl_get_rx_fifo_depth(chnl->priv);
	rx_pool->chnl = chnl->priv;
	rx_pool->dev = chnl->dev;

	rx_pool->rx_tbl = kzalloc(sizeof(struct pfeng_rx_map) * rx_pool->depth, GFP_KERNEL);
	if (!rx_pool->rx_tbl) {
		dev_err(chnl->dev, "chnl%d: failed. No mem\n", rx_pool->id);
		goto err;
	}
	rx_pool->rd_idx = 0;
	rx_pool->wr_idx = 0;
	rx_pool->alloc_idx = 0;
	rx_pool->idx_mask = pfe_hif_chnl_get_rx_fifo_depth(chnl->priv) - 1;

	chnl->bman.rx_pool = rx_pool;

	/* TX pool */
	tx_pool = kzalloc(sizeof(*tx_pool), GFP_KERNEL);
	if (!rx_pool) {
		dev_err(chnl->dev, "chnl%d: No mem for bman tx_pool\n", pfe_hif_chnl_get_id(chnl->priv));
		goto err;
	}

	tx_pool->depth = pfe_hif_chnl_get_tx_fifo_depth(chnl->priv);
	tx_pool->tx_tbl = kzalloc(sizeof(struct pfeng_tx_map) * tx_pool->depth, GFP_KERNEL);
	if (!tx_pool->tx_tbl) {
		dev_err(chnl->dev, "chnl%d: failed. No mem\n", rx_pool->id);
		goto err;
	}
	tx_pool->rd_idx = 0;
	tx_pool->wr_idx = 0;
	tx_pool->idx_mask = pfe_hif_chnl_get_tx_fifo_depth(chnl->priv) - 1;

	chnl->bman.tx_pool = tx_pool;

	return 0;

err:
	pfeng_bman_pool_destroy(chnl);
	return -ENOMEM;
}

void pfeng_bman_pool_destroy(struct pfeng_hif_chnl *chnl)
{
	struct pfeng_rx_chnl_pool *rx_pool = (struct pfeng_rx_chnl_pool *)chnl->bman.rx_pool;
	struct pfeng_tx_chnl_pool *tx_pool = (struct pfeng_tx_chnl_pool *)chnl->bman.tx_pool;

	if (rx_pool) {
		if(rx_pool->rx_tbl) {
			kfree(rx_pool->rx_tbl);
			rx_pool->rx_tbl = NULL;
		}

		kfree(rx_pool);
		chnl->bman.rx_pool = NULL;
	}

	if (tx_pool) {
		if(tx_pool->tx_tbl) {
			kfree(tx_pool->tx_tbl);
			tx_pool->tx_tbl = NULL;
		}

		kfree(tx_pool);
		chnl->bman.tx_pool = NULL;
	}

	return;
}

bool pfeng_hif_chnl_txconf_check(struct pfeng_hif_chnl *chnl, u32 elems)
{
	struct pfeng_tx_chnl_pool *pool = chnl->bman.tx_pool;
	u32 idx = pool->wr_idx;

	if(unlikely(elems >= pool->depth))
		return false;

	/* Check if last element is free */
	idx = (pool->wr_idx + elems) & pool->idx_mask;
	return !pool->tx_tbl[idx].size;
}

int pfeng_hif_chnl_txconf_put_map_frag(struct pfeng_hif_chnl *chnl, void *va_addr, addr_t pa_addr, u32 size, struct sk_buff *skb, u8 flags)
{
	struct pfeng_tx_chnl_pool *pool = chnl->bman.tx_pool;
	u32 idx = pool->wr_idx;

	pool->tx_tbl[idx].va_addr = va_addr;
	pool->tx_tbl[idx].pa_addr = pa_addr;
	pool->tx_tbl[idx].size = size;
	pool->tx_tbl[idx].skb = skb;
#ifdef PFE_CFG_MULTI_INSTANCE_SUPPORT
	pool->tx_tbl[idx].flags = flags;
#endif

	pool->wr_idx = (pool->wr_idx + 1) & pool->idx_mask;

	return idx;
}

u8 pfeng_hif_chnl_txconf_get_flag(struct pfeng_hif_chnl *chnl)
{
	struct pfeng_tx_chnl_pool *pool = chnl->bman.tx_pool;
	u32 idx = pool->rd_idx;

	return pool->tx_tbl[idx].flags;
}

struct sk_buff *pfeng_hif_chnl_txconf_get_skbuf(struct pfeng_hif_chnl *chnl)
{
	struct pfeng_tx_chnl_pool *pool = chnl->bman.tx_pool;
	u32 idx = pool->rd_idx;

	return pool->tx_tbl[idx].skb;
}

int pfeng_hif_chnl_txconf_free_map_full(struct pfeng_hif_chnl *chnl)
{
	struct pfeng_tx_chnl_pool *pool = chnl->bman.tx_pool;
	u32 idx = pool->rd_idx;
	struct sk_buff *skb = pool->tx_tbl[idx].skb;
	u32 nfrags;

	BUG_ON(!skb);

	nfrags = skb_shinfo(skb)->nr_frags;

	/* Unmap linear part */
	dma_unmap_single_attrs(chnl->dev, pool->tx_tbl[idx].pa_addr, pool->tx_tbl[idx].size, DMA_TO_DEVICE, 0);
	pool->tx_tbl[idx].size = 0;

	/* Unmap frags */
	idx = (idx + 1) & pool->idx_mask;
	while (nfrags--) {
		dma_unmap_page(chnl->dev, pool->tx_tbl[idx].pa_addr, pool->tx_tbl[idx].size, DMA_TO_DEVICE);
		pool->tx_tbl[idx].size = 0;

		idx = (idx + 1) & pool->idx_mask;
	}
	pool->rd_idx = idx;

	dev_consume_skb_any(skb);

	return 0;
}

int pfeng_hif_chnl_txconf_unroll_map_full(struct pfeng_hif_chnl *chnl, u32 idx, u32 nfrags)
{
	struct pfeng_tx_chnl_pool *pool = chnl->bman.tx_pool;
	struct sk_buff *skb = pool->tx_tbl[idx].skb;

	BUG_ON(!skb);

	/* Unmap frags. Unrolling from last to first */
	idx = (pool->wr_idx - 1) & pool->idx_mask;
	while (nfrags--) {
		dma_unmap_page(chnl->dev, pool->tx_tbl[idx].pa_addr, pool->tx_tbl[idx].size, DMA_TO_DEVICE);
		pool->tx_tbl[idx].size = 0;

		idx = (idx - 1) & pool->idx_mask;
	}

	/* Unmap linear part */
	dma_unmap_single_attrs(chnl->dev, pool->tx_tbl[idx].pa_addr, pool->tx_tbl[idx].size, DMA_TO_DEVICE, 0);
	pool->tx_tbl[idx].size = 0;

	pool->wr_idx = idx;

	return 0;
}

static inline int pfeng_bman_rx_chnl_pool_unused(struct pfeng_rx_chnl_pool *pool)
{
	return pool->depth - pool->wr_idx + pool->rd_idx - 1;
}

static inline struct pfeng_rx_map *pfeng_bman_get_rx_map(struct pfeng_rx_chnl_pool *pool, u32 idx)
{
	return &pool->rx_tbl[idx & pool->idx_mask];
}

static bool pfeng_bman_buf_alloc_and_map(struct pfeng_rx_chnl_pool *pool, struct pfeng_rx_map *rx_map)
{
	struct page *page;
	dma_addr_t dma;

	/* Request page from DMA safe region */
	page = __dev_alloc_page(GFP_DMA32 | GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!page))
		return false;

	/* do dma map */
	dma = dma_map_page(pool->dev, page, 0, PAGE_SIZE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(pool->dev, dma))) {
		__free_page(page);
		return false;
	}

	rx_map->dma = dma;
	rx_map->page = page;
	rx_map->page_offset = PFE_RXB_PAD;

	return true;
}

static int pfeng_hif_chnl_refill_rx_buffer(struct pfeng_hif_chnl *chnl, struct pfeng_rx_map *rx_map)
{
	struct pfeng_rx_chnl_pool *pool = chnl->bman.rx_pool;
	int err;

	/*	Ask for new buffer */
	if (unlikely(!rx_map->page))
		if (unlikely(!pfeng_bman_buf_alloc_and_map(pool, rx_map))) {
			dev_err(chnl->dev, "buffer allocation error\n");
			return -ENOMEM;
		}

	/* Add new buffer to ring */
	err = pfe_hif_chnl_supply_rx_buf(chnl->priv, (void *)(rx_map->dma + rx_map->page_offset), PFE_RXB_DMA_SIZE);
	if (unlikely(err))
		return err;

	return 0;
}

static int pfeng_hif_chnl_refill_rx_pool(struct pfeng_hif_chnl *chnl, int count)
{
	struct pfeng_rx_chnl_pool *pool = chnl->bman.rx_pool;
	struct pfeng_rx_map *rx_map;
	int i, ret = 0;

	for (i = 0; i < count; i++) {
		rx_map = pfeng_bman_get_rx_map(pool, pool->wr_idx);
		ret = pfeng_hif_chnl_refill_rx_buffer(chnl, rx_map);
		if (unlikely(ret))
			break;
		/* push rx map */
		pool->wr_idx++;
	}

	pool->alloc_idx = pool->wr_idx;

	return ret;
}

static bool pfeng_page_reusable(struct page *page)
{
	return (!page_is_pfmemalloc(page) && page_ref_count(page) == 1);
}

static void pfeng_reuse_page(struct pfeng_rx_chnl_pool *pool,
			     struct pfeng_rx_map *old)
{
	struct pfeng_rx_map *new;

	/* next buf mapping that may reuse a page */
	new = pfeng_bman_get_rx_map(pool, pool->alloc_idx);

	/* copy page reference */
	*new = *old;

	/* advance page allocation idx */
	pool->alloc_idx++;
}

static struct sk_buff *pfeng_rx_map_buff_to_skb(struct pfeng_rx_chnl_pool *pool, u32 rx_len)
{
	struct pfeng_rx_map *rx_map;
	struct sk_buff *skb;
	void *va;

	rx_map = pfeng_bman_get_rx_map(pool, pool->rd_idx);

	/* get rx buffer */
	dma_sync_single_range_for_cpu(pool->dev, rx_map->dma,
				      rx_map->page_offset,
				      rx_len, DMA_FROM_DEVICE);

	va = page_address(rx_map->page) + rx_map->page_offset;
	skb = build_skb(va - PFE_RXB_PAD, PFE_RXB_TRUESIZE);
	if (unlikely(!skb))
		return NULL;

	skb_reserve(skb, PFE_RXB_PAD);
	__skb_put(skb, rx_len);

	/* put rx buffer */
	if (likely(pfeng_page_reusable(rx_map->page))) {
		rx_map->page_offset ^= PFE_RXB_TRUESIZE;
		page_ref_inc(rx_map->page);

		pfeng_reuse_page(pool, rx_map);

		/* dma sync for use by device */
		dma_sync_single_range_for_device(pool->dev, rx_map->dma,
						 rx_map->page_offset,
						 PFE_RXB_DMA_SIZE,
						 DMA_FROM_DEVICE);
	} else {
		dma_unmap_page(pool->dev, rx_map->dma, PAGE_SIZE, DMA_FROM_DEVICE);
	}

	/* drop reference as page was reused at alloc_idx, or was unmaped */
	rx_map->page = NULL;
	/* pull rx map */
	pool->rd_idx++;

	return skb;
}

struct sk_buff *pfeng_hif_chnl_receive_pkt(struct pfeng_hif_chnl *chnl, u32 queue)
{
	struct sk_buff *skb;
	void *buf_pa;
	u32 rx_len;
	bool_t lifm;

	if (unlikely(pfeng_bman_rx_chnl_pool_unused(chnl->bman.rx_pool) >= PFENG_BMAN_REFILL_THR))
		pfeng_hif_chnl_refill_rx_pool(chnl, PFENG_BMAN_REFILL_THR);

	/*	Get RX buffer */
	if (EOK != pfe_hif_chnl_rx(chnl->priv, &buf_pa, &rx_len, &lifm))
	{
		return NULL;
	}

	/*  Get buffer VA */
	skb = pfeng_rx_map_buff_to_skb(chnl->bman.rx_pool, rx_len);
	if (unlikely(!skb)) {
		dev_err(chnl->dev, "chnl%d: pull VA failed\n", chnl->idx);
		return NULL;
	}
	prefetch(skb->data);

	return skb;
}

int pfeng_hif_chnl_fill_rx_buffers(struct pfeng_hif_chnl *chnl)
{
	int cnt = 0;
	int ret;

	while (pfe_hif_chnl_can_accept_rx_buf(chnl->priv)) {

		preempt_disable();
		ret = pfeng_hif_chnl_refill_rx_pool(chnl, 1);
		preempt_enable();
		if (ret)
			break;
		cnt++;
	}

	return cnt;
}
