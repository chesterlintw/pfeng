/* =========================================================================
 *  Copyright 2017-2018 NXP
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation 
 *    and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ========================================================================= */
 
#ifndef LINKED_LIST_H
#define LINKED_LIST_H

/**
* @brief   This structure shall be used to link structures of data.
* @details Just put this anywhere into the structs which shall be linked.
*/
typedef struct LLIST_Struct
{
   struct LLIST_Struct *prNext;
   struct LLIST_Struct *prPrev;
} LLIST_t;

/**
* @def   LLIST_Init
* @brief Initializes given linked list as empty.
*/
#define LLIST_Init(prList) \
do \
{ \
    (prList)->prNext = (prList); \
    (prList)->prPrev = (prList); \
}while(0)

/**
* @def        LLIST_ForEach
* @brief      Iterates through all list items, do not use to delete them.
* @details    The list shall not be modified during the loop.
* @param[out] prCurrent Provide a pointer of type LLIST_t *. In each iteration an item
*             from the list is assigned to it. Do not free this memory - it must exist
*             in the next iteration.
* @param[in]  prHead Head of the list to loop in. Type is again LLIST_t *.
*/
#define LLIST_ForEach(prCurrent, prHead) \
            for( \
                 (prCurrent) = (prHead)->prNext; \
                 (prCurrent) != (prHead); \
                 (prCurrent) = (prCurrent)->prNext \
               )

/**
* @def        LLIST_ForEachRemovable
* @brief      Iterates through all list items with purpose of deleting them.
* @details    It is possible to remove prCurrent in the iteration.
* @param[out] prCurrent Provide a pointer of type LLIST_t *. In each iteration an item
*             from the list is assigned to it. It is safe to free this memory because
*             information for next iterations is stored in prAuxNext.
* @param[out] prAuxNext One extra pointer of type LLIST_t * must be provided to be
*             used for internal use by this macro.
*             internally by the macro.
* @param[in]  prHead Head of the list to loop in. Type is again LLIST_t *.
*/
#define LLIST_ForEachRemovable(prCurrent, prAuxNext, prHead) \
            for( \
                 (prCurrent) = (prHead)->prNext, (prAuxNext) = (prCurrent)->prNext; \
                 (prCurrent) != (prHead); \
                 (prCurrent) = (prAuxNext), (prAuxNext) = (prAuxNext)->prNext \
               )

/**
* @def   LLIST_OffsetOff
* @brief Custom definition of offsetof macro.
*/
#define LLIST_OffsetOff(TypeOfData,NameOfList) (&(((TypeOfData*)0U)->NameOfList))

/**
* @def       LLIST_Data
* @brief     Provide pointer to the structure given list is contained in.
* @param[in] prListItem The pointer to LLIST_t.
* @param[in] TypeOfData The type of the struct this is embedded in.
* @param[in] NameOfList The name of the LLIST_t within the struct.
*/
#define LLIST_Data(prListItem,TypeOfData,NameOfList) \
            ( (TypeOfData*)((char_t *)(prListItem) \
              - (char_t *)LLIST_OffsetOff(TypeOfData, NameOfList)) \
            )

/**
* @def       LLIST_DataChkNull
* @brief     Same as LLIST_Data with NULL pointer check.
* @details   If the prListItem is NULL then it returns NULL.
* @param[in] prListItem The pointer to LLIST_t.
* @param[in] TypeOfData The type of the struct this is embedded in.
* @param[in] NameOfList The name of the LLIST_t within the struct.
*/
#define LLIST_DataChkNull(prListItem,TypeOfData,NameOfList) \
            ( ((prListItem)==NULL) ? \
              NULL : LLIST_Data((prListItem),TypeOfData,NameOfList) \
            )

/**
* @def   LLIST_DataFirst
* @brief Like LLIST_Data but returns next structure in the list.
*/
#define LLIST_DataFirst(prListHead,TypeOfData,NameOfList) \
            LLIST_Data((prListHead)->prNext,TypeOfData,NameOfList)

/**
* @def   LLIST_DataLast
* @brief Like LLIST_Data but returns previous structure in the list.
*/
#define LLIST_DataLast(prListHead,TypeOfData,NameOfList) \
            LLIST_Data((prListHead)->prPrev,TypeOfData,NameOfList)

/**
* @def   LLIST_AddAtBegin
* @brief Adds given entry to the beginning of given list.
*/
#define LLIST_AddAtBegin(prNew, prHead) \
do \
{ \
    (prNew)->prNext = (prHead)->prNext; \
    (prNew)->prPrev = (prHead); \
    (prHead)->prNext->prPrev = (prNew); \
    (prHead)->prNext = (prNew); \
}while(0)

/**
* @def   LLIST_AddAtEnd
* @brief Adds given entry to the end of given list.
*/
#define LLIST_AddAtEnd(prNew, prHead) \
do \
{ \
    (prNew)->prPrev = (prHead)->prPrev; \
    (prNew)->prNext = (prHead); \
    (prHead)->prPrev->prNext = (prNew); \
    (prHead)->prPrev = (prNew); \
}while(0)

/**
* @def       LLIST_Remove
* @brief     Removes given entry from its list.
* @details   After this, the entry is in an undefined state.
* @param[in] prEntry Pointer to item to be removed.
*/
#define LLIST_Remove(prEntry) \
do \
{ \
    (prEntry)->prPrev->prNext = (prEntry)->prNext; \
    (prEntry)->prNext->prPrev = (prEntry)->prPrev; \
    (prEntry)->prPrev = NULL; \
    (prEntry)->prNext = NULL; \
}while(0)

/**
* @def   LLIST_IsEmpty
* @brief Returns boolean information whether the list is empty.
*/
#define LLIST_IsEmpty(prList) ((prList)->prNext == (prList))

#endif /* LINKED_LIST_H */