/* =========================================================================
 *  Copyright 2019-2021 NXP
 *
 *  SPDX-License-Identifier: GPL-2.0
 *
 * ========================================================================= */
#include "pfe_cfg.h"
#include "oal.h"
#include "linked_list.h"
#include "pfe_ct.h"

#include "fci_fp_db.h"
#include "pfe_fp.h"
#include "fci.h"


typedef struct fci_fp_table_tag fci_fp_table_t;
/**
* @brief Flexible parser rule representation
*/
typedef struct
{
    /* Maintenance */
    char_t *name;            /* Unique ID */
    LLIST_t db_entry;        /* Global database link */
    LLIST_t table_entry;     /* Table link (rule can be part of a single table only) */
    uint8_t position;        /* Position in the table */
    fci_fp_table_t *table;   /* Link to the table the rule belongs */
    /* Data */
    char_t *next_rule;       /* Name of the next linked rule */
    uint32_t data;           /* Data to match */
    uint32_t mask;           /* Mask to match */
    uint16_t offset;         /* Data offset to get data */
    pfe_ct_fp_flags_t flags; /* Flags configuring the rule */
} fci_fp_rule_t;

/**
* @brief Criterion for rule database search
*/
typedef enum
{
    FP_RULE_CRIT_ALL,
    FP_RULE_CRIT_NAME,
} fci_fp_rule_criterion_t;

/**
* @brief Argument (requested value) for rule database
*/
typedef union
{
    char_t *name;
} fci_fp_rule_criterion_arg_t;

/**
* @brief Database of flexible parser rules
*/
typedef struct
{
    /* Rules database */
    LLIST_t rules;
    /* Searching */
    fci_fp_rule_criterion_t cur_crit;
    fci_fp_rule_criterion_arg_t cur_crit_arg;
    LLIST_t *cur_item;
} fci_fp_rule_db_t;

/**
* @brief Flexible parser table representation
*/
struct fci_fp_table_tag
{
    char_t *name;              /* Table identifier */
    uint8_t rule_count;        /* Number of rules in the table */
    uint32_t dmem_addr;        /* Address where the table was written into DMEM */
    pfe_class_t *class;
    LLIST_t db_entry;          /* Global database link */
    fci_fp_rule_db_t rules_db; /* Database of rules in the table */
};

/**
* @brief Database of flexible parser tables
*/
typedef struct
{
    /* Tables database */
    LLIST_t tables;
    /* Searching */
    fci_fp_table_criterion_t cur_crit;
    fci_fp_table_criterion_arg_t cur_crit_arg;
    LLIST_t *cur_item;
} fci_fp_table_db_t;

/*
* @brief Selects type of a database
*/
typedef enum
{
    COMMON,
    TABLE
} dbase_t;

static fci_fp_rule_db_t fci_fp_rule_db;
static fci_fp_table_db_t fci_fp_table_db;

static bool_t fci_fp_match_rule_by_criterion(fci_fp_rule_criterion_t crit, fci_fp_rule_criterion_arg_t *arg, fci_fp_rule_t *rule);
static fci_fp_rule_t *fci_fp_rule_get_first(fci_fp_rule_db_t *db, fci_fp_rule_criterion_t crit, void *arg, dbase_t dbase);
static fci_fp_rule_t *fci_fp_rule_get_next(fci_fp_rule_db_t *db, dbase_t dbase);
static bool_t fci_fp_match_table_by_criterion(fci_fp_table_criterion_t crit, fci_fp_table_criterion_arg_t *arg, fci_fp_table_t *table);
static fci_fp_table_t *fci_fp_table_get_first(fci_fp_table_db_t *db, fci_fp_table_criterion_t crit, void *arg);
#if 0
static fci_fp_table_t *fci_fp_table_get_next(fci_fp_table_db_t *db);
#endif
/**
 * @brief        Match rule using given criterion
 * @param[in]    crit Selects criterion
 * @param[in]    arg Criterion argument
 * @param[in]    rule The rule to be matched
 * @retval       TRUE Rule matches the criterion
 * @retval       FALSE Rule does not match the criterion
 */
static bool_t fci_fp_match_rule_by_criterion(fci_fp_rule_criterion_t crit, fci_fp_rule_criterion_arg_t *arg, fci_fp_rule_t *rule)
{
    bool_t match;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely((NULL == rule) || (NULL == arg)))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */
    switch (crit)
    {
        case FP_RULE_CRIT_ALL:
        {
            match = TRUE;
            break;
        }
        case FP_RULE_CRIT_NAME:
        {
            if(0 == strcmp(arg->name, rule->name))
            {
                match = TRUE;
            }
            else
            {
                match = FALSE;
            }
            break;
        }
        default:
        {
            NXP_LOG_ERROR("Unknown criterion\n");
            match = FALSE;
        }
    }
    return match;
}
/**
 * @brief        Get first rule from the database matching given criterion
 * @details      Intended to be used with fci_fp_rule_get_next
 * @param[in]    db The rules database instance
 * @param[in]    crit Get criterion
 * @param[in]    arg Pointer to criterion argument. Every value shall to be in HOST endian format. Strings are copied into internal memory.
 * @param[in]    dbase Type of the database being used (COMMON or TABLE).
 * @return       The matching rule or NULL if not found
 */
static fci_fp_rule_t *fci_fp_rule_get_first(fci_fp_rule_db_t *db, fci_fp_rule_criterion_t crit, void *arg, dbase_t dbase)
{
    LLIST_t *item;
    fci_fp_rule_t *rule;
    bool_t match = FALSE;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely((NULL == db) || (NULL == arg)))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return NULL;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */
    /* Free memory allocated by previous search (if any) */
    if(FP_RULE_CRIT_NAME == db->cur_crit)
    {
        if(NULL != db->cur_crit_arg.name)
        {
            oal_mm_free(db->cur_crit_arg.name);
            db->cur_crit_arg.name = NULL;
        }
    }
    /*    Remember criterion and argument for possible subsequent fci_fp_rule_get_next() calls */
    db->cur_crit = crit;
    switch(db->cur_crit)
    {
        case FP_RULE_CRIT_ALL:
        {
            break;
        }
        case FP_RULE_CRIT_NAME:
        {
            char_t *mem;
            /* Allocate string memory */
            mem = oal_mm_malloc(strlen((char_t *)arg) + 1U);
            if(NULL == mem)
            {
                return NULL;
            }
            db->cur_crit_arg.name = mem;
            /* Copy the string */
            strcpy(mem, (char_t *)arg);
            break;
        }
        default:
        {
            NXP_LOG_ERROR("Unknown criterion\n");
            return NULL;
        }
    }

    /*    Search for first matching rule */
    if (FALSE == LLIST_IsEmpty(&db->rules))
    {
        /*    Get first matching rule */
        LLIST_ForEach(item, &db->rules)
        {
            /*    Get data */
            if(COMMON == dbase)
            {
                rule = LLIST_Data(item, fci_fp_rule_t, db_entry);
            }
            else
            {
                rule = LLIST_Data(item, fci_fp_rule_t, table_entry);
            }

            /*    Remember current item to know where to start later */
            db->cur_item = item->prNext;
            if (NULL != rule)
            {
                if (TRUE == fci_fp_match_rule_by_criterion(db->cur_crit, &db->cur_crit_arg, rule))
                {
                    match = TRUE;
                    break;
                }
            }
        }
    }

    if (TRUE == match)
    {
        return rule;
    }
    else
    {
        return NULL;
    }
}
/**
 * @brief        Get next rule from the database
 * @details      Intended to be used with fci_fp_rule_get_first.
 * @param[in]    db The rules database instance
 * @param[in]    dbase Type of the database being used (COMMON or TABLE).
 * @return       The rule matching criterion set by fci_fp_rule_get_first or NULL if not found
 */
static fci_fp_rule_t *fci_fp_rule_get_next(fci_fp_rule_db_t *db, dbase_t dbase)
{
    fci_fp_rule_t *rule;
    bool_t match = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == db))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return NULL;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */
    if (db->cur_item == &db->rules)
    {
        /*    No more entries */
        rule = NULL;
    }
    else
    {
        while (db->cur_item != &db->rules)
        {
            /*    Get data */
            if(COMMON == dbase)
            {
                rule = LLIST_Data(db->cur_item, fci_fp_rule_t, db_entry);
            }
            else
            {
                rule = LLIST_Data(db->cur_item, fci_fp_rule_t, table_entry);
            }

            /*    Remember current item to know where to start later */
            db->cur_item = db->cur_item->prNext;

            if (NULL != rule)
            {
                if (TRUE == fci_fp_match_rule_by_criterion(db->cur_crit, &db->cur_crit_arg, rule))
                {
                    match = TRUE;
                    break;
                }
            }
        }
    }

    if (TRUE == match)
    {
        return rule;
    }
    else
    {
        return NULL;
    }
}
/**
 * @brief        Match table using given criterion
 * @param[in]    crit Selects criterion
 * @param[in]    arg Criterion argument
 * @param[in]    table The table to be matched
 * @retval       TRUE Table matches the criterion
 * @retval       FALSE Table does not match the criterion
 */
static bool_t fci_fp_match_table_by_criterion(fci_fp_table_criterion_t crit, fci_fp_table_criterion_arg_t *arg, fci_fp_table_t *table)
{
    bool_t match;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely((NULL == table) || (NULL == arg)))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */
    switch (crit)
    {
        case FP_TABLE_CRIT_ALL:
        {
            match = TRUE;
            break;
        }
        case FP_TABLE_CRIT_NAME:
        {
            if(0 == strcmp(arg->name, table->name))
            {
                match = TRUE;
            }
            else
            {
                match = FALSE;
            }
            break;
        }
        case FP_TABLE_CRIT_ADDRESS:
        {
            if(arg->address == table->dmem_addr)
            {
                match = TRUE;
            }
            else
            {
                match = FALSE;
            }
            break;
        }
        default:
        {
            NXP_LOG_ERROR("Unknown criterion\n");
            match = FALSE;
        }
    }
    return match;
}
/**
 * @brief        Get first table from the database matching given criterion
 * @details      Intended to be used with fci_fp_table_get_next
 * @param[in]    db The tables database instance
 * @param[in]    crit Get criterion
 * @param[in]    arg Pointer to criterion argument. Every value shall to be in HOST endian format. Strings are copied into internal memory.
 * @return       The matching table or NULL if not found
 */
static fci_fp_table_t *fci_fp_table_get_first(fci_fp_table_db_t *db, fci_fp_table_criterion_t crit, void *arg)
{
    LLIST_t *item;
    fci_fp_table_t *table;
    bool_t match = FALSE;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely((NULL == db) || (NULL == arg)))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return NULL;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */
    /* Free memory allocated by previous search (if any) */
    if(FP_TABLE_CRIT_NAME == db->cur_crit)
    {
        if(NULL != db->cur_crit_arg.name)
        {
            oal_mm_free(db->cur_crit_arg.name);
            db->cur_crit_arg.name = NULL;
        }
    }
    /*    Remember criterion and argument for possible subsequent fci_fp_table_get_next() calls */
    db->cur_crit = crit;
    switch(db->cur_crit)
    {
        case FP_TABLE_CRIT_ALL:
        {
            break;
        }
        case FP_TABLE_CRIT_NAME:
        {
            char_t *mem;
            /* Allocate string memory */
            mem = oal_mm_malloc(strlen((char_t *)arg) + 1U);
            if(NULL == mem)
            {
                return NULL;
            }
            db->cur_crit_arg.name = mem;
            /* Copy the string */
            strcpy(mem, (char_t *)arg);
            break;
        }
        case FP_TABLE_CRIT_ADDRESS:
        {
            db->cur_crit_arg.address = *(uint32_t *)arg;
            break;
        }
        default:
        {
            NXP_LOG_ERROR("Unknown criterion\n");
            return NULL;
        }
    }

    /*    Search for first matching table */
    if (FALSE == LLIST_IsEmpty(&db->tables))
    {
        /*    Get first matching table */
        LLIST_ForEach(item, &db->tables)
        {
            /*    Get data */
            table = LLIST_Data(item, fci_fp_table_t, db_entry);

            /*    Remember current item to know where to start later */
            db->cur_item = item->prNext;
            if (NULL != table)
            {
                if (TRUE == fci_fp_match_table_by_criterion(db->cur_crit, &db->cur_crit_arg, table))
                {
                    match = TRUE;
                    break;
                }
            }
        }
    }

    if (TRUE == match)
    {
        return table;
    }
    else
    {
        return NULL;
    }
}

#if 0 /* Function prepared for future */
/**
 * @brief        Get next table from the database
 * @details      Intended to be used with fci_fp_rule_get_first.
 * @param[in]    db The rules database instance
 * @return       The table matching criterion set by fci_fp_rule_get_first or NULL if not found
 */
static fci_fp_table_t *fci_fp_table_get_next(fci_fp_table_db_t *db)
{
    fci_fp_table_t *table;
    bool_t match = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == db))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return NULL;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */
    if (db->cur_item == &db->tables)
    {
        /*    No more entries */
        table = NULL;
    }
    else
    {
        while (db->cur_item != &db->tables)
        {
            /*    Get data */
            table = LLIST_Data(db->cur_item, fci_fp_table_t, db_entry);

            /*    Remember current item to know where to start later */
            db->cur_item = db->cur_item->prNext;

            if (NULL != table)
            {
                if (TRUE == fci_fp_match_table_by_criterion(db->cur_crit, &db->cur_crit_arg, table))
                {
                    match = TRUE;
                    break;
                }
            }
        }
    }

    if (TRUE == match)
    {
        return table;
    }
    else
    {
        return NULL;
    }
}
#endif
/**
* @brief Returns the position of the rule within a table
* @param[in] table Table to determine postion of the rule within
* @param[in] rule Rule which position within the table shall be determined
* @param[out] pos Determined rule position
* @return EOK on success, ENOENT if rule is not part of the table.
*/
static errno_t fci_fp_get_rule_pos_in_table(fci_fp_table_t *table, fci_fp_rule_t *rule, uint8_t *pos)
{
    uint8_t i = 0U;
    fci_fp_rule_t *rule_item;
    LLIST_t *item;

    LLIST_ForEach(item, &table->rules_db.rules)
    {
        rule_item = LLIST_Data(item, fci_fp_rule_t, table_entry);
        if(rule_item == rule)
        {
            *pos = i;
            return EOK;
        }
        i++;
    }
    return ENOENT;
}

/**
* @brief Initializes the module
*/
void fci_fp_db_init(void)
{
    memset(&fci_fp_rule_db, 0U, sizeof(fci_fp_rule_db_t));
    memset(&fci_fp_table_db, 0U, sizeof(fci_fp_table_db_t));
    LLIST_Init(&fci_fp_table_db.tables);
    LLIST_Init(&fci_fp_rule_db.rules);

}

/**
* @brief Crates a flexible parser rule
* @param[in] name Name of the rule (unique identifier)
* @param[in] data Expected value of the data (network endian)
* @param[in] mask Mask to be applied on the data (network endian)
* @param[in] offset Offset of the data to be compared (network endian)
* @param[in] flags Flags describing the rule - see pfe_ct_fp_flags_t
* @param[in] next_rule Name of the rule to be examined next if none of flags FP_FL_ACCEPT | FP_FL_REJECT is set
* @return Either EOK or an error code.
*/
errno_t fci_fp_db_create_rule(char_t *name, uint32_t data, uint32_t mask, uint16_t offset, pfe_ct_fp_flags_t flags, char_t *next_rule)
{

    uint32_t mem_size;
    fci_fp_rule_t *rule = NULL;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if(NULL == name)
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    if((0U == (flags & (FP_FL_ACCEPT | FP_FL_REJECT))) && (NULL == next_rule))
    {   /* If flags are not FP_FL_REJECT and not FP_FL_ACCEPT we need the next rule name */
        NXP_LOG_ERROR("Flags FP_FL_ACCEPT and FP_FL_REJECT are not set but next rule is not defined (NULL)\n");
        return EINVAL;
    }
    if((FP_FL_ACCEPT | FP_FL_REJECT) == (flags & (FP_FL_ACCEPT | FP_FL_REJECT)))
    {   /* Cannot do both Accept and Reject action */
        NXP_LOG_ERROR("Both flags FP_FL_ACCEPT and FP_FL_REJECT are set\n");
        return EINVAL;
    }
    if((0U != (flags & (FP_FL_ACCEPT | FP_FL_REJECT))) && (NULL != next_rule))
    {   /* Ignored argument */
        NXP_LOG_WARNING("Next rule is ignored with these flags: 0x%x\n", flags);
        next_rule = NULL; /* Avoid memory allocation and value storage to avoid future problems */
    }
    /* Check that the name is unique in our database */
    if(NULL != fci_fp_rule_get_first(&fci_fp_rule_db, FP_RULE_CRIT_NAME, name, COMMON))
    {   /* Rule with same name found in database */
        NXP_LOG_ERROR("Rule with name \"%s\" already exists\n", name);
        return EEXIST;
    }

    /* Calculate needed memory size */
    mem_size = sizeof(fci_fp_rule_t) + strlen(name) + 1U; /* Structure + name string */
    if(NULL != next_rule)
    {
        mem_size += strlen(next_rule) + 1U; /* Add next rule name string space */
    }
    /* Allocate memory for the rule */
    rule = oal_mm_malloc(mem_size);
    if(NULL == rule)
    {   /* Failed */
        NXP_LOG_ERROR("No memory for rule\n");
        return ENOMEM;
    }
    else
    {
        /* Initialize */
        memset(rule, 0, mem_size);
        LLIST_Init(&rule->db_entry);
        LLIST_Init(&rule->table_entry);
        /* Store the input parameters */
        rule->name = (char_t *)&rule[1];
        strcpy(rule->name, name);
        rule->data = data;
        rule->mask = mask;
        rule->offset = offset;
        if(NULL != next_rule)
        {   /* Just store the next rule name, no validation yet because rule may be added later */
            rule->next_rule = rule->name + strlen(name) + 1U;
            strcpy(rule->next_rule, next_rule);
        }
        else
        {
        rule->next_rule = NULL;
        }
        rule->flags = flags;
        /* Add the rule into the global database */
        LLIST_AddAtEnd(&rule->db_entry, &fci_fp_rule_db.rules);
    }
    return EOK;
}

/**
* @brief Destroys a flexible parser rule
* @param[in] name Name of the table to destroy
* @return EOK or an error code.
*/
errno_t fci_fp_db_destroy_rule(char_t *name)
{
  fci_fp_rule_t *rule = NULL;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if(NULL == name)
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    /* Find the rule */
    rule = fci_fp_rule_get_first(&fci_fp_rule_db, FP_RULE_CRIT_NAME, name, COMMON);
    if(NULL == rule)
    {   /* No such rule */
        NXP_LOG_ERROR("Rule with name \"%s\" does not exist\n", name);
        return ENOENT;
    }
    /* Check that the rule is not in use */
    if(NULL != rule->table)
    {   /* Still in use */
        NXP_LOG_ERROR("Rule \"%s\" is in use in table \"%s\"\n", name, rule->table->name);
        return EACCES;
    }
    /* Remove rule from the database */
    LLIST_Remove(&rule->db_entry);
    /* Free the memory */
    oal_mm_free(rule);
    return EOK;
}

/**
* @brief Creates a flexible parser rules table
* @param[in] name Name of the table - unique identifier
* @return EOK or an error code.
*/
errno_t fci_fp_db_create_table(char_t *name)
{
    uint32_t mem_size;
    fci_fp_table_t *table;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if(NULL == name)
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    /* Check that the name is unique in our database */
    if(NULL != fci_fp_table_get_first(&fci_fp_table_db, FP_TABLE_CRIT_NAME, name))
    {   /* Rule with same name found in database */
        NXP_LOG_ERROR("Table with name \"%s\" already exists\n", name);
        return EEXIST;
    }

    /* Allocate memory for the table */
    mem_size = sizeof(fci_fp_table_t) + strlen(name) + 1U;
    table = oal_mm_malloc(mem_size);
    if(NULL == table)
    {
        NXP_LOG_ERROR("No memory for the table\n");
        return ENOMEM;
    }
    else
    {
        /* Initialize */
        memset(table, 0, mem_size);
        LLIST_Init(&table->db_entry);
        LLIST_Init(&table->rules_db.rules);
        /* Store the input parameters */
        table->name = (char_t *)&table[1];
        strcpy(table->name, name);
        /* Add the table into the global database */
        LLIST_AddAtEnd(&table->db_entry, &fci_fp_table_db.tables);
    }
    return EOK;

}

/**
* @brief Destroys a flexible parser rules table
* @param[in] name Name of the table to destroy
* @param[in] force If set to TRUE the table is destroyed even if it is still in use.
* @return EOK or an error code.
*/
errno_t fci_fp_db_destroy_table(char_t *name, bool_t force)
{
    fci_fp_table_t *table;
    fci_fp_rule_t *rule;
    LLIST_t *item, *aux;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if(NULL == name)
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    /* Find the table */
    table = fci_fp_table_get_first(&fci_fp_table_db, FP_TABLE_CRIT_NAME, name);
    if(NULL == table)
    {
        NXP_LOG_ERROR("Table with name \"%s\" does not exist\n", name);
        return ENOENT;
    }
    /* Check that the table is not in use */
    if(0U != table->dmem_addr)
    {   /* Table is still in use */
        if(FALSE == force)
        {   /* No override */
            NXP_LOG_ERROR("Table \"%s\" is in use\n", name);
            return EACCES;
        }
        else
        {   /* Override (and ride to hell) */
            NXP_LOG_WARNING("Table \"%s\" is in use\n", name);
            table->dmem_addr = 0U;
        }
    }

    /* Unlink all rules in the table if there are any */
    if(FALSE == LLIST_IsEmpty(&table->rules_db.rules))
    {
        LLIST_ForEachRemovable(item, aux, &table->rules_db.rules)
        {
            rule = LLIST_Data(item, fci_fp_rule_t, table_entry);
            LLIST_Remove(item);
            table->rule_count -= 1U;
            rule->table = NULL;
        }
    }
    /* Remove table from the database */
    LLIST_Remove(&table->db_entry);
    /* Free the memory */
    oal_mm_free(table);
    return EOK;
}


/**
* @brief Adds a rule into a table at given position
* @param[in] table_name Table where the rule shall be added
* @param[in] rule_name Rule which shall be added into a table.
* @param[in] position Position where to place rule. Either fci_fp_RULE_POSITION_LAST, fci_fp_RULE_POSITION_FIRST,
*            or an integer in range 0 to 255 describing the position.
* @note Single rule can belong to only one table.
* @return Either EOK or an error code.
*/
errno_t fci_fp_db_add_rule_to_table(char_t *table_name, char_t *rule_name, uint16_t position)
{
    fci_fp_table_t *table;
    fci_fp_rule_t *rule;
    LLIST_t *item;
    uint32_t i = 0U; /* Start search from position 0 */

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if((NULL == table_name)||(NULL == rule_name))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    /* Check that the rule does exist */
    rule = fci_fp_rule_get_first(&fci_fp_rule_db, FP_RULE_CRIT_NAME, rule_name, COMMON);
    if(NULL == rule)
    {
        NXP_LOG_ERROR("Rule \"%s\" does not exist\n", rule_name);
        return ENOENT;
    }
    /* Check that the rule does not belong to any other table */
    if(NULL != rule->table)
    {
        NXP_LOG_ERROR("Rule \"%s\" is already part of the table \"%s\"\n", rule_name, rule->table->name);
        return EACCES;
    }
    /* Check that the table does exist */
    table = fci_fp_table_get_first(&fci_fp_table_db, FP_TABLE_CRIT_NAME, table_name);
    if(NULL == table)
    {
        NXP_LOG_ERROR("Table \"%s\" does not exist\n", table_name);
        return ENOENT;
    }

    /* Add rule into the table */
    if(LLIST_IsEmpty(&table->rules_db.rules))
    {   /* Empty list - ignore position */
        if((position != FCI_FP_RULE_POSITION_FIRST) && (position != FCI_FP_RULE_POSITION_LAST))
        {
            NXP_LOG_WARNING("Adding into an empty table position %u ignored\n", position);
        }
        LLIST_AddAtBegin(&rule->table_entry, &table->rules_db.rules);
        rule->table = table;
        table->rule_count = 1U; /* 1st rule in table */
    }
    else
    {   /* Table not empty - need to handle position request */
        if(position == FCI_FP_RULE_POSITION_FIRST)
        {   /* Insert as the first one */
            LLIST_AddAtBegin(&rule->table_entry, &table->rules_db.rules);
            rule->table = table;
            table->rule_count += 1U;
        }
        else if(position >= FCI_FP_RULE_POSITION_LAST)
        {   /* Add as the last one */
            LLIST_AddAtEnd(&rule->table_entry, &table->rules_db.rules);
            rule->table = table;
            table->rule_count += 1U;
        }
        else if(position > FCI_FP_RULE_POSITION_FIRST)
        {   /* Insert at specified position */
            bool_t added = FALSE;
            LLIST_ForEach(item, &table->rules_db.rules)
            {
                if(position == i)
                {   /* This is the right position - put rule before the item */
                    LLIST_Insert(&rule->table_entry, item);
                    rule->table = table;
                    table->rule_count += 1U;
                    added = TRUE;
                    break;
                }
                i++;
            }
            if(FALSE == added)
            {   /* The requested position has not been found - add at the end */
                NXP_LOG_WARNING("Position %u does not exist, adding at %u\n", position, i);
                LLIST_AddAtEnd(&rule->table_entry, &table->rules_db.rules);
                rule->table = table;
                table->rule_count += 1U;
            }
        }
        else
        {   /* Invalid value */
            NXP_LOG_ERROR("Invalid value of position %u\n", position);
            return EINVAL;
        }
    }
    return EOK;
}

/**
* @brief Removes the rule from a table
* @param[in] rule_name Rule to be removed from the table
* @details Each rule knows which table it belongs therefore the table reference is not needed.
* @return EOK or error code.
*/
errno_t fci_fp_db_remove_rule_from_table(char_t *rule_name)
{
    fci_fp_rule_t *rule;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if((NULL == rule_name))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    /* Check that the rule does exist */
    rule = fci_fp_rule_get_first(&fci_fp_rule_db, FP_RULE_CRIT_NAME, rule_name, COMMON);
    if(NULL == rule)
    {
        NXP_LOG_ERROR("Rule \"%s\" does not exist\n", rule_name);
        return ENOENT;
    }
    /* Check that the rule is in a table */
    if(NULL != rule->table)
    {   /* Rule in a table - remove it */
        LLIST_Remove(&rule->table_entry);
        rule->table->rule_count--;
        rule->table = NULL;
    }
    else
    {   /* Rule not in a table */
        NXP_LOG_WARNING("Rule \"%s\" is not part of any table\n", rule_name);
    }
    return EOK;
}

/**
* @brief Returns table address in the DMEM
* @param[in] table Table instance which DMEM address shall be returned.
* @return DMEM address of the table or 0 if table has not been written into DMEM yet.
*/
uint32_t fci_fp_db_get_table_dmem_addr(char_t *table_name)
{
    fci_fp_table_t *table;
    uint32_t retval;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if(NULL == table_name)
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif

    table = fci_fp_table_get_first(&fci_fp_table_db, FP_TABLE_CRIT_NAME, table_name);
    if(NULL == table)
    {
        NXP_LOG_WARNING("Table \"%s\" not found\n", table_name);
        retval = 0U;
    }
    else
    {
        retval = table->dmem_addr;
    }
    return retval;
}

/**
* @brief Writes flexible parser table into DMEM of all PEs in given Classifier
* @param[in] classifier Classifier which DMEM shall be written
* @param[in] table Table which shall be written
* @details Function allocates the DMEM to write the table and writes the table into
*          this memory. Use the function fci_fp_db_get_table_dmem_addr to obtain the
*          table address.
* @return Either EOK or an error code.
*/
errno_t fci_fp_db_push_table_to_hw(pfe_class_t *class, char_t *table_name)
{
    fci_fp_table_t *table;
    fci_fp_rule_t *next_rule;
    pfe_ct_fp_rule_t rule_buf;
    LLIST_t *item;
    fci_fp_rule_t *rule;
    uint32_t i = 0U;
    uint8_t pos;


#if defined(PFE_CFG_NULL_ARG_CHECK)
    if((NULL == class)||(NULL == table_name))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    /* Get the table */
    table = fci_fp_table_get_first(&fci_fp_table_db, FP_TABLE_CRIT_NAME, table_name);
    if(NULL == table)
    {
        NXP_LOG_WARNING("Table \"%s\" not found\n", table_name);
        return ENOENT;
    }

    table->dmem_addr = pfe_fp_create_table(class, table->rule_count);
    table->class = class;
    if(0U == table->dmem_addr)
    {
        NXP_LOG_ERROR("Cannot write the table");
        return EFAULT;
    }

    /* Write rules into the table */
    LLIST_ForEach(item, &table->rules_db.rules)
    {
        rule = LLIST_Data(item, fci_fp_rule_t, table_entry);
        rule_buf.data = rule->data;
        rule_buf.mask = rule->mask;
        rule_buf.offset = rule->offset;
        rule_buf.flags = rule->flags;
        if(NULL != rule->next_rule)
        {   /* Next rule is specified */
            /* Convert next_rule name to position in the table */
            next_rule = fci_fp_rule_get_first(&table->rules_db, FP_RULE_CRIT_NAME, rule->next_rule, TABLE);
            if(NULL == next_rule)
            {   /* Failed - cannot proceed */
                NXP_LOG_ERROR("Referenced rule \"%s\" is not part of the table \"%s\"\n", rule->next_rule, table_name);
                pfe_fp_destroy_table(class, table->dmem_addr);
                table->dmem_addr = 0U;
                return ENOENT;
            }
            if(EOK != fci_fp_get_rule_pos_in_table(table, next_rule, &pos))
            {   /* Failed - cannot proceed */
                NXP_LOG_ERROR("Referenced rule \"%s\" is not part of the table \"%s\"\n", rule->next_rule, table_name);
                pfe_fp_destroy_table(class, table->dmem_addr);
                table->dmem_addr = 0U;
                return ENOENT;
            }
            rule_buf.next_idx = pos;
        }
        else
        {   /* Next rule is not used */
            rule_buf.next_idx = 0xFFU; /* If used it will cause FW internal check to detect it */
        }
        pfe_fp_table_write_rule(class, table->dmem_addr, &rule_buf, i);

        i++;
    }

    return EOK;
}

/**
* @brief Removes table from the DMEM in PEs when it is no longer in use
* @param[in] table_name Name of the table to be removed
* @warning Remove the table only if there are no references to it
* @details Removal of unused tables from the DMEM is needed to avoid depletion of the
*          DMEM memory pool.
* @return EOK or an error code.
*/
errno_t fci_fp_db_pop_table_from_hw(char_t *table_name)
{
    fci_fp_table_t *table;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if(NULL == table_name)
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    /* Get the table */
    table = fci_fp_table_get_first(&fci_fp_table_db, FP_TABLE_CRIT_NAME, table_name);
    if(NULL == table)
    {
        NXP_LOG_WARNING("Table \"%s\" not found\n", table_name);
        return ENOENT;
    }

    /* Free the DMEM */
    pfe_fp_destroy_table(table->class, table->dmem_addr);
    /* Clear the references to DMEM */
    table->dmem_addr = 0U;
    table->class = NULL;
    return EOK;
}

/**
* @brief Returns name of the table being written at given DMEM address
* @param[in] addr Address to find the table
* @param[out] table_name Returned table name
* @return EOK or an error code
*/
errno_t fci_fp_db_get_table_from_addr(uint32_t addr, char_t **table_name)
{
    fci_fp_table_t *table;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if(NULL == table_name)
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    if(0U == addr)
    {   /* 0 is not valid table address, used as no-address */
        return EINVAL;
    }
    
    table = fci_fp_table_get_first(&fci_fp_table_db, FP_TABLE_CRIT_ADDRESS, &addr);
    if(NULL == table)
    {
        NXP_LOG_WARNING("Table with address 0x%x not found\n", addr);
        return ENOENT;
    }
    *table_name = table->name;
    return EOK;
}

/**
 * @brief		Get first DB entry (table) matching the criterion
 * @param[in]	crit The criterion
 * @parma[in]	arg The criterion argument
 * @return		FP table instance or NULL if not found
 */
fci_fp_table_t *fci_fp_db_get_first(fci_fp_table_criterion_t crit, void *arg)
{
	return fci_fp_table_get_first(&fci_fp_table_db, crit, arg);
}

/**
* @brief Returns parameters of the first rule in the database
* @details Function is intended to start query of all rules in the database (by FCI).
* @param[out] rule_name Name of the rule
* @param[out] data Rule data
* @param[out] mask Rule mask
* @param[out] offset Rule offset
* @param[out] flags Rule flags
* @param[out] next_rule Name of the next rule (if any)
* @return EOK or an error code.
*/
errno_t fci_fp_db_get_first_rule(char_t **rule_name, uint32_t *data, uint32_t *mask, uint16_t *offset, pfe_ct_fp_flags_t *flags, char_t **next_rule)
{
    fci_fp_rule_t *rule;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if((NULL == rule_name) || (NULL == data) || (NULL == mask) || (NULL == offset) || (NULL == flags) || (NULL == next_rule))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif

    rule = fci_fp_rule_get_first(&fci_fp_rule_db, FP_RULE_CRIT_ALL, NULL, COMMON);
    if(NULL == rule)
    {
        return ENOENT;
    }
    else
    {
        *rule_name = rule->name;
        *data = rule->data;
        *mask = rule->mask;
        *offset = rule->offset;
        *flags = rule->flags;
        *next_rule = rule->next_rule;
        return EOK;
    }
}

/**
* @brief Returns parameters of the next rule in the database
* @details Function is intended to continue query of all rules in the database (by FCI).
* @param[out] rule_name Name of the rule
* @param[out] data Rule data
* @param[out] mask Rule mask
* @param[out] offset Rule offset
* @param[out] flags Rule flags
* @param[out] next_rule Name of the next rule (if any)
* @return EOK or an error code.
*/
errno_t fci_fp_db_get_next_rule(char_t **rule_name, uint32_t *data, uint32_t *mask, uint16_t *offset, pfe_ct_fp_flags_t *flags, char_t **next_rule)
{
    fci_fp_rule_t *rule;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if((NULL == rule_name) || (NULL == data) || (NULL == mask) || (NULL == offset) || (NULL == flags) || (NULL == next_rule))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif

    rule = fci_fp_rule_get_next(&fci_fp_rule_db, COMMON);
    if(NULL == rule)
    {
        return ENOENT;
    }
    else
    {
        *rule_name = rule->name;
        *data = rule->data;
        *mask = rule->mask;
        *offset = rule->offset;
        *flags = rule->flags;
        *next_rule = rule->next_rule;
        return EOK;
    }
}

/**
* @brief Returns parameters of the first rule in the table
* @details Function is intended to start query of all rules in the table (by FCI).
* @param[in]  table_name Name of the table to query
* @param[out] rule_name Name of the rule
* @param[out] data Rule data (network endian)
* @param[out] mask Rule mask (network endian)
* @param[out] offset Rule offset (network endian)
* @param[out] flags Rule flags
* @param[out] next_rule Name of the next rule (if any)
* @return EOK or an error code.
*/
errno_t fci_fp_db_get_table_first_rule(char_t *table_name, char_t **rule_name, uint32_t *data, uint32_t *mask, uint16_t *offset, pfe_ct_fp_flags_t *flags, char_t **next_rule)
{
    fci_fp_table_t *table;
    fci_fp_rule_t *rule;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if((NULL == table_name) || (NULL == rule_name) || (NULL == data) || (NULL == mask) || (NULL == offset) || (NULL == flags) || (NULL == next_rule))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    /* Get the table */
    table = fci_fp_table_get_first(&fci_fp_table_db, FP_TABLE_CRIT_NAME, table_name);
    if(NULL == table)
    {
        NXP_LOG_WARNING("Table \"%s\" not found\n", table_name);
        return ENOENT;
    }
    /* Get the first rule */
    rule = fci_fp_rule_get_first(&table->rules_db, FP_RULE_CRIT_ALL, NULL, TABLE);
    if(NULL == rule)
    {
        return ENOENT;
    }
    else
    {
        *rule_name = rule->name;
        *data = rule->data;
        *mask = rule->mask;
        *offset = rule->offset;
        *flags = rule->flags;
        *next_rule = rule->next_rule;
        return EOK;
    }
}

/**
* @brief Returns parameters of the next rule in the table
* @details Function is intended to start query of all rules in the table (by FCI).
* @param[in]  table_name Name of the table to query
* @param[out] rule_name Name of the rule
* @param[out] data Rule data
* @param[out] mask Rule mask
* @param[out] offset Rule offset
* @param[out] flags Rule flags
* @param[out] next_rule Name of the next rule (if any)
* @return EOK or an error code.
*/
errno_t fci_fp_db_get_table_next_rule(char_t *table_name, char_t **rule_name, uint32_t *data, uint32_t *mask, uint16_t *offset, pfe_ct_fp_flags_t *flags, char_t **next_rule)
{
    fci_fp_table_t *table;
    fci_fp_rule_t *rule;
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if((NULL == table_name) || (NULL == rule_name) || (NULL == data) || (NULL == mask) || (NULL == offset) || (NULL == flags) || (NULL == next_rule))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return EINVAL;
    }
#endif
    /* Get the table */
    table = fci_fp_table_get_first(&fci_fp_table_db, FP_TABLE_CRIT_NAME, table_name);
    if(NULL == table)
    {
        NXP_LOG_WARNING("Table \"%s\" not found\n", table_name);
        return ENOENT;
    }
    /* Get the rule */
    rule = fci_fp_rule_get_next(&table->rules_db, TABLE);
    if(NULL == rule)
    {
        return ENOENT;
    }
    else
    {
        *rule_name = rule->name;
        *data = rule->data;
        *mask = rule->mask;
        *offset = rule->offset;
        *flags = rule->flags;
        *next_rule = rule->next_rule;
        return EOK;
    }
}

/**
* @brief Prints a rule in a human readable form
* @param[in] rule Rule to be printed
* @param[in] buf Buffer where to put the output
* @param[in] len Size of the buffer
* @param[in] verb_level Verbosity level
* @return Number of characters written into the buffer
*/
static uint32_t fci_fp_print_rule(fci_fp_rule_t *rule, char_t *buf, uint32_t buf_len, uint8_t verb_level)
{
    uint32_t len = 0U;

    len += oal_util_snprintf(buf + len, buf_len - len, "%s = {", rule->name);
    /* Conditions */
    if(FP_FL_INVERT == (rule->flags & FP_FL_INVERT))
    {
        len += oal_util_snprintf(buf + len, buf_len - len, "!");
    }
    len += oal_util_snprintf(buf + len, buf_len - len, "(0x%x & 0x%x == ", rule->data, rule->mask);
    if(FP_FL_L4_OFFSET == (rule->flags & FP_FL_L4_OFFSET))
    {
        len += oal_util_snprintf(buf + len, buf_len - len, "frame[L4 header + %u] & 0x%x)", rule->offset, rule->mask);
    }
    if(FP_FL_L3_OFFSET == (rule->flags & FP_FL_L3_OFFSET))
    {
        len += oal_util_snprintf(buf + len, buf_len - len, "frame[L3 header + %u] & 0x%x)", rule->offset, rule->mask);
    }
    else
    {
        len += oal_util_snprintf(buf + len, buf_len - len, "frame[%u] & 0x%x)", rule->offset, rule->mask);
    }
    /* Consequences */
    if(FP_FL_REJECT == (rule->flags & FP_FL_REJECT))
    {
        len += oal_util_snprintf(buf + len, buf_len - len, "? REJECT : use next rule");
    }
    else if(FP_FL_ACCEPT == (rule->flags & FP_FL_ACCEPT))
    {
        len += oal_util_snprintf(buf + len, buf_len - len, "? ACCEPT : use next rule");
    }
    else
    {
        len += oal_util_snprintf(buf + len, buf_len - len, "? use rule %s : use next rule", rule->next_rule);
    }
    len += oal_util_snprintf(buf + len, buf_len - len, "}\n");
    return len;
}

/**
* @brief Prints table rules in a human readable form
* @param[in] table Table to be printed
* @param[in] buf Buffer where to put the output
* @param[in] len Size of the buffer
* @param[in] verb_level Verbosity level
* @return Number of characters written into the buffer
*/
uint32_t fci_fp_print_table(fci_fp_table_t *table, char_t *buf, uint32_t buf_len, uint8_t verb_level)
{
    uint32_t len = 0U;
    LLIST_t *item;
    fci_fp_rule_t *rule;

    len += oal_util_snprintf(buf + len, buf_len - len, "%s = {\n", table->name);
    LLIST_ForEach(item, &table->rules_db.rules)
    {
        rule = LLIST_Data(item, fci_fp_rule_t, table_entry);
        len += fci_fp_print_rule(rule, buf + len, buf_len - len, verb_level);
    }

    len += oal_util_snprintf(buf + len, buf_len - len, "\n}\n");
    return len;
}

/**
* @brief Prints all tables in a human readable form
* @param[in] table Table to be printed
* @param[in] buf Buffer where to put the output
* @param[in] len Size of the buffer
* @param[in] verb_level Verbosity level
* @return Number of characters written into the buffer
*/
uint32_t fci_fp_print_tables(char_t *buf, uint32_t buf_len, uint8_t verb_level)
{
    fci_fp_table_t *table;
    LLIST_t *item;
    uint32_t len = 0U;

    LLIST_ForEach(item, &fci_fp_table_db.tables)
    {
        table = LLIST_Data(item,  fci_fp_table_t, db_entry);
        len += fci_fp_print_table(table, buf + len, buf_len - len, verb_level);
    }
    return len;
}

uint32_t pfe_fp_get_text_statistics(pfe_fp_t *temp, char_t *buf, uint32_t buf_len, uint8_t verb_level)
{
    fci_fp_table_t *table;
    pfe_ct_class_flexi_parser_stats_t *c_stats; 
    LLIST_t *item;
    uint32_t len = 0U;
    uint32_t pe_idx = 0U;

    LLIST_ForEach(item, &fci_fp_table_db.tables)
    {
        table = LLIST_Data(item,  fci_fp_table_t, db_entry);
        len += oal_util_snprintf(buf + len, buf_len - len, "%s = {\n", table->name);
        if (table->dmem_addr != 0)
        {
            c_stats = oal_mm_malloc(sizeof(pfe_ct_class_flexi_parser_stats_t) * (pfe_class_get_num_of_pes(table->class) + 1U));
            if(NULL == c_stats)
            {
                NXP_LOG_ERROR("Memory allocation failed\n");
                oal_mm_free(c_stats);
                return len;
            }

            (void)memset(c_stats, 0, sizeof(pfe_ct_class_flexi_parser_stats_t) * (pfe_class_get_num_of_pes(table->class) + 1U));

            for(pe_idx = 0U; pe_idx < pfe_class_get_num_of_pes(table->class); pe_idx++)
            {
                pfe_fp_table_get_statistics(table->class, pe_idx, table->dmem_addr, &c_stats[pe_idx +1]);
                pfe_class_flexi_parser_stats_endian(&c_stats[pe_idx + 1]);
                pfe_class_sum_flexi_parser_stats(&c_stats[0], &c_stats[pe_idx + 1]);
            }

            len += pfe_class_fp_stat_to_str(&c_stats[0U], buf + len, buf_len - len, verb_level);

            oal_mm_free(c_stats);
        }
        else
        {
            len += oal_util_snprintf(buf + len, buf_len - len, "Table not enabled in Firmware\n");
        }

        len += oal_util_snprintf(buf + len, buf_len - len, "\n}\n");
    }

    return len;
}

