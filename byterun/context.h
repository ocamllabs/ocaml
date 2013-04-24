#ifndef CAML_CONTEXT_H
#define CAML_CONTEXT_H

struct global_ptr;
void global_ptr_init(struct global_ptr* g);
void* global_ptr_read(struct global_ptr* g);
void* global_ptr_begin_update(struct global_ptr* g);
void global_ptr_commit_update(struct global_ptr* g, void* new_value);

struct bar;

extern struct bar mybar;



void caml_init_main_context();

/* called before any caml code runs, but after the heap exists */
void caml_setup_main_context();


void caml_stop_the_world();
void caml_resume_the_world();

void caml_message_poll();

#endif
