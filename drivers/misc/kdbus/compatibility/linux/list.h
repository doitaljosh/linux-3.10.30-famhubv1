#include_next <linux/list.h>

#ifndef KDBUS_LIST_H
#define KDBUS_LIST_H


/**
 * list_first_entry_or_null - get the first element from a list
 * @ptr:        the list head to take the element from.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the list_struct within the struct.
 *
 * Note that if the list is empty, it returns NULL.
 */
#define list_first_entry_or_null(ptr, type, member) \
        (!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)


#endif
