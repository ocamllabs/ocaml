#include <stdlib.h>
#include <stdio.h>
#include "config.h"
#include "memory.h"
#include "mlvalues.h"
#include "forward_table.h"
#include "global_heap.h"
#include "misc.h"

PER_CONTEXT struct forward_table fwd_table = FORWARD_TABLE_INIT;

CAMLexport value caml_alloc_global(mlsize_t wosize, tag_t tag) {
  printf("Allocating %d words of global heap\n", wosize);
  void* obj = malloc(Bhsize_wosize(wosize));
  Assert (!Is_in_value_area(Val_hp(obj)));
  Hd_hp(obj) = Make_header(wosize, tag, Caml_black);
#ifdef DEBUG
  {
    uintnat i;
    for (i = 0; i < wosize; i++) {
      Field_ (Val_hp (obj), i) = Debug_uninit_global;
    }
  }
#endif
  return Val_hp (obj);
}

CAMLexport value caml_get_global_version(value val) {
  Assert (Is_block(val));
  Assert (Is_in_value_area(val));
  Assert (Is_yellow_hd(Hd_val(val)));
  value ret = (value)forward_table_lookup(&fwd_table, val);
  printf("%08llx -> %08llx\n", val, ret);
  return ret;
}

CAMLexport value caml_globalize(value root) {
  printf("glob %d %d %d\n", Is_block(root), Is_in_value_area(root), Is_yellow_hd(Hd_val(root)));
  if (!Is_block(root)) 
    return root;
  if (!Is_in_value_area(root))
    return root;
  if (Is_yellow_hd(Hd_val(root)))
    return caml_get_global_version(root);
  
  value* stack = 0;
  value local = root, global = Val_long(0);
  uintnat field = 0;

  while (1) {
    /* It is important that we use Field_, not Field, since we are
       now setting up the global version of the object */
    header_t hd = Hd_val(local);
    printf("globalizing %llx %d/%d\n", local, field, Wosize_hd(hd));

    if (field == 0) {
      /* just moved onto a new object, allocate a global copy */
      global = caml_alloc_global(Wosize_hd(hd), Tag_hd(hd));
      Hd_val(local) = Yellowhd_hd(hd);
      uintnat* fwd = forward_table_insert_pos(&fwd_table, local);
      Assert(*fwd == FORWARD_TABLE_NOT_PRESENT);
      *fwd = (uintnat)global;
    }

    Assert(Is_block(local) && Is_block(global));
    Assert(caml_get_global_version(local) == global);

    if (field < Wosize_hd(hd)) {
      /* still copying fields of current object */
      value curr = Field_(local, field);
      if (!Is_block(curr) || !Is_in_value_area(curr)) {
        /* doesn't need to be copied to global heap */
        Field_(global, field) = curr;
        field++;
      } else if (Is_yellow_hd (Hd_val (curr))) {
        /* has already been copied to global heap */
        Field_(global, field) = caml_get_global_version(curr);
        field++;
      } else {
        /* need to deep-copy this field, push it to the stack */
        Hd_val(global) = (header_t)stack;
        Field_(global, field) = local;
        stack = &Field_(global, field);
        /* move onto child object */
        local = curr;
        field = 0;
      }
    } else {
      /* finished with this object */
      Hd_val(global) = Make_header(Wosize_hd(hd), Tag_hd(hd), Caml_black);
      if (stack) {
        /* pop next object from stack */
        local = *stack;
        *stack = global;
        global = caml_get_global_version(local);
        field = (uintnat)(stack - &Field_(global, 0));
        Assert(0 <= field && field <= Wosize_val(local));
        stack = (value*)Hd_val(global);
        field++;
      } else {
        /* done */
        return global;
      }
    }
  }
}

CAMLextern value caml_read_barrier(value x) {
  Assert (Is_block(x));
  return Canonicalize(x);
}
