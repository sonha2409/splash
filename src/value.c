#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define VALUE_LIST_INITIAL_CAP 8


Value *value_string(const char *s) {
    Value *v = xmalloc(sizeof(Value));
    v->type = VALUE_STRING;
    v->string = xstrdup(s ? s : "");
    return v;
}

Value *value_int(int64_t n) {
    Value *v = xmalloc(sizeof(Value));
    v->type = VALUE_INT;
    v->integer = n;
    return v;
}

Value *value_float(double f) {
    Value *v = xmalloc(sizeof(Value));
    v->type = VALUE_FLOAT;
    v->floating = f;
    return v;
}

Value *value_bool(bool b) {
    Value *v = xmalloc(sizeof(Value));
    v->type = VALUE_BOOL;
    v->boolean = b;
    return v;
}

Value *value_nil(void) {
    Value *v = xmalloc(sizeof(Value));
    v->type = VALUE_NIL;
    return v;
}

Value *value_list(void) {
    Value *v = xmalloc(sizeof(Value));
    v->type = VALUE_LIST;
    v->list.items = xmalloc(VALUE_LIST_INITIAL_CAP * sizeof(Value *));
    v->list.count = 0;
    v->list.capacity = VALUE_LIST_INITIAL_CAP;
    return v;
}


void value_free(Value *v) {
    if (!v) {
        return;
    }
    switch (v->type) {
    case VALUE_STRING:
        free(v->string);
        break;
    case VALUE_LIST:
        for (size_t i = 0; i < v->list.count; i++) {
            value_free(v->list.items[i]);
        }
        free(v->list.items);
        break;
    case VALUE_TABLE:
        // table_free() will be implemented in 7.2
        // For now, table pointer is not freed here
        break;
    case VALUE_INT:
    case VALUE_FLOAT:
    case VALUE_BOOL:
    case VALUE_NIL:
        break;
    }
    free(v);
}


Value *value_clone(const Value *v) {
    if (!v) {
        return NULL;
    }
    switch (v->type) {
    case VALUE_STRING:
        return value_string(v->string);
    case VALUE_INT:
        return value_int(v->integer);
    case VALUE_FLOAT:
        return value_float(v->floating);
    case VALUE_BOOL:
        return value_bool(v->boolean);
    case VALUE_NIL:
        return value_nil();
    case VALUE_LIST: {
        Value *clone = value_list();
        for (size_t i = 0; i < v->list.count; i++) {
            value_list_append(clone, value_clone(v->list.items[i]));
        }
        return clone;
    }
    case VALUE_TABLE:
        // table_clone() will be implemented in 7.2
        // For now, return NIL as placeholder
        return value_nil();
    }
    return value_nil();
}


char *value_to_string(const Value *v) {
    if (!v) {
        return xstrdup("(null)");
    }
    char buf[64];
    switch (v->type) {
    case VALUE_STRING:
        return xstrdup(v->string);
    case VALUE_INT:
        snprintf(buf, sizeof(buf), "%lld", (long long)v->integer);
        return xstrdup(buf);
    case VALUE_FLOAT:
        snprintf(buf, sizeof(buf), "%g", v->floating);
        return xstrdup(buf);
    case VALUE_BOOL:
        return xstrdup(v->boolean ? "true" : "false");
    case VALUE_NIL:
        return xstrdup("nil");
    case VALUE_LIST: {
        // Format: [item1, item2, ...]
        size_t total = 2; // "[]"
        char **strs = xmalloc(v->list.count * sizeof(char *));
        for (size_t i = 0; i < v->list.count; i++) {
            strs[i] = value_to_string(v->list.items[i]);
            total += strlen(strs[i]);
            if (i > 0) {
                total += 2; // ", "
            }
        }
        char *result = xmalloc(total + 1);
        char *p = result;
        *p++ = '[';
        for (size_t i = 0; i < v->list.count; i++) {
            if (i > 0) {
                *p++ = ',';
                *p++ = ' ';
            }
            size_t len = strlen(strs[i]);
            memcpy(p, strs[i], len);
            p += len;
            free(strs[i]);
        }
        *p++ = ']';
        *p = '\0';
        free(strs);
        return result;
    }
    case VALUE_TABLE:
        return xstrdup("<table>");
    }
    return xstrdup("(unknown)");
}


const char *value_type_name(ValueType t) {
    switch (t) {
    case VALUE_STRING: return "string";
    case VALUE_INT:    return "int";
    case VALUE_FLOAT:  return "float";
    case VALUE_BOOL:   return "bool";
    case VALUE_NIL:    return "nil";
    case VALUE_TABLE:  return "table";
    case VALUE_LIST:   return "list";
    }
    return "unknown";
}


bool value_equal(const Value *a, const Value *b) {
    if (!a || !b) {
        return a == b;
    }
    if (a->type != b->type) {
        return false;
    }
    switch (a->type) {
    case VALUE_STRING:
        return strcmp(a->string, b->string) == 0;
    case VALUE_INT:
        return a->integer == b->integer;
    case VALUE_FLOAT:
        return a->floating == b->floating;
    case VALUE_BOOL:
        return a->boolean == b->boolean;
    case VALUE_NIL:
        return true;
    case VALUE_LIST:
        if (a->list.count != b->list.count) {
            return false;
        }
        for (size_t i = 0; i < a->list.count; i++) {
            if (!value_equal(a->list.items[i], b->list.items[i])) {
                return false;
            }
        }
        return true;
    case VALUE_TABLE:
        // Identity comparison — same pointer
        return a->table == b->table;
    }
    return false;
}


void value_list_append(Value *list, Value *item) {
    if (!list || list->type != VALUE_LIST || !item) {
        return;
    }
    if (list->list.count >= list->list.capacity) {
        list->list.capacity *= 2;
        list->list.items = xrealloc(list->list.items,
                                     list->list.capacity * sizeof(Value *));
    }
    list->list.items[list->list.count++] = item;
}

Value *value_list_get(const Value *list, size_t index) {
    if (!list || list->type != VALUE_LIST || index >= list->list.count) {
        return NULL;
    }
    return list->list.items[index];
}

size_t value_list_count(const Value *list) {
    if (!list || list->type != VALUE_LIST) {
        return 0;
    }
    return list->list.count;
}
