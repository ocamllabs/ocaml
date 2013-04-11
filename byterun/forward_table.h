#include "mlvalues.h"

struct forward_table_entry { uintnat key, value; };
struct forward_table {
  struct forward_table_entry* entries;
  uintnat size;
};

#define FORWARD_TABLE_INIT {0,0}


uintnat forward_table_lookup(struct forward_table* t, value v);

#define FORWARD_TABLE_NOT_PRESENT ((uintnat)(-1))


uintnat* forward_table_insert_pos(struct forward_table* t, value v);


void forward_table_clear(struct forward_table* t);
