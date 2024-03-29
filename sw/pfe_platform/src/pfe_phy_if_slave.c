/* =========================================================================
 *  
 *  Copyright (c) 2019 Imagination Technologies Limited
 *  Copyright 2018-2021 NXP
 *
 *  SPDX-License-Identifier: GPL-2.0
 *
 * ========================================================================= */

/**
 * @file		pfe_phy_if_slave.c
 * @brief		The PFE physical interface module source file (slave).
 * @details		This file contains physical interface-related functionality for
 *				the slave driver variant. All physical interface instance
 *				manipulation is done via RPC in way that local driver only
 *				sends requests to master driver which performs the actual
 *				requested operations.
 */

#include "pfe_cfg.h"
#include "oal.h"

#ifdef PFE_CFG_PFE_SLAVE
#include "hal.h"
#include "pfe_platform_cfg.h"
#include "pfe_ct.h"
#include "linked_list.h"
#include "pfe_phy_if.h"
#include "pfe_idex.h" /* The RPC provider */
#include "pfe_platform_rpc.h" /* The RPC codes and data structures */

struct pfe_phy_if_tag
{
	pfe_ct_phy_if_id_t id;
	char_t *name;
	pfe_mac_db_t *mac_db; /* MAC database */
	oal_mutex_t lock;
	bool_t is_enabled;
};

static bool_t pfe_phy_if_has_log_if_nolock(const pfe_phy_if_t *iface, const pfe_log_if_t *log_if);

static errno_t pfe_phy_if_db_lock(void)
{
	errno_t ret;

	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_IF_LOCK, NULL, 0, NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("Unable to lock interface DB: %d\n", ret);
	}

	return ret;
}

static errno_t pfe_phy_if_db_unlock(void)
{
	errno_t ret;

	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_IF_UNLOCK, NULL, 0, NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("Unable to lock interface DB: %d\n", ret);
	}

	return ret;
}

/**
 * @brief		Create new physical interface instance
 * @param[in]	class The classifier instance
 * @param[in]	id The PFE firmware is using HW interface identifiers to distinguish
 * 				between particular interfaces. The set of available IDs (the
 * 				pfe_ct_phy_if_id_t) shall remain compatible with the firmware.
 * @param[in]	name Name of the interface
 * @return		The interface instance or NULL if failed
 */
pfe_phy_if_t *pfe_phy_if_create(pfe_class_t *class, pfe_ct_phy_if_id_t id, const char_t *name)
{
	pfe_platform_rpc_pfe_phy_if_create_arg_t req = {0U};
	pfe_phy_if_t *iface;
	errno_t ret;

	/*	Get remote phy_if instance */
	req.phy_if_id = id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_CREATE, &req, sizeof(req), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("Can't get remote instance: %d\n", ret);
		return NULL;
	}

	iface = oal_mm_malloc(sizeof(pfe_phy_if_t));
	if (NULL == iface)
	{
		return NULL;
	}
	else
	{
		memset(iface, 0, sizeof(pfe_phy_if_t));
		iface->id = id;
	}

	iface->mac_db = pfe_mac_db_create();
	if (NULL == iface->mac_db)
	{
		NXP_LOG_ERROR("Could not create MAC database\n");
		oal_mm_free(iface);
		return NULL;
	}

	if (NULL == name)
	{
		iface->name = NULL;
	}
	else
	{
		iface->name = oal_mm_malloc(strlen(name) + 1U);
		if (NULL == iface->name)
		{
			NXP_LOG_ERROR("Memory allocation failed\n");
			oal_mm_free(iface);
			return NULL;
		}

		strcpy(iface->name, name);
	}
	
	if (EOK != oal_mutex_init(&iface->lock))
	{
		NXP_LOG_ERROR("Could not initialize mutex\n");
		oal_mm_free(iface);
		iface = NULL;
		return NULL;
	}

	return iface;
}

/**
 * @brief		Destroy interface instance
 * @param[in]	iface The interface instance
 */
void pfe_phy_if_destroy(pfe_phy_if_t *iface)
{
	pfe_platform_rpc_pfe_phy_if_flush_mac_addrs_arg_t arg = {0};
	errno_t ret = EOK;

	if (NULL != iface)
	{
		if (EOK != oal_mutex_lock(&iface->lock))
		{
			NXP_LOG_DEBUG("mutex lock failed\n");
		}

		/*	Ask the master driver to remove all associated MAC addresses */
		arg.phy_if_id = iface->id;
		arg.crit = MAC_DB_CRIT_ALL;
		arg.type = PFE_TYPE_ANY;
		ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_FLUSH_MAC_ADDRS, &arg, sizeof(arg), NULL, 0U);
		if (EOK != ret)
		{
			NXP_LOG_ERROR("PFE_PLATFORM_RPC_PFE_PHY_IF_FLUSH_MAC_ADDRS failed: %d\n", ret);
		}

		/* Destroy local MAC database */
		ret = pfe_mac_db_destroy(iface->mac_db);
		if (EOK != ret)
		{
			NXP_LOG_WARNING("Unable to destroy MAC database: %d\n", ret);
		}

		if (EOK != oal_mutex_unlock(&iface->lock))
		{
			NXP_LOG_DEBUG("mutex unlock failed\n");
		}

		if (NULL != iface->name)
		{
			oal_mm_free(iface->name);
			iface->name = NULL;
		}

		if (EOK != oal_mutex_destroy(&iface->lock))
		{
			NXP_LOG_DEBUG("Could not destroy mutex\n");
		}

		oal_mm_free(iface);
	}
	
	return;
}

/**
 * @brief		Return classifier instance associated with interface
 * @param[in]	iface The interface instance
 * @return		The classifier instance
 */
__attribute__((pure)) pfe_class_t *pfe_phy_if_get_class(const pfe_phy_if_t *iface)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return NULL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	return NULL;
}

/**
 * @brief		Add logical interface
 * @details		First added logical interface will become the default one. Default is used
 * 				when packet is not matching any other logical interface within the physical one.
 * @param[in]	iface The physical interface instance
 * @param[in]	log_if The logical interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 * @retval		ENOEXEC Command failed
 * @retval		EEXIST Entry exists
 */
errno_t pfe_phy_if_add_log_if(pfe_phy_if_t *iface, pfe_log_if_t *log_if)
{
	errno_t ret = EOK;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == iface) || (NULL == log_if)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	NXP_LOG_DEBUG("%s: Not supported in slave variant\n", __func__);
	ret = ENOTSUP;

	return ret;
}

static bool_t pfe_phy_if_has_log_if_nolock(const pfe_phy_if_t *iface, const pfe_log_if_t *log_if)
{
	pfe_platform_rpc_pfe_phy_if_has_log_if_arg_t arg = {0};
	errno_t ret;
	bool_t val = TRUE;

	ct_assert(sizeof(arg.log_if_id) == sizeof(uint8_t));

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == iface) || (NULL == log_if)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return FALSE;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	Ask master driver if such logical interface is associated with the physical one */
	arg.phy_if_id = iface->id;

	arg.log_if_id = pfe_log_if_get_id(log_if);

	(void)pfe_phy_if_db_lock();

	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_HAS_LOG_IF, &arg, sizeof(arg), NULL, 0U);
	if (EOK == ret)
	{
		val = TRUE;
	}
	else if (ENOENT == ret)
	{
		val = FALSE;
	}
	else
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_HAS_LOG_IF failed: %d\n", ret);
		val = FALSE;
	}

	(void)pfe_phy_if_db_unlock();

	return val;
}

/**
 * @brief		Check if physical interface contains given logical interface
 * @param[in]	iface The physical interface instance
 * @param[in]	log_if The logical interface instance
 * @return		TRUE if logical interface is bound to the physical one. False
 * 				otherwise.
 */
bool_t pfe_phy_if_has_log_if(pfe_phy_if_t *iface, const pfe_log_if_t *log_if)
{
	bool_t match = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == iface) || (NULL == log_if)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return FALSE;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	match = pfe_phy_if_has_log_if_nolock(iface, log_if);

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return match;
}

/**
 * @brief		Delete associated logical interface
 * @param[in]	iface The physical interface instance
 * @param[in]	log_if The logical interface instance to be deleted
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 * @retval		ENOEXEC Command failed
 * @retval		ENOENT Entry not found
 */
errno_t pfe_phy_if_del_log_if(pfe_phy_if_t *iface, const pfe_log_if_t *log_if)
{
	errno_t ret = EOK;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == iface) || (NULL == log_if)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	NXP_LOG_DEBUG("%s: Not supported in slave variant\n", __func__);
	ret = ENOTSUP;

	return ret;
}

/**
 * @brief		Get operational mode
 * @param[in]	iface The interface instance
 * @retval		Current phy_if mode. See pfe_ct_if_op_mode_t.
 */
pfe_ct_if_op_mode_t pfe_phy_if_get_op_mode(pfe_phy_if_t *iface)
{
	errno_t ret;
	pfe_ct_if_op_mode_t mode = IF_OP_DISABLED;
	pfe_platform_rpc_pfe_phy_if_get_op_mode_arg_t arg = {0};
	pfe_platform_rpc_pfe_phy_if_get_op_mode_ret_t rpc_ret = {0};

#if defined(GLOBAL_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return IF_OP_DISABLED;
	}
#endif /* GLOBAL_CFG_NULL_ARG_CHECK */

	/*	Update the interface structure */
	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to change the operation mode */
	arg.phy_if_id = iface->id;

	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_GET_OP_MODE, &arg, sizeof(arg), &rpc_ret, sizeof(rpc_ret));
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_GET_OP_MODE failed: %d\n", ret);
	}
	else
	{
		mode = rpc_ret.mode;
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return mode;
}
/**
 * @brief		Set operational mode
 * @param[in]	iface The interface instance
 * @param[in]	mode Mode to be set. See pfe_ct_if_op_mode_t.
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 */
errno_t pfe_phy_if_set_op_mode(pfe_phy_if_t *iface, pfe_ct_if_op_mode_t mode)
{
	errno_t ret;
	pfe_platform_rpc_pfe_phy_if_set_op_mode_arg_t arg = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	Update the interface structure */
#ifndef PFE_CFG_TARGET_OS_AUTOSAR
	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}
#endif /* PFE_CFG_TARGET_OS_AUTOSAR */

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to change the operation mode */
	arg.phy_if_id = iface->id;
	arg.op_mode = mode;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_SET_OP_MODE, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_SET_OP_MODE failed: %d\n", ret);
	}

	(void)pfe_phy_if_db_unlock();

#ifndef PFE_CFG_TARGET_OS_AUTOSAR
	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}
#endif /* PFE_CFG_TARGET_OS_AUTOSAR */

	return ret;
}

/**
 * @brief		Bind interface with EMAC
 * @param[in]	iface The interface instance
 * @param[in]	emac The EMAC instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 * @retval		EPERM Operation not permitted
 */
errno_t pfe_phy_if_bind_emac(pfe_phy_if_t *iface, pfe_emac_t *emac)
{
	errno_t ret = EOK;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == emac) || (NULL == iface)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	We're not going to allow slave driver to do this */
	NXP_LOG_ERROR("%s: Not supported\n", __func__);
	ret = ENOTSUP;

	return ret;
}

/**
 * @brief		Bind interface with HIF channel
 * @param[in]	iface The interface instance
 * @param[in]	hif The HIF channel instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 * @retval		EPERM Operation not permitted
 */
errno_t pfe_phy_if_bind_hif(pfe_phy_if_t *iface, pfe_hif_chnl_t *hif)
{
	errno_t ret = EOK;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == hif) || (NULL == iface)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	We're not going to allow slave driver to do this */
	NXP_LOG_ERROR("%s: Not supported\n", __func__);
	ret = ENOTSUP;

	return ret;
}

/**
 * @brief		Initialize util physical interface
 * @param[in]	iface The interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 * @retval		EPERM Operation not permitted
 */
errno_t pfe_phy_if_bind_util(pfe_phy_if_t *iface)
{
	errno_t ret = EOK;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	We're not going to allow slave driver to do this */
	NXP_LOG_ERROR("%s: Not supported\n", __func__);
	ret = ENOTSUP;

	return ret;
}

/**
 * @brief		Check if interface is enabled
 * @param[in]	iface The interface instance
 * @retval		TRUE if enabled
 * @retval		FALSE if disabled
 */
bool_t pfe_phy_if_is_enabled(pfe_phy_if_t *iface)
{
	errno_t ret;
	bool_t status = FALSE;
	pfe_platform_rpc_pfe_phy_if_is_enabled_arg_t arg = {0};
	pfe_platform_rpc_pfe_phy_if_is_enabled_ret_t rpc_ret = {0};

#if defined(GLOBAL_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return FALSE;
	}
#endif /* GLOBAL_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to enable the interface */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_IS_ENABLED, &arg, sizeof(arg), &rpc_ret, sizeof(rpc_ret));
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_IS_ENABLED failed: %d\n", ret);
	}
	else
	{
		status = rpc_ret.status;
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return status;
}

/**
 * @brief		Enable interface (RX/TX)
 * @param[in]	iface The interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 */
errno_t pfe_phy_if_enable(pfe_phy_if_t *iface)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_enable_arg_t arg = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to enable the interface */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_ENABLE, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_ENABLE failed: %d\n", ret);
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

errno_t pfe_phy_if_disable_nolock(pfe_phy_if_t *iface)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_disable_arg_t arg = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to disable the interface */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_DISABLE, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_DISABLE failed: %d\n", ret);
	}

	(void)pfe_phy_if_db_unlock();

	return ret;
}

/**
 * @brief		Disable interface (RX/TX)
 * @param[in]	iface The interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 */
errno_t pfe_phy_if_disable(pfe_phy_if_t *iface)
{
	errno_t ret;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	ret = pfe_phy_if_disable_nolock(iface);

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Set physical interface flag
 * @param[in]	iface The interface instance
 * @param[in]	flag The flag to set
 * @return		EOK if success, error code otherwise
 */
errno_t pfe_phy_if_set_flag(pfe_phy_if_t *iface, pfe_ct_if_flags_t flag)
{
	errno_t ret;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	We're not going to allow slave driver to do this */
	NXP_LOG_ERROR("%s: Not supported\n", __func__);
	ret = ENOTSUP;

	return ret;
}

/**
 * @brief		Clear physical interface flag
 * @param[in]	iface The interface instance
 * @param[in]	flag The flag to clear
 * @return		EOK if success, error code otherwise
 */
errno_t pfe_phy_if_clear_flag(pfe_phy_if_t *iface, pfe_ct_if_flags_t flag)
{
	errno_t ret;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	We're not going to allow slave driver to do this */
	NXP_LOG_ERROR("%s: Not supported\n", __func__);
	ret = ENOTSUP;

	return ret;
}

/**
 * @brief		Get physical interface flag
 * @param[in]	iface The interface instance
 * @param[in]	flag The flag to check
 * @return		Flag if 'flag' is set, zero (IF_FL_NONE) otherwise
 */
pfe_ct_if_flags_t pfe_phy_if_get_flag(pfe_phy_if_t *iface, pfe_ct_if_flags_t flag)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return IF_FL_NONE;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	/*	We're not going to allow slave driver to do this */
	NXP_LOG_ERROR("%s: Not supported\n", __func__);

	return IF_FL_NONE;
}

/**
 * @brief		Check if phy_if in promiscuous mode
 * @param[in]	iface The interface instance
 * @retval		TRUE promiscuous mode is enabled
 * @retval		FALSE  promiscuous mode is disbaled
 */
bool_t pfe_phy_if_is_promisc(pfe_phy_if_t *iface)
{
	errno_t ret;
	bool_t status = FALSE;
	pfe_platform_rpc_pfe_phy_if_is_promisc_arg_t arg = {0};
	pfe_platform_rpc_pfe_phy_if_is_promisc_ret_t rpc_ret = {0};

#if defined(GLOBAL_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return FALSE;
	}
#endif /* GLOBAL_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to enable the interface */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_IS_PROMISC, &arg, sizeof(arg), &rpc_ret, sizeof(rpc_ret));
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_IS_ENABLED failed: %d\n", ret);
	}
	else
	{
		status = rpc_ret.status;
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return status;
}

/**
 * @brief		Enable promiscuous mode
 * @param[in]	iface The interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 */
errno_t pfe_phy_if_promisc_enable(pfe_phy_if_t *iface)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_promisc_enable_arg_t arg = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to enable the promiscuous mode */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_PROMISC_ENABLE, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_PROMICS_ENABLE failed: %d\n", ret);
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Disable promiscuous mode
 * @param[in]	iface The interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 */
errno_t pfe_phy_if_promisc_disable(pfe_phy_if_t *iface)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_promisc_enable_arg_t arg = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to disable the promiscuous mode */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_PROMISC_DISABLE, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_PROMICS_DISABLE failed: %d\n", ret);
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Enable loopback mode
 * @param[in]	iface The interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 */
errno_t pfe_phy_if_loopback_enable(pfe_phy_if_t *iface)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_loopback_enable_arg_t arg = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/* Ask the master driver to enable the loopback mode */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_LOOPBACK_ENABLE, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_LOOPBACK_ENABLE failed: %d\n", ret);
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Disable loopback mode
 * @param[in]	iface The interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 */
errno_t pfe_phy_if_loopback_disable(pfe_phy_if_t *iface)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_loopback_disable_arg_t arg = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/* Ask the master driver to disable the loopback mode */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_LOOPBACK_DISABLE, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_PROMICS_DISABLE failed: %d\n", ret);
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Enable ALLMULTI mode
 * @param[in]	iface The interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 */
errno_t pfe_phy_if_allmulti_enable(pfe_phy_if_t *iface)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_allmulti_enable_arg_t arg = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to enable the allmulti mode */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_ALLMULTI_ENABLE, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_ALLMULTI_ENABLE failed: %d\n", ret);
	}

	(void)pfe_phy_if_db_unlock();

	return ret;
}

/**
 * @brief		Disable ALLMULTI mode
 * @param[in]	iface The interface instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 */
errno_t pfe_phy_if_allmulti_disable(pfe_phy_if_t *iface)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_allmulti_disable_arg_t arg = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to disable the allmulti mode */
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_ALLMULTI_DISABLE, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_ALLMULTI_DISABLE failed: %d\n", ret);
	}

	(void)pfe_phy_if_db_unlock();

	return ret;
}

/**
 * @brief       Get rx/tx flow control config
 * @param[in]   iface The interface instance
 * @param[out]  tx_ena tx flow control status
 * @param[out]  rx_ena rx flow control status
 * @return      EOK on success
 */
errno_t pfe_phy_if_get_flow_control(pfe_phy_if_t *iface, bool_t* tx_ena, bool_t* rx_ena)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
        }
#endif /* PFE_CFG_NULL_ARG_CHECK */

	return ENOTSUP;
}

/**
 * @brief       Set tx flow control
 * @param[in]   iface The interface instance
 * @param[in]   tx_ena TRUE: enable flow control, FALSE: disable flow control
 * @return      EOK on success
 */
errno_t pfe_phy_if_set_tx_flow_control(pfe_phy_if_t *iface, bool_t tx_ena)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	return ENOTSUP;
}

/**
 * @brief       Set rx flow control
 * @param[in]   iface The interface instance
 * @param[in]   rx_ena TRUE: enable flow control, FALSE: disable flow control
 * @return      EOK on success
 */
errno_t pfe_phy_if_set_rx_flow_control(pfe_phy_if_t *iface, bool_t rx_ena)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	return ENOTSUP;
}

/**
 * @brief		Add new MAC address
 * @param[in]	iface The interface instance
 * @param[in]	addr The MAC address to add
 * @param[in]	owner The identification of driver instance
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 * @retval		ENOEXEC Command failed
 */
errno_t pfe_phy_if_add_mac_addr(pfe_phy_if_t *iface, const pfe_mac_addr_t addr, pfe_ct_phy_if_id_t owner)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_add_mac_addr_arg_t arg = {0};

	ct_assert(sizeof(pfe_mac_addr_t) == sizeof(arg.mac_addr));

	(void)owner; /* Owner will be added directly to the RPC */

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/*	Add address to local database */
	ret = pfe_mac_db_add_addr(iface->mac_db, addr, owner);
	if(EOK == ret)
	{
		/*	Ask the master driver to add the MAC address */
		memcpy(&arg.mac_addr[0], addr, sizeof(arg.mac_addr));
		arg.phy_if_id = iface->id;
		ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_ADD_MAC_ADDR, &arg, sizeof(arg), NULL, 0U);
		if (EOK != ret)
		{
			NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_ADD_MAC_ADDR failed: %d\n", ret);
			ret = pfe_mac_db_del_addr(iface->mac_db, addr);
			if(EOK != ret)
			{
				NXP_LOG_WARNING("Unable to remove MAC address from phy_if MAC database: %d\n", ret);
			}
		}
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Delete MAC address
 * @param[in]	iface The interface instance
 * @param[in]	addr The MAC address to delete
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 * @retval		ENOENT Address not found
 */
errno_t pfe_phy_if_del_mac_addr(pfe_phy_if_t *iface, const pfe_mac_addr_t addr)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_del_mac_addr_arg_t arg = {0};

	ct_assert(sizeof(pfe_mac_addr_t) == sizeof(arg.mac_addr));

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to delete the MAC address */
	memcpy(&arg.mac_addr[0], addr, sizeof(arg.mac_addr));
	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_DEL_MAC_ADDR, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_DEL_MAC_ADDR failed: %d\n", ret);
	}
	else
	{
		ret = pfe_mac_db_del_addr(iface->mac_db, addr);
		if(EOK != ret)
		{
			NXP_LOG_WARNING("Unable to remove MAC address from phy_if MAC database: %d\n", ret);
		}
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Get handle of internal MAC database
 * @param[in]	iface The interface instance
 * @retval		Database handle.
 */
pfe_mac_db_t *pfe_phy_if_get_mac_db(const pfe_phy_if_t *iface)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return NULL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	return iface->mac_db;
}

/**
 * @brief		Get MAC address
 * @param[in]	iface The interface instance
 * @param[out]	addr The MAC address will be written here
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 * @retval		ENOENT No address found
 */
errno_t pfe_phy_if_get_mac_addr(pfe_phy_if_t *iface, pfe_mac_addr_t addr)
{
	errno_t ret = EOK;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	ret = pfe_mac_db_get_first_addr(iface->mac_db, MAC_DB_CRIT_ALL, PFE_TYPE_ANY, PFE_CFG_LOCAL_IF, addr);
	if(EOK != ret)
	{
		NXP_LOG_WARNING("unable to get MAC address: %d\n", ret);
	}

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Delete MAC addresses added by owner with defined type
 * @param[in]	iface The interface instance
 * @param[in]	crit All, Owner, Type or Owner&Type criterion
 * @param[in]	type Required type of MAC address (Broadcast, Multicast, Unicast, ANY) criterion
 * @param[in]	owner Required owner of MAC address
 * @retval		EOK Success
 * @retval		EINVAL Invalid or missing argument
 * @retval		ENOEXEC Command failed
 */
errno_t pfe_phy_if_flush_mac_addrs(pfe_phy_if_t *iface, pfe_mac_db_crit_t crit, pfe_mac_type_t type, pfe_ct_phy_if_id_t owner)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_flush_mac_addrs_arg_t arg = {0};
	(void)owner; /* Owner will be added directly to the RPC */

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	(void)pfe_phy_if_db_lock();

	/*	Ask the master driver to flush owner's MAC addresses due to flush mode */
	arg.phy_if_id = iface->id;
	arg.crit = crit;
	arg.type = type;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_FLUSH_MAC_ADDRS, &arg, sizeof(arg), NULL, 0U);
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_FLUSH_MAC_ADDRS failed: %d\n", ret);
	}
	else
	{
		/*	Remove MAC addresses also from local database */
		ret = pfe_mac_db_flush(iface->mac_db, crit, type, owner);
		if(EOK != ret)
		{
			NXP_LOG_DEBUG("Unable to flush MAC address from phy_if MAC database: %d\n", ret);
		}	
	}

	(void)pfe_phy_if_db_unlock();

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Get HW ID of the interface
 * @param[in]	iface The interface instance
 * @return		Interface ID
 */
__attribute__((pure)) pfe_ct_phy_if_id_t pfe_phy_if_get_id(const pfe_phy_if_t *iface)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return PFE_PHY_IF_ID_INVALID;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	return iface->id;
}

/**
 * @brief		Get name
 * @param[in]	iface The interface instance
 * @return		Pointer to interface name string or NULL if not found/failed
 */
__attribute__((pure)) char_t *pfe_phy_if_get_name(const pfe_phy_if_t *iface)
{
	static char_t *unknown = "(unknown)";

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return NULL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (NULL == iface)
	{
		return unknown;
	}
	else
	{
		return iface->name;
	}

	return iface->name;
}

/**
 * @brief		Get phy interface statistics
 * @param[in]	iface The interface instance
 * @param[out]	stat Statistic structure
 * @retval		EOK Success
 * @retval		NOMEM Not possible to allocate memory for read
 */
errno_t pfe_phy_if_get_stats(pfe_phy_if_t *iface, pfe_ct_phy_if_stats_t *stat)
{
	errno_t ret = EOK;
	pfe_platform_rpc_pfe_phy_if_stats_arg_t arg = {0};
	pfe_platform_rpc_pfe_phy_if_stats_ret_t rpc_ret = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == iface) || (NULL == stat)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (EOK != oal_mutex_lock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex lock failed\n");
	}

	arg.phy_if_id = iface->id;
	ret = pfe_idex_master_rpc(PFE_PLATFORM_RPC_PFE_PHY_IF_STATS, &arg, sizeof(arg), &rpc_ret, sizeof(rpc_ret));
	if (EOK != ret)
	{
		NXP_LOG_DEBUG("PFE_PLATFORM_RPC_PFE_PHY_IF_IS_STATS failed: %d\n", ret);
	}
	else
	{
		memcpy(stat,&rpc_ret.stats,sizeof(rpc_ret.stats));
	}

	if (EOK != oal_mutex_unlock(&iface->lock))
	{
		NXP_LOG_DEBUG("mutex unlock failed\n");
	}

	return ret;
}

/**
 * @brief		Return physical interface runtime statistics in text form
 * @details		Function writes formatted text into given buffer.
 * @param[in]	iface 		The physical interface instance
 * @param[in]	buf 		A pointer to the buffer to write to
 * @param[in]	buf_len 	Buffer length
 * @param[in]	verb_level 	Verbosity level
 * @return		Number of bytes written to the buffer
 */
uint32_t pfe_phy_if_get_text_statistics(const pfe_phy_if_t *iface, char_t *buf, uint32_t buf_len, uint8_t verb_level)
{
	uint32_t len = 0U;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == iface))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return 0U;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	len += oal_util_snprintf(buf + len, buf_len - len, "[PhyIF 0x%x]: Unable to read DMEM (not implemented)\n", iface->id);
	
	return len;
}

#endif /* PFE_CFG_PFE_SLAVE */
