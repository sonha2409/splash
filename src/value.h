#ifndef SPLASH_VALUE_H
#define SPLASH_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declaration — defined in table.h (Milestone 7.2)
typedef struct Table Table;

// Value types for structured data pipes
typedef enum {
    VALUE_STRING,
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_BOOL,
    VALUE_NIL,
    VALUE_TABLE,
    VALUE_LIST
} ValueType;

// Dynamic list of Values
typedef struct {
    struct Value **items;   // Array of owned Value pointers
    size_t count;
    size_t capacity;
} ValueList;

// Tagged union representing any splash value.
// Caller takes ownership of heap-allocated Values and must call value_free().
typedef struct Value {
    ValueType type;
    union {
        char *string;       // VALUE_STRING — owned, heap-allocated
        int64_t integer;    // VALUE_INT
        double floating;    // VALUE_FLOAT
        bool boolean;       // VALUE_BOOL
        Table *table;       // VALUE_TABLE — owned
        ValueList list;     // VALUE_LIST — inline, owns its items
    };
} Value;

// Constructors — each returns a heap-allocated Value. Caller takes ownership.
Value *value_string(const char *s);
Value *value_int(int64_t n);
Value *value_float(double f);
Value *value_bool(bool b);
Value *value_nil(void);
Value *value_list(void);
Value *value_table(Table *t);  // Takes ownership of the Table

// Destructor — recursively frees the value and all owned data.
void value_free(Value *v);

// Deep copy. Caller takes ownership of the returned Value.
Value *value_clone(const Value *v);

// Returns a human-readable string representation. Caller must free().
char *value_to_string(const Value *v);

// Returns a static string for the type name (e.g., "string", "int").
const char *value_type_name(ValueType t);

// Equality comparison. TABLE values compare by pointer (identity).
bool value_equal(const Value *a, const Value *b);

// List operations — list must be VALUE_LIST, item ownership is transferred.
void value_list_append(Value *list, Value *item);

// Returns the item at index, or NULL if out of bounds. Does NOT transfer ownership.
Value *value_list_get(const Value *list, size_t index);

// Returns the number of items in a list. list must be VALUE_LIST.
size_t value_list_count(const Value *list);

#endif // SPLASH_VALUE_H
