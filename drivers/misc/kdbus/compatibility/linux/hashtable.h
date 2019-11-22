#include_next <linux/hashtable.h>

#ifndef KDBUS_HASHTABLE_H
#define KDBUS_HASHTABLE_H

// bus.c
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
#undef hlist_for_each_entry
#undef hash_for_each_possible

#define hlist_entry_safe(ptr, type, member) \
         ({ typeof(ptr) ____ptr = (ptr); \
            ____ptr ? hlist_entry(____ptr, type, member) : NULL; \
         })

#define hlist_for_each_entry(pos, head, member)                         \
         for (pos = hlist_entry_safe((head)->first, typeof(*(pos)), member);\
              pos;                                                       \
              pos = hlist_entry_safe((pos)->member.next, typeof(*(pos)), member))

#define hash_for_each_possible(name, obj, member, key)                  \
         hlist_for_each_entry(obj, &name[hash_min(key, HASH_BITS(name))], member)

// connection.c
#undef hash_for_each

#define hash_for_each(name, bkt, obj, member)                           \
         for ((bkt) = 0, obj = NULL; obj == NULL && (bkt) < HASH_SIZE(name);\
                         (bkt)++)\
                 hlist_for_each_entry(obj, &name[bkt], member)
#endif

// names.c
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
#undef hash_for_each_safe
#undef hlist_for_each_entry_safe

#define hlist_for_each_entry_safe(pos, n, head, member)                 \
         for (pos = hlist_entry_safe((head)->first, typeof(*pos), member);\
              pos && ({ n = pos->member.next; 1; });                     \
              pos = hlist_entry_safe(n, typeof(*pos), member))

#define hash_for_each_safe(name, bkt, tmp, obj, member)                 \
         for ((bkt) = 0, obj = NULL; obj == NULL && (bkt) < HASH_SIZE(name);\
                         (bkt)++)\
                 hlist_for_each_entry_safe(obj, tmp, &name[bkt], member)
#endif
#endif /* KDBUS_HASHTABLE_H */
