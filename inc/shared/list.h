/*
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef LIST_H
#define LIST_H

//
// list.h
//

typedef struct list_s {
    struct list_s   *next; // head
    struct list_s   *prev; // tail
} list_t;

static inline void List_Link(list_t *prev,
                             list_t *next,
                             list_t *elem)
{
    prev->next = elem;
    next->prev = elem;
    elem->prev = prev;
    elem->next = next;
}

static inline void List_Unlink(list_t *prev, list_t *next)
{
    prev->next = next;
    next->prev = prev;
}

static inline void List_Relink(list_t *elem)
{
    elem->prev->next = elem;
    elem->next->prev = elem;
}

static inline void List_Init(list_t *list)
{
    list->prev = list->next = list;
}

static inline void List_Append(list_t *tail, list_t *elem)
{
    List_Link(tail->prev, tail, elem);
}

static inline void List_Insert(list_t *head, list_t *elem)
{
    List_Link(head, head->next, elem);
}

#define LIST_FOR_EACH_ELEM(cursor, list) \
    for (cursor = (list)->next; cursor != list; cursor = (cursor)->next)

// insert element into the sorted list of array elements
static inline void List_SeqAdd(list_t *list, list_t *elem)
{
    list_t *cursor;

    LIST_FOR_EACH_ELEM(cursor, list) {
        if (elem < cursor) {
            break;
        }
    }

    List_Append(cursor, elem);
}

static inline void List_Delete(list_t *elem)
{
    List_Unlink(elem->prev, elem->next);
    List_Init(elem);
}

static inline void List_Remove(list_t *elem)
{
    List_Unlink(elem->prev, elem->next);
}

#define LIST_ENTRY(type, elem, member) \
    ((type *)((unsigned char *)elem - q_offsetof(type, member)))

#define LIST_EMPTY(list) \
    ((list)->next == list)
 
#define LIST_SINGLE(list) \
    (!LIST_EMPTY(list) && (list)->next == (list)->prev)

#define LIST_TERM(cursor, list, member) \
    (&(cursor)->member == list)
 
#define LIST_FIRST(type, list, member) \
    LIST_ENTRY(type, (list)->next, member)

#define LIST_LAST(type, list, member) \
    LIST_ENTRY(type, (list)->prev, member)

#define LIST_NEXT(type, entry, member) \
    LIST_ENTRY(type, (entry)->member.next, member)

#define LIST_PREV(type, entry, member) \
    LIST_ENTRY(type, (entry)->member.prev, member)

#define LIST_FOR_EACH(type, cursor, list, member) \
    for (cursor = LIST_FIRST(type, list, member); \
         !LIST_TERM(cursor, list, member); \
         cursor = LIST_NEXT(type, cursor, member))

#define LIST_FOR_EACH_SAFE(type, cursor, next, list, member) \
    for (cursor = LIST_FIRST(type, list, member); \
         next = LIST_NEXT(type, cursor, member), \
         !LIST_TERM(cursor, list, member); \
         cursor = next)

#define LIST_DECL(list) list_t list = { &list, &list }

static inline int List_Count(list_t *list)
{
    int count = 0;
    list_t *cursor;

    LIST_FOR_EACH_ELEM(cursor, list) {
        count++;
    }

    return count;
}

static inline void *List_Index(list_t *list, size_t offset, int index)
{
    int count = 0;
    list_t *cursor;

    LIST_FOR_EACH_ELEM(cursor, list) {
        if (count == index) {
            return (unsigned char *)cursor - offset;
        }
        count++;
    }

    return NULL;
}

#define LIST_INDEX(type, index, list, member) \
    ((type *)List_Index(list, q_offsetof(type, member), index))

#endif // LIST_H
