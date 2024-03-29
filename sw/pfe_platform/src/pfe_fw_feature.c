/* =========================================================================
 *  
 *  Copyright (c) 2019 Imagination Technologies Limited
 *  Copyright 2020-2021 NXP
 *
 *  SPDX-License-Identifier: GPL-2.0
 *
 * ========================================================================= */

#include "pfe_cfg.h"
#include "pfe_ct.h"
#include "oal.h"
#include "pfe_class.h"
#include "pfe_fw_feature.h"

struct pfe_fw_feature_tag
{
    pfe_ct_feature_desc_t *ll_data;
    const char *string_base;
    dmem_read_func_t dmem_read_func;
    dmem_write_func_t dmem_write_func;
    void *dmem_rw_func_data;  
};

/**
 * @brief Creates a feature instance
 * @return The created feature instance or NULL in case of failure
 */
pfe_fw_feature_t *pfe_fw_feature_create(void)
{
    pfe_fw_feature_t *feature;
    feature = oal_mm_malloc(sizeof(pfe_fw_feature_t));
    if(NULL != feature)
    {
        (void)memset(feature, 0U, sizeof(pfe_fw_feature_t));
    }
    else
    {
        NXP_LOG_ERROR("Cannot allocate %u bytes of memory for feature\n", (uint_t)sizeof(pfe_fw_feature_t));
    }
    return feature;
}

/**
 * @brief Destroys a feature instance previously created by pfe_fw_feature_create()
 * @param[in] feature Previously created feature
 */
void pfe_fw_feature_destroy(const pfe_fw_feature_t *feature)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == feature))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */     
    oal_mm_free(feature);
}

/**
 * @brief Sets reference to low level data in obtained from PE
 * @param[in] feature Feature to set the low level data
 * @param[in] ll_data Low level data to set
 * @return EOK or an error code.
 */
errno_t pfe_fw_feature_set_ll_data(pfe_fw_feature_t *feature, pfe_ct_feature_desc_t *ll_data)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == feature)||(NULL == ll_data)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */      
    feature->ll_data = ll_data;
    return EOK;
}

/**
 * @brief Sets the base address for the strings
 * @param[in] feature Feature which string base address shall be set
 * @param[in] string_base String base address to be set
 * @return EOK or an error code.
 * @details All features use the same base address which is actually pointer to copy of elf-section 
 *          .features loaded by PE. All strings are stored there and their addresses are stored in
 *          the low level data set by pfe_fw_feature_set_ll_data(). 
 */
errno_t pfe_fw_feature_set_string_base(pfe_fw_feature_t *feature, const char *string_base)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == feature)||(NULL == string_base)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */      
    feature->string_base = string_base;
    return EOK;
}

/**
 * @brief Sets the functions to access PEs DMEM
 * @param[in] feature Feature to set the functions
 * @param[in] read_func Function to read the PE DMEM data 
 * @param[in] write_func Function to write PE DMEM data
 * @param[in] data Class/Util reference used by read_func/write_func.
 * @return EOK or an error code.
 */
errno_t pfe_fw_feature_set_dmem_funcs(pfe_fw_feature_t *feature, dmem_read_func_t read_func, dmem_write_func_t write_func, void *data)
{    
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == feature)||
                 (NULL == read_func)||
                 (NULL == write_func)||
                 (NULL == data)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */  
    feature->dmem_read_func = read_func;
    feature->dmem_write_func = write_func;
    feature->dmem_rw_func_data = data;
    return EOK;
}

/**
 * @brief Returns name of the feature
 * @param[in] feature Feature to be read.
 * @param[out] name The feature name to be read.
 * @return EOK or an error code. 
 */
errno_t pfe_fw_feature_get_name(const pfe_fw_feature_t *feature, const char **name)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == feature)||(NULL == name)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */  
    *name = feature->string_base + oal_ntohl(feature->ll_data->name);
    return EOK;
    
}

/**
 * @brief Returns the feature description provide by the firmware.
 * @param[in] feature Feature to be read.
 * @param[out] desc Descripton of the feature
 * @return EOK or an error code. 
 */
errno_t pfe_fw_feature_get_desc(const pfe_fw_feature_t *feature, const char **desc)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == feature)||(NULL ==desc)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */  
    *desc = feature->string_base + oal_ntohl(feature->ll_data->description);
    return EOK;
    
}

/**
 * @brief Reads the configuration variant used by the firmware
 * @param[in] Feature to be read
 * @param[out] variant The read variant code 
 * @return EOK or an error code. 
 * @details The variants are always disabled (0), always enabled (1), configured by driver (2).
 */
errno_t pfe_fw_feature_get_variant(const pfe_fw_feature_t *feature, uint8_t *variant)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == feature)||(NULL==variant)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */  
    *variant = feature->ll_data->variant;
    return EOK;
    
}

/**
 * @brief Reads the default value of the feature i.e. initial value set by the FW
 * @param[in] feature Feature to read the value
 * @param[out] def_val The read default value.
 * @return EOK or an error code.
 */
errno_t pfe_fw_feature_get_def_val(const pfe_fw_feature_t *feature, uint8_t *def_val)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == feature)||(NULL == def_val)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */  
    *def_val = feature->ll_data->def_val;
    return EOK;
    
}

/**
 * @brief Reads value of the feature enable variable
 * @param[in] Feature to read the value
 * @param[out] val Value read from the DMEM
 * @return EOK or an error code. 
 */
errno_t pfe_fw_feature_get_val(const pfe_fw_feature_t *feature, uint8_t *val)
{
	 errno_t ret;
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == feature)||(NULL == val)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */  
    ret = feature->dmem_read_func(feature->dmem_rw_func_data, 0U, val, (addr_t)oal_ntohl(feature->ll_data->position), sizeof(uint8_t));
    return ret;
    
}

/**
 * @brief Checks whether the given feature is in enabled state
 * @param[in] feature Feature to check the enabled state
 * @retval TRUE Feature is enabled (the enable variable value is not 0)
 * @retval FALSE Feature is disable (or its state could not be read)
 */
bool_t pfe_fw_feature_enabled(const pfe_fw_feature_t *feature)
{
	uint8_t val;
	errno_t ret;
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == feature))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return FALSE;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */  

	ret = pfe_fw_feature_get_val(feature, &val);
	if(EOK != ret) 
	{
		return FALSE;
	}
	if(0U != val)
	{
		return TRUE;
	}
	return FALSE;
}

/**
 * @brief Sets value of the feature enable variable in the DMEM 
 * @param[in] feature Feature to set the value
 * @param[in] val Value to be set
 * @return EOK or an error code.
 */
errno_t pfe_fw_feature_set_val(const pfe_fw_feature_t *feature, uint8_t val)
{
	 errno_t ret;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == feature))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */  
	ret = feature->dmem_write_func(feature->dmem_rw_func_data, -1, (addr_t)oal_ntohl(feature->ll_data->position), (void *)&val, sizeof(uint8_t));
	return ret;
}
