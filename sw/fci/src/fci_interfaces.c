/* =========================================================================
 *  Copyright 2018-2021 NXP
 *
 *  SPDX-License-Identifier: GPL-2.0
 *
 * ========================================================================= */

/**
 * @addtogroup  dxgr_FCI
 * @{
 *
 * @file		fci_interfaces.c
 * @brief		Ethernet interfaces management functions.
 * @details		All interfaces-related functionality provided by the FCI should be
 * 				implemented within this file. This includes commmands dedicated
 *				to register and unregister interface to/from the FCI.
 *
 */

#include "pfe_cfg.h"
#include "libfci.h"
#include "fpp.h"
#include "fpp_ext.h"
#include "fci_internal.h"
#include "fci_fp_db.h"
#include "fci.h"



static errno_t fci_interfaces_get_arg_info(fpp_if_m_args_t *m_arg, fpp_if_m_rules_t rule, void **offset, size_t *size, uint32_t *fp_table_addr);

/*
 * @brief			Get offset and size of the rule
 * @details			Errors are handled in platform driver
 * @param[in]		m_args pointer to the argument structure
 * @param[in]		rule single rule. See pfe_ct_if_m_rules_t
 * @param[in,out]	offset is set based on the rule to the structure m_arg
 * @param[in,out]	size of the underlying type in the struct based on the rule
 */
static errno_t fci_interfaces_get_arg_info(fpp_if_m_args_t *m_arg, fpp_if_m_rules_t rule, void **offset, size_t *size, uint32_t *fp_table_addr)
{
	errno_t retval = EOK; /* Function return value */
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == m_arg) || (NULL == offset) || (NULL == size)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	switch (rule)
	{
		case IF_MATCH_VLAN:
		{
			*size = sizeof(m_arg->vlan);
			*offset = &m_arg->vlan;
			break;
		}

		case IF_MATCH_PROTO:
		{
			*size = sizeof(m_arg->proto);
			*offset = &m_arg->proto;
			break;
		}

		case IF_MATCH_SPORT:
		{
			*size = sizeof(m_arg->sport);
			*offset = &m_arg->sport;
			break;
		}

		case IF_MATCH_DPORT:
		{
			*size = sizeof(m_arg->dport);
			*offset = &m_arg->dport;
			break;
		}

		case IF_MATCH_SIP6:
		{
			*size = sizeof(m_arg->ipv.v6.sip);
			*offset = &m_arg->ipv.v6.sip;
			break;
		}

		case IF_MATCH_DIP6:
		{
			*size = sizeof(m_arg->ipv.v6.dip);
			*offset = &m_arg->ipv.v6.dip;
			break;
		}

		case IF_MATCH_SIP:
		{
			*size = sizeof(m_arg->ipv.v4.sip);
			*offset = &m_arg->ipv.v4.sip;
			break;
		}

		case IF_MATCH_DIP:
		{
			*size = sizeof(m_arg->ipv.v4.dip);
			*offset = &m_arg->ipv.v4.dip;
			break;
		}

		case IF_MATCH_ETHTYPE:
		{
			*size = sizeof(m_arg->ethtype);
			*offset = &m_arg->ethtype;
			break;
		}

		case IF_MATCH_FP0:
		{
			/* Get the table address in the HW */
			*fp_table_addr = oal_htonl(fci_fp_db_get_table_dmem_addr(m_arg->fp_table0));
			if(0 == *fp_table_addr)
			{
				retval = ENOENT;
			}
			*offset = fp_table_addr;
			*size = sizeof(uint32_t);
			break;
		}

		case IF_MATCH_FP1:
		{
			/* Get the table address in the HW */
			*fp_table_addr = oal_htonl(fci_fp_db_get_table_dmem_addr(m_arg->fp_table1));
			if(0 == *fp_table_addr)
			{
				retval = ENOENT;
			}
			*offset = fp_table_addr;
			*size = sizeof(uint32_t);
			break;
		}

		case IF_MATCH_SMAC:
		{
			*size = sizeof(m_arg->smac);
			*offset = &m_arg->smac;
			break;
		}

		case IF_MATCH_DMAC:
		{
			*size = sizeof(m_arg->dmac);
			*offset = &m_arg->dmac;
			break;
		}

		case IF_MATCH_HIF_COOKIE:
		{
			*size = sizeof(m_arg->hif_cookie);
			*offset = &m_arg->hif_cookie;
			break;
		}

		default:
		{
			*size = 0U;
			*offset = NULL;
		}
	}
	return retval;
}

/**
 * @brief			Destroy FP tables if they are used.
 * 					Auxiliary function for logical interface processing.
 * @param[in]		match	Match rules of a logical interface.
 * @param[in]		args	Match rule arguments of a logical interface.
 * @return			EOK if success, error code otherwise
 */
static errno_t fci_interfaces_destroy_fptables(const fpp_if_m_rules_t match, const pfe_ct_if_m_args_t* args)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (NULL == args)
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	char_t *table_name;
	if(FPP_IF_MATCH_FP0 == (match & FPP_IF_MATCH_FP0))
	{	/* A flexible parser table was dropped - it needs to be destroyed if it existed */
		if(0 != args->fp0_table)
		{	/* Table existed */
			fci_fp_db_get_table_from_addr(args->fp0_table, &table_name);
			fci_fp_db_pop_table_from_hw(table_name);
		}
	}
	if(FPP_IF_MATCH_FP1 == (match & FPP_IF_MATCH_FP1))
	{	/* A flexible parser table was dropped - it needs to be destroyed if it existed */
		if(0 != args->fp1_table)
		{	/* Table existed */
			fci_fp_db_get_table_from_addr(args->fp1_table, &table_name);
			fci_fp_db_pop_table_from_hw(table_name);
		}
	}

	return EOK;
}

/**
 * @brief			Process interface atomic session related commands
 * @param[in]		msg FCI cmd code
 * @param[out]		fci_ret FCI return code
 * @return			EOK if success, error code otherwise
 */
errno_t fci_interfaces_session_cmd(uint32_t code, uint16_t *fci_ret)
{
	fci_t *context = (fci_t *)&__context;
	errno_t ret = EOK;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely(NULL == fci_ret))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	switch(code)
	{
		case FPP_CMD_IF_LOCK_SESSION:
		{
			*fci_ret = FPP_ERR_OK;
			if (EOK != pfe_if_db_lock(&context->if_session_id))
			{
				*fci_ret = FPP_ERR_IF_RESOURCE_ALREADY_LOCKED;
				NXP_LOG_DEBUG("DB lock failed\n");
			}
			break;
		}
		case FPP_CMD_IF_UNLOCK_SESSION:
		{
			*fci_ret = FPP_ERR_OK;
			if (EOK != pfe_if_db_unlock(context->if_session_id))
			{
				*fci_ret = FPP_ERR_IF_WRONG_SESSION_ID;
				NXP_LOG_DEBUG("DB unlock failed due to incorrect session ID\n");
			}
			break;
		}
		default:
		{
			NXP_LOG_ERROR("Unknown Interface Session Command Received\n");
			*fci_ret = FPP_ERR_UNKNOWN_ACTION;
			break;
		}
	}
	return ret;
}

/**
 * @brief			Process FPP_CMD_LOG_IF commands
 * @param[in]		msg FCI message containing the FPP_CMD_LOG_IF command
 * @param[out]		fci_ret FCI command return value
 * @param[out]		reply_buf Pointer to a buffer where function will construct command reply (fpp_log_if_cmd_t)
 * @param[in,out]	reply_len Maximum reply buffer size on input, real reply size on output (in bytes)
 * @return			EOK if success, error code otherwise
 * @note			Function is only called within the FCI worker thread context.
 * @note			Must run with interface DB session lock.
 */
errno_t fci_interfaces_log_cmd(fci_msg_t *msg, uint16_t *fci_ret, fpp_log_if_cmd_t *reply_buf, uint32_t *reply_len)
{
	fci_t *context = (fci_t *)&__context;
	fpp_log_if_cmd_t *if_cmd;
	errno_t ret = EOK;
	pfe_ct_if_m_args_t args;
	pfe_ct_if_m_rules_t rules;
	pfe_if_db_entry_t *entry = NULL;
	pfe_phy_if_t *phy_if = NULL;
	pfe_log_if_t *log_if = NULL;
	uint32_t index = 0U, egress = 0U;
	size_t size = 0U;
	void *offset = NULL;
	uint32_t fp_table_addr;
	uint32_t fp_table_destroy[2];
	char_t *table_name;
	pfe_ct_class_algo_stats_t stats = {0};

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == msg) || (NULL == fci_ret) || (NULL == reply_buf) || (NULL == reply_len)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}

	if (unlikely(FALSE == context->fci_initialized))
	{
		NXP_LOG_ERROR("Context not initialized\n");
		return EPERM;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (*reply_len < sizeof(fpp_log_if_cmd_t))
	{
		NXP_LOG_ERROR("Buffer length does not match expected value (fpp_if_cmd_t)\n");
		return EINVAL;
	}
	else
	{
		/*	No data written to reply buffer (yet) */
		*reply_len = 0U;
	}

	/*	Initialize the reply buffer */
	memset(reply_buf, 0, sizeof(fpp_log_if_cmd_t));

	if_cmd = (fpp_log_if_cmd_t *)msg->msg_cmd.payload;

	switch(if_cmd->action)
	{
		case FPP_ACTION_REGISTER:
		{
			/* Get the intended parent physical interface */
			ret = pfe_if_db_get_first(context->phy_if_db, context->if_session_id, IF_DB_CRIT_BY_NAME, if_cmd->parent_name, &entry);
			if(EOK != ret)
			{
				*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				break;
			}
			phy_if = pfe_if_db_entry_get_phy_if(entry);
			if(NULL == phy_if)
			{
				*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				ret = ENOENT;
				break;
			}
			/* Create the logical interface */
			log_if = pfe_log_if_create(phy_if, if_cmd->name);
			if(NULL == log_if)
			{
				*fci_ret = FPP_ERR_IF_OP_CANNOT_CREATE;
				ret = ENOENT;
				break;
			}
			/* Add the interface into the database */
			ret = pfe_if_db_add(context->log_if_db, context->if_session_id, log_if, pfe_phy_if_get_id(phy_if));
			if(EOK != ret)
			{
				pfe_log_if_destroy(log_if);
				*fci_ret = FPP_ERR_IF_OP_CANNOT_CREATE;
				break;
			}
			NXP_LOG_INFO("Added logical interface %s to physical interface %s\n", if_cmd->name, if_cmd->parent_name);
			break;
		}

		case FPP_ACTION_DEREGISTER:
		{
			ret = pfe_if_db_get_first(context->log_if_db, context->if_session_id, IF_DB_CRIT_BY_NAME, if_cmd->name, &entry);

			if(EOK != ret)
			{
				NXP_LOG_ERROR("Incorrect session ID detected\n");
				*fci_ret = FPP_ERR_IF_WRONG_SESSION_ID;
				break;
			}

			/* Check if entry is not NULL and get logical interface */
			if(NULL != entry)
			{
				log_if = pfe_if_db_entry_get_log_if(entry);
			}

			/* Check if the entry exists */
			if((NULL == entry) || (NULL == log_if))
			{
				/* Interface doesn't exist or couldn't be extracted from the entry */
				*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				ret = ENOENT;
				break;
			} 

			/* Destroy FP tables if they were used by this interface. */
			if(EOK == pfe_log_if_get_match_rules(log_if, &rules, &args))
			{
				/* Fix endians of FP tables */
				args.fp0_table = oal_ntohl(args.fp0_table);
				args.fp1_table = oal_ntohl(args.fp1_table);

				/* Destroy FP tables */
				fci_interfaces_destroy_fptables(rules, &args);
			}

			/* Remove interface from the database */
			pfe_if_db_remove(context->log_if_db, context->if_session_id, entry);
			/* Destroy the interface */
			pfe_log_if_destroy(log_if);
			break;
		}

		case FPP_ACTION_UPDATE:
		{
			*fci_ret = FPP_ERR_OK;
			*reply_len = sizeof(fpp_log_if_cmd_t);

			ret = pfe_if_db_get_first(context->log_if_db, context->if_session_id, IF_DB_CRIT_BY_NAME, if_cmd->name, &entry);

			if(EOK != ret)
			{
				NXP_LOG_ERROR("Incorrect session ID detected\n");
				*fci_ret = FPP_ERR_IF_WRONG_SESSION_ID;
				break;
			}

			/* Check if entry is not NULL and get logical interface */
			if(NULL != entry)
			{
				log_if = pfe_if_db_entry_get_log_if(entry);
			}

			/* Check if the entry exists */
			if((NULL == entry) || (NULL == log_if))
			{
				/* Interface doesn't exist or couldn't be extracted from the entry */
				*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				break;
			}

			/* Get the currently set rules */
			ret = pfe_log_if_get_match_rules(log_if, &rules, &args);
			if(ret != EOK)
			{
				*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				break;
			}

			/* Fix endians of FP tables */
			args.fp0_table = oal_ntohl(args.fp0_table);
			args.fp1_table = oal_ntohl(args.fp1_table);

			/* Drop all unset rules (if any) */
			ret = pfe_log_if_del_match_rule(log_if, ~oal_ntohl(if_cmd->match));

			/* Destroy FP tables if they are not used by new rules */
			fci_interfaces_destroy_fptables(~oal_ntohl(if_cmd->match), &args);

			if(EOK == ret)
			{
				NXP_LOG_INFO("All match rules were dropped on %s before match rule update.\n",  pfe_log_if_get_name(log_if));
			}
			else
			{
				NXP_LOG_ERROR("Dropping of all match rules on logical interface %s failed !!\n",  pfe_log_if_get_name(log_if));
				*fci_ret = FPP_ERR_IF_MATCH_UPDATE_FAILED;
			}
			/* Clear the storage for queues to be destroyed */
			fp_table_destroy[0] = 0U;
			fp_table_destroy[1] = 0U;
			/* We are going to configure Flexible parser - prepare table(s) */
			if(FPP_IF_MATCH_FP0 == (oal_ntohl(if_cmd->match) & FPP_IF_MATCH_FP0))
			{
				/* Get the newly configured table address */
				fp_table_addr = fci_fp_db_get_table_dmem_addr(if_cmd->arguments.fp_table0);
				if(0 == fp_table_addr)
				{   /* Table has not been created yet */
					ret = fci_fp_db_push_table_to_hw(context->class, if_cmd->arguments.fp_table0);
					if(EOK != ret)
					{   /* Failed to write */
						*fci_ret = FPP_ERR_IF_MATCH_UPDATE_FAILED;
						break;
					}
					/* We have just created the table therefore the existing one must be different
					   and it needs to be destroyed before we overwrite the reference */
					if(0 != args.fp0_table)
					{
						/* Table is still in use therefore it cannot be destroyed,
						   just remember it */
						fp_table_destroy[0] = args.fp0_table;
					}
				}
				else
				{   /* Table does exist */
					/* Check whether it is already configured */
					if(fp_table_addr != args.fp0_table)
					{   /* Different table is configured thus the new one must be in use
						   somewhere else (because it does have the address) and cannot be
						   used here */
						NXP_LOG_ERROR("Table %s already in use.\n", if_cmd->arguments.fp_table0);
						*fci_ret = FPP_ERR_IF_MATCH_UPDATE_FAILED;
						break;
					}

				}
			}
			if(FPP_IF_MATCH_FP1 == (oal_ntohl(if_cmd->match) & FPP_IF_MATCH_FP1))
			{
				/* Get the newly configured table address */
				fp_table_addr = fci_fp_db_get_table_dmem_addr(if_cmd->arguments.fp_table1);
				if(0 == fp_table_addr)
				{   /* Table has not been created yet */
					ret = fci_fp_db_push_table_to_hw(context->class, if_cmd->arguments.fp_table1);
					if(EOK != ret)
					{   /* Failed to write */
						*fci_ret = FPP_ERR_IF_MATCH_UPDATE_FAILED;
						break;
					}
					/* We have just created the table therefore the existing one must be different
					   and it needs to be destroyed before we overwrite the reference */
					if(0 != args.fp1_table)
					{
						/* Table is still in use therefore it cannot be destroyed,
						   just remember it */
						*fci_ret = FPP_ERR_IF_MATCH_UPDATE_FAILED;
						break;
					}
				}
				else
				{   /* Table does exist */
					/* Check whether it is already configured */
					if(fp_table_addr != args.fp1_table)
					{   /* Different table is configured thus the new one must be in use
						   somewhere else (because it does have the address) and cannot be
						   used here */
						NXP_LOG_ERROR("Table %s already in use.\n", if_cmd->arguments.fp_table1);
						*fci_ret = FPP_ERR_IF_MATCH_UPDATE_FAILED;
						break;
					}

				}
			}

			/* Update each rule one by one */
			for(index = 0U; 8U * sizeof(if_cmd->match) > index;  ++index)
			{
				if(0U != (oal_ntohl(if_cmd->match) & (1U << index)))
				{
					/* Resolve position of data and size */
					ret = fci_interfaces_get_arg_info(&if_cmd->arguments, oal_ntohl(if_cmd->match) & (1U << index), &offset, &size, &fp_table_addr);
					if(EOK != ret)
					{
						NXP_LOG_ERROR("Failed to get update argument\n");
						*fci_ret = FPP_ERR_IF_MATCH_UPDATE_FAILED;
					}

					/* Add match rule and arguments */
					ret = pfe_log_if_add_match_rule(log_if, oal_ntohl(if_cmd->match) & (1U << index), offset, size);

					if(EOK != ret)
					{
						NXP_LOG_ERROR("Updating single rule on logical interface %s failed !!\n",  pfe_log_if_get_name(log_if));
						*fci_ret = FPP_ERR_IF_MATCH_UPDATE_FAILED;
					}
				}
			}

			/* Now is the time to destroy Flexible Parser tables no longer in use */
			if(0 != fp_table_destroy[0])
			{
				fci_fp_db_get_table_from_addr(fp_table_destroy[0], &table_name);
				fci_fp_db_pop_table_from_hw(table_name);
			}
			if(0 != fp_table_destroy[1])
			{
				fci_fp_db_get_table_from_addr(fp_table_destroy[1], &table_name);
				fci_fp_db_pop_table_from_hw(table_name);
			}

			/* Update egress in case at least one is set (old egress is dropped) */
			if(0 != if_cmd->egress)
			{
				NXP_LOG_INFO("Updating egress interfaces on %s (0x%x)\n",  pfe_log_if_get_name(log_if), oal_ntohl(if_cmd->egress));
				for(index = 0U; PFE_PHY_IF_ID_INVALID > index;  ++index)
				{
					if((PFE_PHY_IF_ID_HIF == index) || (PFE_PHY_IF_ID_HIF_NOCPY == index))
					{
						/* Skip currently not used interfaces */
						continue;
					}
					/* For each bit in egress mask search if the phy if exists */
					ret = pfe_if_db_get_first(context->phy_if_db, context->if_session_id, IF_DB_CRIT_BY_ID, (void *)(addr_t)index, &entry);
					if((EOK == ret) && (NULL != entry))
					{   /* phy if does exist */
						phy_if = pfe_if_db_entry_get_phy_if(entry);

						/* Check whether the phy if shall be added
						   We are getting inputs in network order thus conversion is needed */
						if(0U != (oal_ntohl(if_cmd->egress) & (1U << index)))
						{   /* Add */
							/* If the ID exits add corresponding phy_if as egress to log_if*/
							if (EOK != pfe_log_if_add_egress_if(log_if, phy_if))
							{
								NXP_LOG_ERROR("Could not set egress interface for %s\n", pfe_log_if_get_name(log_if));
								*fci_ret = FPP_ERR_IF_EGRESS_UPDATE_FAILED;
							}
						}
						else
						{   /* Do not add (drop from the list if already on the list) */
							/* Get current egress interfaces */
							ret = pfe_log_if_get_egress_ifs(log_if, &egress);
							if(EOK == ret)
							{
								if(0U != (egress && (1U << index)))
								{   /* Interface is on the current list but not on the requested list - drop it */
									ret= pfe_log_if_del_egress_if(log_if, phy_if);
								}
							}

							if (EOK != ret)
							{
								NXP_LOG_ERROR("Could not get and clear egress interface for %s\n", pfe_log_if_get_name(log_if));
								*fci_ret = FPP_ERR_IF_EGRESS_UPDATE_FAILED;
							}
						}
					}
					else
					{
						NXP_LOG_ERROR("Egress %u on %s is not set because it doesn't exist\n", index,  pfe_log_if_get_name(log_if));

						/* Error in input do not continue */
						*fci_ret = FPP_ERR_IF_EGRESS_DOESNT_EXIST;
						break;
					}
				}
			}

			/* AND/OR rules */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_MATCH_OR))
			{
				ret = pfe_log_if_set_match_or(log_if);
			}
			else
			{
				ret = pfe_log_if_set_match_and(log_if);
			}

			if(EOK != ret)
			{
				NXP_LOG_ERROR("AND/OR flag wans't updated correctly on %s\n",  pfe_log_if_get_name(log_if));
				*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
			}

			/* enable/disable */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_ENABLED))
			{
				ret = pfe_log_if_enable(log_if);
			}
			else
			{
				ret = pfe_log_if_disable(log_if);
			}

			if(EOK != ret)
			{
				NXP_LOG_ERROR("ENABLE flag wasn't updated correctly on %s\n",  pfe_log_if_get_name(log_if));
				*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
			}

			/* loopback */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_LOOPBACK))
			{
				ret = pfe_log_if_loopback_enable(log_if);
			}
			else
			{
				ret = pfe_log_if_loopback_disable(log_if);
			}

			if(EOK != ret)
			{
				NXP_LOG_ERROR("ENABLE flag wasn't updated correctly on %s\n",  pfe_log_if_get_name(log_if));
				*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
			}

			/* promisc */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_PROMISC))
			{
				ret = pfe_log_if_promisc_enable(log_if);
			}
			else
			{
				ret = pfe_log_if_promisc_disable(log_if);
			}

			if(EOK != ret)
			{
				NXP_LOG_ERROR("PROMISC flag wasn't updated correctly on %s\n",  pfe_log_if_get_name(log_if));
				*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
			}

			/* discard */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_DISCARD))
			{
				ret = pfe_log_if_discard_enable(log_if);
			}
			else
			{
				ret = pfe_log_if_discard_disable(log_if);
			}

			if(EOK != ret)
			{
				NXP_LOG_ERROR("DISCARD flag wasn't updated correctly on %s\n",  pfe_log_if_get_name(log_if));
				*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
			}

			break;
		}
		case FPP_ACTION_QUERY:
			{
				ret = pfe_if_db_get_first(context->log_if_db, context->if_session_id, IF_DB_CRIT_ALL, NULL, &entry);
				if (NULL == entry)
				{
					*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
					if(EOK != ret)
					{
						NXP_LOG_ERROR("Incorrect session ID detected\n");
						*fci_ret = FPP_ERR_IF_WRONG_SESSION_ID;
					}
					ret = EOK;
					break;
				}
			}
			/* FALLTHRU */
		case FPP_ACTION_QUERY_CONT:
		{
			if (NULL == entry)
			{
				ret = pfe_if_db_get_next(context->log_if_db, context->if_session_id, &entry);
				if (NULL == entry)
				{
					*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
					if(EOK != ret)
					{
						NXP_LOG_ERROR("Incorrect session ID detected\n");
						*fci_ret = FPP_ERR_IF_WRONG_SESSION_ID;
					}
					ret = EOK;
					break;
				}
			}

			log_if = pfe_if_db_entry_get_log_if(entry);

			if(NULL != log_if)
			{
				phy_if = pfe_log_if_get_parent(log_if);
			}
			/* Store names */
			if(NULL != phy_if)
			{
				strncpy(reply_buf->name, pfe_log_if_get_name(log_if), IFNAMSIZ-1);
				strncpy(reply_buf->parent_name, pfe_phy_if_get_name(phy_if), IFNAMSIZ-1);
			}
			else
			{
				NXP_LOG_DEBUG("Was not possible to resolve DB entry to log_if or parent phy_if");
				*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				break;
			}

			if(EOK != (ret = pfe_log_if_get_stats(log_if,&stats)))
			{
				NXP_LOG_ERROR("Could not get interface statistics\n");
				break;
			}

			/* Copy the log if statistics to reply */
			memcpy(&reply_buf->stats, &stats, sizeof(reply_buf->stats));

			/* Get important flag values */
			reply_buf->flags = 0U;
			if(pfe_log_if_is_enabled(log_if))
			{
				reply_buf->flags |= oal_htonl(FPP_IF_ENABLED);
			}

			if(pfe_log_if_is_loopback(log_if))
			{
				reply_buf->flags |= oal_htonl(FPP_IF_LOOPBACK);
			}

			if(pfe_log_if_is_promisc(log_if))
			{
				reply_buf->flags |= oal_htonl(FPP_IF_PROMISC);
			}

			if(pfe_log_if_is_discard(log_if))
			{
				reply_buf->flags |= oal_htonl(FPP_IF_DISCARD);
			}

			if(pfe_log_if_is_match_or(log_if))
			{
				reply_buf->flags |= oal_htonl(FPP_IF_MATCH_OR);
			}

			/* Store egress interfaces */
			if(EOK != pfe_log_if_get_egress_ifs(log_if, &egress))
			{
				NXP_LOG_DEBUG("Was not possible to get egress interfaces\n");
			}
			reply_buf->egress = oal_htonl(egress);

			/* Store rules for FCI */
			if(EOK != pfe_log_if_get_match_rules(log_if, &rules, &args))
			{
				NXP_LOG_DEBUG("Was not possible to get match rules and arguments\n");
			}

			/* Fix endians of FP tables */
			args.fp0_table = oal_ntohl(args.fp0_table);
			args.fp1_table = oal_ntohl(args.fp1_table);
			reply_buf->match = oal_htonl(rules);

			/* Store match rule arguments for FCI */
			reply_buf->arguments.vlan = args.vlan;
			reply_buf->arguments.ethtype = args.ethtype;
			reply_buf->arguments.sport = args.sport;
			reply_buf->arguments.dport = args.dport;
			reply_buf->arguments.proto = args.proto;
			reply_buf->arguments.hif_cookie = args.hif_cookie;
			memcpy(&reply_buf->arguments.ipv, &args.ipv, sizeof(reply_buf->arguments.ipv));
			memcpy(reply_buf->arguments.smac, args.smac, 6U);
			memcpy(reply_buf->arguments.dmac, args.dmac, 6U);

			/* Translate names of flexible parser tables from addresses to strings. */
			memset(reply_buf->arguments.fp_table0, 0, IFNAMSIZ);
			memset(reply_buf->arguments.fp_table1, 0, IFNAMSIZ);
			if(EOK == fci_fp_db_get_table_from_addr(args.fp0_table, &table_name))
			{
				strcpy(reply_buf->arguments.fp_table0, table_name);
			}
			if(EOK == fci_fp_db_get_table_from_addr(args.fp1_table, &table_name))
			{
				strcpy(reply_buf->arguments.fp_table1, table_name);
			}

			/* Set ids */
			reply_buf->id = oal_htonl(pfe_log_if_get_id(log_if));
			reply_buf->parent_id = oal_htonl(pfe_phy_if_get_id(pfe_log_if_get_parent(log_if)));

			*reply_len = sizeof(fpp_log_if_cmd_t);
			*fci_ret = FPP_ERR_OK;
			break;
		}
	}

	return ret;
}

/**
 * @brief			Process FPP_CMD_PHY_IF commands
 * @param[in]		msg FCI message containing the FPP_CMD_PHY_IF command
 * @param[out]		fci_ret FCI command return value
 * @param[out]		reply_buf Pointer to a buffer where function will construct command reply (fpp_phy_if_cmd_t)
 * @param[in,out]	reply_len Maximum reply buffer size on input, real reply size on output (in bytes)
 * @return			EOK if success, error code otherwise
 * @note			Function is only called within the FCI worker thread context.
 * @note			Must run with interface DB session lock.
 */
errno_t fci_interfaces_phy_cmd(fci_msg_t *msg, uint16_t *fci_ret, fpp_phy_if_cmd_t *reply_buf, uint32_t *reply_len)
{
	fci_t *context = (fci_t *)&__context;
	fpp_phy_if_cmd_t *if_cmd;
	errno_t ret = EOK;
	pfe_if_db_entry_t *entry = NULL;
	pfe_if_db_entry_t *entry2 = NULL;
	pfe_phy_if_t *phy_if = NULL;
	pfe_ct_block_state_t block_state;
	pfe_phy_if_t *mirror_if = NULL;
	pfe_ct_phy_if_id_t mirror_if_id = PFE_PHY_IF_ID_INVALID;
	pfe_ct_phy_if_stats_t stats = {0};
	uint32_t addr;
	char_t *name;

#if defined(PFE_CFG_NULL_ARG_CHECK)
	if (unlikely((NULL == msg) || (NULL == fci_ret) || (NULL == reply_buf) || (NULL == reply_len)))
	{
		NXP_LOG_ERROR("NULL argument received\n");
		return EINVAL;
	}

	if (unlikely(FALSE == context->fci_initialized))
	{
		NXP_LOG_ERROR("Context not initialized\n");
		return EPERM;
	}
#endif /* PFE_CFG_NULL_ARG_CHECK */

	if (*reply_len < sizeof(fpp_phy_if_cmd_t))
	{
		NXP_LOG_ERROR("Buffer length does not match expected value (fpp_if_cmd_t)\n");
		return EINVAL;
	}
	else
	{
		/*	No data written to reply buffer (yet) */
		*reply_len = 0U;
	}

	/*	Initialize the reply buffer */
	memset(reply_buf, 0, sizeof(fpp_phy_if_cmd_t));

	if_cmd = (fpp_phy_if_cmd_t *)msg->msg_cmd.payload;

	switch (if_cmd->action)
	{
		case FPP_ACTION_UPDATE:
		{
			/* Get the requested interface */
			ret = pfe_if_db_get_first(context->phy_if_db, context->if_session_id, IF_DB_CRIT_BY_NAME, if_cmd->name, &entry);

			if(EOK != ret)
			{
				NXP_LOG_ERROR("Incorrect session ID detected\n");
				*fci_ret = FPP_ERR_IF_WRONG_SESSION_ID;
				break;
			}

			/* Check if entry is not NULL and get logical interface */
			if(NULL != entry)
			{
				phy_if = pfe_if_db_entry_get_phy_if(entry);
			}

			/* Check if the entry exits*/
			if((NULL == entry) || (NULL == phy_if))
			{
				/* Interface doesn't exist or couldn't be extracted from the entry */
				*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				break;
			}

			/*	Set the interface block state - use the fact the enumerations 
				have same values */
			ret = pfe_phy_if_set_block_state(phy_if, (pfe_ct_block_state_t)if_cmd->block_state);
			if(EOK != ret)
			{
				*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
				break;
			}

			/* Set the interface mode */
			ret = pfe_phy_if_set_op_mode(phy_if, if_cmd->mode);
			if(EOK != ret)
			{
				*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
				break;
			}

			if(FPP_IF_MIRROR == (oal_ntohl(if_cmd->flags) & FPP_IF_MIRROR))
			{
				/* Get the requested interface */
				ret = pfe_if_db_get_first(context->phy_if_db, context->if_session_id, IF_DB_CRIT_BY_NAME, if_cmd->mirror, &entry2);

				if(EOK != ret)
				{
					NXP_LOG_ERROR("Failed to obtain interface \"%s\"in the database\n", if_cmd->mirror);
					*fci_ret = FPP_ERR_IF_WRONG_SESSION_ID;
					break;
				}

				/* Check if entry is not NULL and get logical interface */
				if(NULL != entry2)
				{
					mirror_if = pfe_if_db_entry_get_phy_if(entry2);
				}

				/* Check if the entry exits*/
				if((NULL == mirror_if) || (NULL == phy_if))
				{
					NXP_LOG_DEBUG("Interface doesn't exist or couldn't be extracted from the entry\n");
					*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
					break;
				}

				if (EOK != pfe_phy_if_set_mirroring(phy_if, pfe_phy_if_get_id(mirror_if)))
				{
					NXP_LOG_DEBUG("Unable to enable mirroring on %s\n", pfe_phy_if_get_name(phy_if));
					*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
				}
			}
			else
			{
				if (EOK != pfe_phy_if_set_mirroring(phy_if, PFE_PHY_IF_ID_INVALID))
				{
					NXP_LOG_DEBUG("Unable to disable mirroring on %s\n", pfe_phy_if_get_name(phy_if));
					*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
				}
			}

			/*	Enable/Disable */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_ENABLED))
			{
				ret = pfe_phy_if_enable(phy_if);
			}
			else
			{
				ret = pfe_phy_if_disable(phy_if);
			}

			if(EOK != ret)
			{
				NXP_LOG_ERROR("ENABLE flag wasn't updated correctly on %s\n",  pfe_phy_if_get_name(phy_if));
				*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
			}

			/* promisc */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_PROMISC))
			{
				ret = pfe_phy_if_promisc_enable(phy_if);
			}
			else
			{
				ret = pfe_phy_if_promisc_disable(phy_if);
			}

			/*	VLAN conformance check */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_VLAN_CONF_CHECK))
			{
				ret = pfe_phy_if_set_flag(phy_if, IF_FL_VLAN_CONF_CHECK);
			}
			else
			{
				ret = pfe_phy_if_clear_flag(phy_if, IF_FL_VLAN_CONF_CHECK);
			}

			/*	PTP conformance check */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_PTP_CONF_CHECK))
			{
				ret = pfe_phy_if_set_flag(phy_if, IF_FL_PTP_CONF_CHECK);
			}
			else
			{
				ret = pfe_phy_if_clear_flag(phy_if, IF_FL_PTP_CONF_CHECK);
			}

			/*	PTP promiscuous mode */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_PTP_PROMISC))
			{
				ret = pfe_phy_if_set_flag(phy_if, IF_FL_PTP_PROMISC);
			}
			else
			{
				ret = pfe_phy_if_clear_flag(phy_if, IF_FL_PTP_PROMISC);
			}

			/*	QinQ support control */
			if(0U != (oal_ntohl(if_cmd->flags) & FPP_IF_ALLOW_Q_IN_Q))
			{
				ret = pfe_phy_if_set_flag(phy_if, IF_FL_ALLOW_Q_IN_Q);
			}
			else
			{
				ret = pfe_phy_if_clear_flag(phy_if, IF_FL_ALLOW_Q_IN_Q);
			}

			if(EOK != ret)
			{
				NXP_LOG_ERROR("PROMISC flag wasn't updated correctly on %s\n",  pfe_phy_if_get_name(phy_if));
				*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
			}

			/*	Flexible Filter */
			if (0U != strlen((char_t *)if_cmd->ftable))
			{
				/*	Validate table */
				if (NULL == fci_fp_db_get_first(FP_TABLE_CRIT_NAME, (void *)if_cmd->ftable))
				{
					/*	Table not found */
					NXP_LOG_ERROR("%s: FP table %s not found\n",
							pfe_phy_if_get_name(phy_if), if_cmd->ftable);
				}
				else
				{
					/*	If not already done, write the table to HW */
					addr = fci_fp_db_get_table_dmem_addr((char_t *)if_cmd->ftable);
					if (0U == addr)
					{
						ret = fci_fp_db_push_table_to_hw(context->class, (char_t *)if_cmd->ftable);
						addr = fci_fp_db_get_table_dmem_addr((char_t *)if_cmd->ftable);
					}

					/*	Assign the table to the physical interface */
					/*	TODO: Temporary way. Pass table instance or name but not the DMEM address :( */
					ret = pfe_phy_if_set_ftable(phy_if, addr);
					if (EOK != ret)
					{
						NXP_LOG_ERROR("%s: Could not set filter table: %d\n", pfe_phy_if_get_name(phy_if), ret);
						*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
					}
				}
			}
			else
			{
				/*	Disable the filter. Get table entry from DB first. */
				addr = pfe_phy_if_get_ftable(phy_if);
				if (EOK == fci_fp_db_get_table_from_addr(addr, &name))
				{
					/* Delete the table from DMEM - no longer in use, copy is in database */
					(void)fci_fp_db_pop_table_from_hw(name);
				}

				/*	Assign NULL-table to the physical interface */
				/*	TODO: Temporary way. Pass table instance or name but not the DMEM address :( */
				ret = pfe_phy_if_set_ftable(phy_if, 0U);
				if (EOK != ret)
				{
					NXP_LOG_ERROR("%s: Could not set filter table: %d\n", pfe_phy_if_get_name(phy_if), ret);
					*fci_ret = FPP_ERR_IF_OP_UPDATE_FAILED;
				}
			}

			break;
		}

		case FPP_ACTION_QUERY:
		{
			ret = pfe_if_db_get_first(context->phy_if_db, context->if_session_id, IF_DB_CRIT_ALL, NULL, &entry);

			if(EOK != ret)
			{
				NXP_LOG_ERROR("Incorrect session ID detected\n");
				*fci_ret = FPP_ERR_IF_WRONG_SESSION_ID;
				break;
			}

			if (NULL == entry)
			{
				ret = EOK;
				*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				break;
			}
		}
		/* FALLTHRU */

		case FPP_ACTION_QUERY_CONT:
		{
			if (NULL == entry)
			{
				ret = pfe_if_db_get_next(context->phy_if_db, context->if_session_id, &entry);
				if(EOK != ret)
				{
					ret = EOK;
					*fci_ret = FPP_ERR_IF_WRONG_SESSION_ID;
					break;
				}

				if (NULL == entry)
				{
					ret = EOK;
					*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
					break;
				}
			}

			phy_if = pfe_if_db_entry_get_phy_if(entry);
			if (NULL == phy_if)
			{
				NXP_LOG_DEBUG("Was not possible to resolve DB entry to phy_if");
				*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				break;
			}

			if(EOK != (ret = pfe_phy_if_get_stats(phy_if, &stats)))
			{
				NXP_LOG_ERROR("Could not get interface statistics\n");
				break;
			}
			/* Copy the phy if statistics to reply */
			memcpy(&reply_buf->stats, &stats, sizeof(reply_buf->stats));

			/* Store phy_if name and MAC */
			strncpy(reply_buf->name, pfe_phy_if_get_name(phy_if), IFNAMSIZ-1);
			if (EOK != pfe_phy_if_get_mac_addr(phy_if, reply_buf->mac_addr))
			{
				NXP_LOG_DEBUG("Could not get interface MAC address\n");
			}

			/* Store phy_if id */
			reply_buf->id = oal_htonl(pfe_phy_if_get_id(phy_if));

			reply_buf->flags |= (TRUE == pfe_phy_if_is_promisc(phy_if)) ? oal_htonl(FPP_IF_PROMISC) : 0;
			reply_buf->flags |= (TRUE == pfe_phy_if_is_enabled(phy_if)) ? oal_htonl(FPP_IF_ENABLED) : 0;
			reply_buf->flags |= (IF_FL_NONE != pfe_phy_if_get_flag(phy_if, IF_FL_VLAN_CONF_CHECK)) ? oal_htonl(FPP_IF_VLAN_CONF_CHECK) : 0;
			reply_buf->flags |= (IF_FL_NONE != pfe_phy_if_get_flag(phy_if, IF_FL_PTP_CONF_CHECK)) ? oal_htonl(FPP_IF_PTP_CONF_CHECK) : 0;
			reply_buf->flags |= (IF_FL_NONE != pfe_phy_if_get_flag(phy_if, IF_FL_PTP_PROMISC)) ? oal_htonl(FPP_IF_PTP_PROMISC) : 0;
			reply_buf->flags |= (IF_FL_NONE != pfe_phy_if_get_flag(phy_if, IF_FL_ALLOW_Q_IN_Q)) ? oal_htonl(FPP_IF_ALLOW_Q_IN_Q) : 0;

			/* Get the mode - use the fact enums have same values */
			reply_buf->mode = (fpp_phy_if_op_mode_t) pfe_phy_if_get_op_mode(phy_if);
			pfe_phy_if_get_mac_addr(phy_if, reply_buf->mac_addr);

			/* Get the block state */
			(void)pfe_phy_if_get_block_state(phy_if, &block_state);
			/* Use the fact that the enums have same values */
			reply_buf->block_state = (fpp_phy_if_block_state_t)block_state; 
			mirror_if_id = pfe_phy_if_get_mirroring(phy_if);
			if(PFE_PHY_IF_ID_INVALID != mirror_if_id)
			{
				ret = pfe_if_db_get_single(context->phy_if_db, context->if_session_id, IF_DB_CRIT_BY_ID, (void *)(addr_t)mirror_if_id, &entry);
				if(EOK != ret)
				{
					NXP_LOG_ERROR("Failed to get interface with ID %u from database\n", mirror_if_id);
				}
				if (NULL != entry)
				{
					mirror_if = pfe_if_db_entry_get_phy_if(entry);
					reply_buf->flags |= oal_htonl(FPP_IF_MIRROR);
					strncpy(reply_buf->mirror, pfe_phy_if_get_name(mirror_if), IFNAMSIZ-1);
				}
				else
				{
					NXP_LOG_ERROR("Failed to obtain interface for ID %u\n", mirror_if_id);

					reply_buf->flags |= oal_htonl(FPP_IF_MIRROR);
					/* Fallback solution - we cannot query for the mirror interface because it
					   would cancel the outgoing query for physical interfaces */
					snprintf(reply_buf->mirror, IFNAMSIZ-1, "IF ID: %u", mirror_if_id);
				}
			}
			else
			{
				reply_buf->flags &= oal_htonl(~FPP_IF_MIRROR);
			}

			/*	Get filter info */
			addr = pfe_phy_if_get_ftable(phy_if);
			if (0U != addr)
			{
				ret = fci_fp_db_get_table_from_addr(addr, &name);
				if (EOK == ret)
				{
					strncpy(reply_buf->ftable, name, sizeof(reply_buf->ftable));
				}
				else
				{
					NXP_LOG_ERROR("Can't get table name from DB: %d\n", ret);
					*fci_ret = FPP_ERR_IF_ENTRY_NOT_FOUND;
				}
			}
			else
			{
				memset(reply_buf->ftable, 0, sizeof(reply_buf->ftable));
			}

			/* Set reply length end return OK */
			*reply_len = sizeof(fpp_phy_if_cmd_t);
			*fci_ret = FPP_ERR_OK;
			ret = EOK;
			break;
		}

		default:
		{
			NXP_LOG_ERROR("Interface Command: Unknown action received: 0x%x\n", if_cmd->action);
			*fci_ret = FPP_ERR_UNKNOWN_ACTION;
			break;
		}
	}

	return ret;
}
