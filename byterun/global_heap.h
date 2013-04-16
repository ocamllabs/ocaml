#ifndef CAML_GLOBAL_HEAP_H
#define CAML_GLOBAL_HEAP_H
#include "mlvalues.h"


CAMLexport value caml_alloc_global(mlsize_t wosize, tag_t tag);

CAMLexport value caml_get_global_version(value val);

CAMLexport value caml_globalize(value val);

#define Is_global_val(val) Is_yellow_hd (Hd_val (val))

#define Canonicalize(val)                                           \
  ( (Is_block(val) && Is_in_value_area(val) && Is_global_val(val))        \
    ? caml_get_global_version(val) : val)




#endif
