/* =========================================================================
 *  
 *  Copyright (c) 2019 Imagination Technologies Limited
 *  Copyright 2018-2021 NXP
 *
 *  SPDX-License-Identifier: GPL-2.0
 *
 * ========================================================================= */

#include "pfe_cfg.h"
#include "oal.h"
#include "hal.h"

#include "pfe_cbus.h"
#include "pfe_platform_cfg.h"
#include "pfe_bmu.h"

/* Configuration check */
#if ((PFE_CFG_BMU1_LMEM_BASEADDR + PFE_CFG_BMU1_LMEM_SIZE) > CBUS_LMEM_SIZE)
	#error BMU1 buffers exceed LMEM capacity
#endif

struct pfe_bmu_tag
{
	addr_t cbus_base_va;		/*	CBUS base virtual address */
	addr_t bmu_base_va;		/*	BMU base address (virtual) */
	addr_t pool_va_offset;	/*	Pre-calculated VA-PA conversion offset */
	addr_t pool_base_va;
	addr_t pool_base_pa;
	addr_t pool_size;
#ifdef PFE_CFG_PARANOID_IRQ
	oal_mutex_t lock;
#endif /* PFE_CFG_PARANOID_IRQ */
	addr_t bmu_base_offset;	/*	BMU base offset within CBUS space */
	uint32_t buf_size;
};

/**
 * @brief		BMU ISR
 * @param[in]	bmu The BMU instance
 * @return		EOK if interrupt has been handled
 */
__attribute__((cold)) errno_t pfe_bmu_isr(const pfe_bmu_t *bmu)
{
	errno_t ret = ENOENT;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == bmu))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_lock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex lock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */

	/*	Run the low-level ISR to identify and process the interrupt */
	ret = pfe_bmu_cfg_isr(bmu->bmu_base_va, bmu->cbus_base_va);

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_unlock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex unlock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */

	return ret;
}

/**
 * @brief		Mask BMU interrupts
 * @param[in]	bmu The BMU instance
 */
void pfe_bmu_irq_mask(const pfe_bmu_t *bmu)
{
#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_lock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex lock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */

	pfe_bmu_cfg_irq_mask(bmu->bmu_base_va);

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_unlock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex unlock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */
}

/**
 * @brief		Unmask BMU interrupts
 * @param[in]	hif The BMU instance
 */
void pfe_bmu_irq_unmask(const pfe_bmu_t *bmu)
{
#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_lock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex lock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */

	pfe_bmu_cfg_irq_unmask(bmu->bmu_base_va);

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_unlock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex unlock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */
}

/**
 * @brief		Create new BMU instance
 * @details		Creates and initializes BMU instance. New instance is disabled and needs
 * 				to be enabled by pfe_bmu_enable().
 * @param[in]	cbus_base_va CBUS base virtual address
 * @param[in]	bmu_base BMU base address offset within CBUS address space
 * @param[in]	cfg The BMU block configuration
 * @return		The BMU instance or NULL if failed
 */
__attribute__((cold)) pfe_bmu_t *pfe_bmu_create(addr_t cbus_base_va, addr_t bmu_base, const pfe_bmu_cfg_t *cfg)
{
	pfe_bmu_t *bmu;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == cfg) || (NULL_ADDR == cbus_base_va)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return NULL;
	}

	if (unlikely(NULL_ADDR == cfg->pool_pa))
	{
		NXP_LOG_ERROR("Buffer pool base is NULL\n");
		return NULL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	bmu = oal_mm_malloc(sizeof(pfe_bmu_t));

	if (NULL == bmu)
	{
		return NULL;
	}
	else
	{
		(void)memset(bmu, 0, sizeof(pfe_bmu_t));
		bmu->cbus_base_va = cbus_base_va;
		bmu->bmu_base_offset = bmu_base;
		bmu->bmu_base_va = (bmu->cbus_base_va + bmu->bmu_base_offset);
		bmu->pool_base_pa = cfg->pool_pa;
		bmu->pool_base_va = cfg->pool_va;
		bmu->pool_va_offset = bmu->pool_base_va - bmu->pool_base_pa;
		bmu->pool_size = ((uint32_t)1U << cfg->buf_size) * cfg->max_buf_cnt;
		bmu->buf_size = ((uint32_t)1U << cfg->buf_size);

#ifdef PFE_CFG_PARANOID_IRQ
		/*	Resource protection */
		if (EOK != oal_mutex_init(&bmu->lock))
		{
			NXP_LOG_DEBUG("Mutex initialization failed\n");
			oal_mm_free(bmu);
			return NULL;
		}
#endif /* PFE_CFG_PARANOID_IRQ */
	}

	pfe_bmu_reset(bmu);

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_lock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex lock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */

	pfe_bmu_cfg_disable(bmu->bmu_base_va);
	pfe_bmu_cfg_init(bmu->bmu_base_va, cfg);

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_unlock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex unlock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */

	return bmu;
}

/**
 * @brief		Reset the BMU block
 * @param[in]	bmu The BMU instance
 */
__attribute__((cold)) void pfe_bmu_reset(const pfe_bmu_t *bmu)
{
	errno_t ret;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == bmu))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_lock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex lock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */

	ret = pfe_bmu_cfg_reset(bmu->bmu_base_va);
	if (ETIMEDOUT == ret)
	{
		NXP_LOG_WARNING("BMU reset timed-out\n");
	}
	else if (EOK != ret)
	{
		NXP_LOG_WARNING("BMU reset failed: 0x%x\n", ret);
	}
	else
	{
		/*Do Nothing*/
		;
	}

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_unlock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex unlock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */
}

/**
 * @brief		Enable the BMU block
 * @param[in]	bmu The BMU instance
 */
__attribute__((cold)) void pfe_bmu_enable(const pfe_bmu_t *bmu)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == bmu))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_lock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex lock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */

	pfe_bmu_cfg_enable(bmu->bmu_base_va);

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_unlock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex unlock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */
}

/**
 * @brief		Disable the BMU block
 * @param[in]	bmu The BMU instance
 */
__attribute__((cold)) void pfe_bmu_disable(const pfe_bmu_t *bmu)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == bmu))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_lock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex lock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */

	pfe_bmu_cfg_disable(bmu->bmu_base_va);

#ifdef PFE_CFG_PARANOID_IRQ
	if (EOK != oal_mutex_unlock(&bmu->lock))
	{
		NXP_LOG_DEBUG("Mutex unlock failed\n");
	}
#endif /* PFE_CFG_PARANOID_IRQ */
}

/**
 * @brief		Allocate buffer via BMU
 * @param[in]	bmu The BMU instance
 * @return		Allocated buffer pointer (physical)
 * @note		Thread safe
 */
__attribute__((hot)) void *pfe_bmu_alloc_buf(const pfe_bmu_t *bmu)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == bmu))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return NULL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	No resource protection here since it is done by register read */
	return (void *)pfe_bmu_cfg_alloc_buf(bmu->bmu_base_va);
}

/**
 * @brief		Convert physical buffer address to virtual one
 * @param[in]	bmu The BMU instance
 * @param[in]	pa The address to be converted
 * @return		Associated virtual address or NULL if failed
 */
__attribute__((hot, pure)) void *pfe_bmu_get_va(const pfe_bmu_t *bmu, addr_t pa)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == bmu))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return NULL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if ((bmu->pool_base_pa + bmu->pool_size) < pa)
	{
		/*	TODO: The condition is not sufficient and need to consider buffer size... */
		NXP_LOG_DEBUG("PA out of range\n");
	}

	return (void *)(pa + bmu->pool_va_offset);
}

/**
 * @brief		Convert virtual buffer address to physical one
 * @param[in]	bmu The BMU instance
 * @param[in]	pa The address to be converted
 * @return		Associated virtual address or NULL if failed
 */
__attribute__((hot, pure)) void *pfe_bmu_get_pa(const pfe_bmu_t *bmu, addr_t va)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == bmu))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return NULL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if ((bmu->pool_base_va + bmu->pool_size) < va)
	{
		/*	TODO: The condition is not sufficient and need to consider buffer size... */
		NXP_LOG_DEBUG("VA out of range\n");
	}

	return (void *)(va - bmu->pool_va_offset);
}

/**
 * @brief		Get BMU buffer
 * @param[in]	bmu The BMU instance
 * @return		Buffer size in number of bytes
 */
__attribute__((cold, pure)) uint32_t pfe_bmu_get_buf_size(const pfe_bmu_t *bmu)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == bmu))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return 0U;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	return bmu->buf_size;
}

/**
 * @brief		Free buffer via BMU
 * @param[in]	bmu The BMU instance
 * @param[in]	buffer Pointer (physical) to the buffer to be freed.
 * @note		Thread safe
 */
__attribute__((hot)) void pfe_bmu_free_buf(const pfe_bmu_t *bmu, addr_t buffer)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == bmu) || (NULL == buffer)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	No resource protection here since it is done by register write */
	pfe_bmu_cfg_free_buf(bmu->bmu_base_va, PFE_CFG_MEMORY_PHYS_TO_PFE(buffer));
}

/**
 * @brief		Destroy BMU instance
 * @param[in]	bmu The BMU instance
 */
__attribute__((cold)) void pfe_bmu_destroy(const pfe_bmu_t *bmu)
{
	if (NULL != bmu)
	{
#ifdef PFE_CFG_PARANOID_IRQ
		if (EOK != oal_mutex_lock(&bmu->lock))
		{
			NXP_LOG_DEBUG("Mutex lock failed\n");
		}
#endif /* PFE_CFG_PARANOID_IRQ */

		pfe_bmu_cfg_disable(bmu->bmu_base_va);
		pfe_bmu_cfg_fini(bmu->bmu_base_va);

#ifdef PFE_CFG_PARANOID_IRQ
		if (EOK != oal_mutex_unlock(&bmu->lock))
		{
			NXP_LOG_DEBUG("Mutex unlock failed\n");
		}

		if (EOK != oal_mutex_destroy(&bmu->lock))
		{
			NXP_LOG_DEBUG("Mutex destroy failed\n");
		}
#endif /* PFE_CFG_PARANOID_IRQ */

		oal_mm_free(bmu);
	}
}

/**
 * @brief		Return BMU runtime statistics in text form
 * @details		Function writes formatted text into given buffer.
 * @param[in]	bmu 		The BMU instance
 * @param[in]	buf 		Pointer to the buffer to write to
 * @param[in]	buf_len 	Buffer length
 * @param[in]	verb_level 	Verbosity level
 * @return		Number of bytes written to the buffer
 */
__attribute__((cold)) uint32_t pfe_bmu_get_text_statistics(const pfe_bmu_t *bmu, char_t *buf, uint32_t buf_len, uint8_t verb_level)
{
	uint32_t len = 0U;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == bmu))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return 0U;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

		len += pfe_bmu_cfg_get_text_stat(bmu->bmu_base_va, buf, buf_len, verb_level);

	return len;
}
