/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 2001 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

#ifndef CAML_STARTUP_H
#define CAML_STARTUP_H

#include "mlvalues.h"
#include "exec.h"

extern int caml_parser_trace;
extern char * caml_exe_name;
extern char ** caml_main_argv;

#ifndef NATIVE_CODE
extern char * caml_section_table;
extern asize_t caml_section_table_size;
#endif


CAMLextern void caml_main(char **argv);

CAMLextern void caml_startup_code(
           code_t code, asize_t code_size,
           char *data, asize_t data_size,
           char *section_table, asize_t section_table_size,
           char **argv);

enum { FILE_NOT_FOUND = -1, BAD_BYTECODE  = -2 };

extern int caml_attempt_open(char **name, struct exec_trailer *trail,
                             int do_open_script);
extern void caml_read_section_descriptors(int fd, struct exec_trailer *trail);
extern int32 caml_seek_optional_section(int fd, struct exec_trailer *trail,
                                        char *name);
extern int32 caml_seek_section(int fd, struct exec_trailer *trail, char *name);


#endif /* CAML_STARTUP_H */
