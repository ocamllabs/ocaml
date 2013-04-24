#define _BSD_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "atomic.h"
#include "context.h"
#include "misc.h"
#include "mlvalues.h"
#include "global_heap.h"
#include "startup.h"
#include "memory.h"
#include "alloc.h"
#include "callback.h"

#define message_poll caml_message_poll

typedef atomic_word counter;

struct context;
__thread struct context* self;


struct message;
typedef void (*message_callback)(struct context*, struct message*);

struct message {
  message_callback action;
  void* ptr;
  counter num;
  value camlval;
};


#define MSG_QUEUE_CHUNK_SIZE 2
struct message_queue_chunk_entry {
  atomic_word valid;
  struct message msg;
};
struct message_queue_chunk {
  counter chunk_start;
  struct message_queue_chunk* prev;
  /* (struct message_queue_chunk*) */ atomic_word next; 
  struct message_queue_chunk_entry msg[MSG_QUEUE_CHUNK_SIZE];
};

struct message_queue {
  counter produced;

  counter produce_chunk_start;
  /* (struct message_queue_chunk*) */ atomic_word produce_chunk;

  counter consumed;
  struct message_queue_chunk* consume_chunk;
};
void message_queue_init(struct message_queue* q) {
  q->produced = q->consumed = 0;
  struct message_queue_chunk* chunk = malloc(sizeof(struct message_queue_chunk));
  memset(chunk, 0, sizeof(*chunk)); // dubious.
  chunk->next = 0;
  q->consume_chunk = chunk;
  store_release_ptr(&q->produce_chunk, chunk);
}

counter message_queue_produce(struct message_queue* q, struct message msg) {
  counter slot = fetch_and_increment(&q->produced);
  counter chunk_pos = slot % MSG_QUEUE_CHUNK_SIZE;
  counter chunk_start = slot - chunk_pos;
  assert(chunk_start % MSG_QUEUE_CHUNK_SIZE == 0);

  /* We can't look at old chunks, they may have been freed.
     Wait until the current chunk is new */
  while (load_acquire(&q->produce_chunk_start) < chunk_start) {
    CPU_RELAX();
  }
  struct message_queue_chunk* chunk = load_acquire_ptr(&q->produce_chunk);

  assert(chunk->chunk_start >= chunk_start);  
  while (chunk->chunk_start > chunk_start) {
    chunk = chunk->prev;
    assert(chunk); // FAIL
  }
  assert(chunk->chunk_start == chunk_start); // FAIL

  if (chunk_pos == MSG_QUEUE_CHUNK_SIZE - 1) {
    // if we add the last message to this chunk, we advance to the next one
    struct message_queue_chunk* next = load_wait_ptr(&chunk->next);
    assert(next->chunk_start == chunk_start + MSG_QUEUE_CHUNK_SIZE);
    store_release_ptr(&q->produce_chunk, next);
    /* complexity */
    store_release(&q->produce_chunk_start, next->chunk_start);
  }

  /* The consumer will free the chunk only after it loads all of the
     messages and the next pointer. */

  chunk->msg[chunk_pos].msg = msg;

  /* The consumer will not free the chunk until it observes this store */
  store_release(&chunk->msg[chunk_pos].valid, 1);

  /* The producer of the first message in a chunk allocates the next chunk.
     This could equally be done by the last producer in the block, but since
     allocating and initialising a new chunk may be relatively slow, we get 
     better concurrency by doing it early so that it's almost certainly done by
     the time the last producer runs */
  if (chunk_pos == 0) {
    // if we add the first message to this chunk, we allocate the next chunk.
    assert(chunk->next == 0);
    struct message_queue_chunk* next = malloc(sizeof(struct message_queue_chunk));
    next->chunk_start = chunk->chunk_start + MSG_QUEUE_CHUNK_SIZE;
    next->prev = chunk;
    next->next = 0;
    memset(next->msg, 0, sizeof(next->msg));
    /* consumer will not free the chunk until it observes this store */
    store_release_ptr(&chunk->next, next);
  }
  return slot;
}

int message_queue_consume(struct message_queue* q, struct message* msg) {
  /* it may be OK to relax the acquire/release semantics of q->consumed
     (won't make a difference for x86 */
  counter slot = load_acquire(&q->consumed);
  if (slot < load_acquire(&q->produced)) {
    counter chunk_pos = slot % MSG_QUEUE_CHUNK_SIZE;
    counter chunk_start = slot - chunk_pos;
    assert(chunk_start == q->consume_chunk->chunk_start);
    load_wait(&q->consume_chunk->msg[chunk_pos].valid);
    *msg = q->consume_chunk->msg[chunk_pos].msg;
    if (chunk_pos == MSG_QUEUE_CHUNK_SIZE - 1) {
      // just consumed last message from this chunk, free it
      // all writers to this chunk have finished since otherwise we would
      // have blocked waiting for them.
      struct message_queue_chunk* chunk = q->consume_chunk;
      assert(!chunk->prev);

      /* next pointer may not be written yet, it is written
         after the message is sent. wait for it. */
      q->consume_chunk = load_wait_ptr(&chunk->next);

      /* all producers have finished modifying the current chunk.
         once some producer advances produce_chunk, then it's safe to
         free this chunk */
      while (load_acquire(&q->produce_chunk_start) <= chunk->chunk_start) {
        CPU_RELAX();
      }

      /* there are no more producer references to this chunk, free it */
      assert(q->consume_chunk->prev == chunk);
      q->consume_chunk->prev = 0; /* just for debugging, this pointer should never be read again */
      free(chunk);
    }
    store_release(&q->consumed, slot + 1);
    return 1;
  } else {
    return 0;
  }
}

int message_queue_has_consumed(struct message_queue* q, counter slot) {
  return load_acquire(&q->consumed) > slot;
}


struct context {
  pthread_t thread;
  value main_func;

  struct message_queue msgq;

  /* always held by this context except when sleeping */
  pthread_mutex_t awake_lock;

  /* set to nonzero between context_sleep and context_wake */
  atomic_word is_asleep;

  /* becomes nonzero after context's thread starts */
  atomic_word is_started;

  /* becomes zero after context is removed from all_context */
  atomic_word is_shutdown;

  /* constant once assigned */
  int id;

  /* Ocaml value representing context (boxed pointer to this struct) */
  value caml_self;
};



void action_await_counter(struct context* c, struct message* m) {
  if (c == self) {
    fprintf(stderr, "Context %d waiting...\n", self->id);
    fflush(stdout);
    counter* c = m->ptr;
    while (load_acquire(c) < m->num) CPU_RELAX();
    fprintf(stderr, "... context %d finished waiting\n", self->id);
  }
}

struct message msg_await_counter(counter* c, counter value) {
  struct message m = {&action_await_counter, (void*)c, value, Val_unit};
  return m;
}


struct global_ptr {
  /* overflow is fine, as long as the maximum value exceeds the number of threads */
  atomic_word new_generation;
  atomic_word generation;
  atomic_word ptr;
};

#define GLOBAL_PTR_INIT(p) { 0, 0, (atomic_word)(p) }

void* global_ptr_read(struct global_ptr* g) {
  return (void*)load_acquire(&g->ptr);
}

struct context_set {
  int ncontexts;
  struct context** contexts;
};

atomic_word next_context_id;
struct global_ptr all_contexts = GLOBAL_PTR_INIT(0);

struct context* get_context(atomic_word ctx_id) {
  struct context_set* cs = global_ptr_read(&all_contexts);
  int i;
  for (i = 0; i < cs->ncontexts; i++) {
    if (cs->contexts[i]->id == ctx_id) {
      return cs->contexts[i];
    }
  }
  return 0;
}

void message_poll() {
  struct message msg;
  while (message_queue_consume(&self->msgq, &msg)) {
    msg.action(self, &msg);
  }
}

void message_poll_until(atomic_word* word, atomic_word value) {
  if (load_acquire(word) != value) {
    do {
      message_poll();
      CPU_RELAX();
    } while (load_acquire(word) != value);
  }
}


struct message_in_flight {
  struct message msg;
  struct context* c;
  counter slot;
};

struct message_in_flight message_send(struct context* c, struct message m) {
  if (!c) {
    fprintf(stderr, "message sent to dead context\n");
    exit(244);
    //return; // FIXME
  }
  counter slot = message_queue_produce(&c->msgq, m);
  struct message_in_flight ret = {m, c, slot};
  return ret;
}

/* may pump msgq */
void message_sync(struct message_in_flight m, int run_locally) {
  struct context* c = m.c;
  while (1) {
    if (message_queue_has_consumed(&c->msgq, m.slot)) {
      // remote thread is handling message
      break;
    }
     
    // if the thread is sleeping, we may want to handle the message ourselves
    if (run_locally && load_acquire(&c->is_asleep)) {
      if (pthread_mutex_trylock(&c->awake_lock) == 0) {
        // we can handle message
        m.msg.action(c, &m.msg);
        pthread_mutex_unlock(&c->awake_lock);
        break;
      }
    }
    
    message_poll();
  }
}

/* Send a message to every other context and wait for them to recieve it */
void message_broadcast_sync_waking(struct message m) {
  struct message_in_flight* msgs = 0;
  int nmsgs=0, i;
  {
    struct context_set* cs = global_ptr_read(&all_contexts);
    if (cs) {
      msgs = malloc(cs->ncontexts * sizeof(struct message_in_flight));
      for (i = 0; i < cs->ncontexts; i++) {
        if (cs->contexts[i] != self) {
          msgs[nmsgs++] = message_send(cs->contexts[i], m);
        }
      }
    }
  }

  for (i = 0; i < nmsgs; i++) {
    message_sync(msgs[i], 1);
  }
  free(msgs);
}

/* Send a message to every other context */
void message_broadcast_async(struct message m) {
  int i;
  struct context_set* cs = global_ptr_read(&all_contexts);
  for (i = 0; i < cs->ncontexts; i++) {
    if (cs->contexts[i] != self) {
      message_send(cs->contexts[i], m);
    }
  }
}

/* currently, this is STW. RCU would be faster */
void* global_ptr_begin_update(struct global_ptr* g) {
  counter my_gen = __sync_fetch_and_add(&g->new_generation, 1);
  // wait for other in-progress updates to complete
  if (load_acquire(&g->generation) != my_gen) {
    do {
      message_poll();
      CPU_RELAX();
    } while (load_acquire(&g->generation) != my_gen);
  }
  // tell everyone else to wait for this update to complete
  message_broadcast_sync_waking(msg_await_counter(&g->generation, my_gen));
  return global_ptr_read(g);
}

void global_ptr_commit_update(struct global_ptr* g, void* new_value) {
  store_release(&g->ptr, (atomic_word)new_value);
  counter oldgen = load_acquire(&g->generation);
  store_release(&g->generation, oldgen + 1);
}

struct context* new_context() {
  int i;
  struct context* c = malloc(sizeof(struct context));
  pthread_mutex_init(&c->awake_lock, 0);
  c->is_asleep = 0;
  c->is_started = 0;
  c->is_shutdown = 0;
  c->id = fetch_and_increment(&next_context_id);
  message_queue_init(&c->msgq);

  c->caml_self = Val_unit;

  struct context_set* cs = global_ptr_begin_update(&all_contexts);
  if (!cs) {
    cs = malloc(sizeof(struct context_set));
    cs->ncontexts = 0;
    cs->contexts = 0;
  }

  struct context** new_contexts = malloc(sizeof(struct context*) * (cs->ncontexts + 1));
  for (i=0; i<cs->ncontexts; i++) {
    new_contexts[i] = cs->contexts[i];
  }
  new_contexts[cs->ncontexts] = c;
  free(cs->contexts);
  cs->contexts = new_contexts;
  cs->ncontexts++;
  global_ptr_commit_update(&all_contexts, cs);
  
  return c;
}

void setup_context_caml_val(struct context* c) {
  value caml_self = caml_alloc_small(1, Abstract_tag);
  Field(caml_self, 0) = (value)c;
  Assert (c->caml_self == Val_unit);
  c->caml_self = caml_globalize(caml_self);
}

void start_context(struct context* ctx) {
  assert(!self);
  pthread_mutex_lock(&ctx->awake_lock);
  ctx->thread = pthread_self();
  self = ctx;

  fprintf(stderr, "Context %d started\n", (int)self->id);
  store_release(&self->is_started, 1);
}

void shutdown_context() {
  int i, alive;
  struct context_set* cs = global_ptr_begin_update(&all_contexts);
  
  alive = 0;
  for (i = 0; i<cs->ncontexts; i++) {
    if (cs->contexts[i] != self) {
      cs->contexts[alive++] = cs->contexts[i];
    }
  }
  cs->ncontexts = alive;
  
  if (!alive) {
    free(cs->contexts);
    free(cs);
    cs = 0;
  }

  fprintf(stderr, "Context %d shut down\n", (int)self->id);

  global_ptr_commit_update(&all_contexts, cs);
  message_poll();
  pthread_mutex_unlock(&self->awake_lock);
  store_release(&self->is_shutdown, 1);
  self = 0;
}

void context_sleep() {
  message_poll();
  store_release(&self->is_asleep, 1);
  pthread_mutex_unlock(&self->awake_lock);
}

void context_wake() {
  pthread_mutex_lock(&self->awake_lock);
  store_release(&self->is_asleep, 0);
  message_poll();
}

void await_shutdown(struct context* c) {
  void* ret;
  message_poll_until(&c->is_started, 1);
  message_poll_until(&c->is_shutdown, 1);
  pthread_join(c->thread, &ret);
  free(c);
}




static struct {
  atomic_word generation, new_generation;
  struct context* stopper; // just used for asserts
} world_stopper = {0,0};


void caml_stop_the_world() {
  message_poll();
  Assert (world_stopper.stopper == 0);
  fprintf(stderr, "Context %d stopping the world...\n", self->id);
  atomic_word my_gen = fetch_and_increment(&world_stopper.new_generation);
  message_poll_until(&world_stopper.generation, my_gen);
  world_stopper.stopper = self;
  message_broadcast_sync_waking(msg_await_counter(&world_stopper.generation, my_gen + 1));
  fprintf(stderr, "... context %d has stopped the world\n", self->id);
}

void caml_resume_the_world() {
  Assert (world_stopper.stopper == self);
  fprintf(stderr, "Context %d resuming the world...\n", self->id);
  world_stopper.stopper = 0;
  store_release(&world_stopper.generation,
                load_acquire(&world_stopper.generation) + 1);
  fprintf(stderr, "... context %d has resumed the world\n", self->id);
  message_poll();
}


static void* thread_func(void* p) {
  start_context(p);
  fprintf(stderr, "Context %d running\n", self->id);
  caml_run_context(self->main_func);
  shutdown_context();
  return 0;
}


void caml_init_main_context() {
  start_context(new_context());
}

void caml_setup_main_context() {
  setup_context_caml_val(self);
}

CAMLprim value caml_context_create(value main_func) 
{
  pthread_t th;
  struct context* c = new_context();
  setup_context_caml_val(c);
  c->main_func = caml_globalize(main_func);
  Assert (Is_global_val(c->main_func));
  pthread_create(&th, 0, &thread_func, c);
  return c->caml_self;
}

CAMLprim value caml_context_self(value unit) {
  return self->caml_self;
}

CAMLprim value caml_context_get_id(value ctx) {
  struct context* c = (struct context*)Field(ctx, 0);
  return Val_long(c->id);
}

static void action_callback(struct context* c, struct message* m) {
  caml_callback_exn(m->camlval, Val_unit);
}
struct message msg_callback(value cb) {
  struct message m = {&action_callback, 0, 0, cb};
  return m;
}

CAMLprim value caml_context_interrupt(value ctx, value callback) {
  struct context* c = (struct context*)Field(ctx, 0);
  callback = caml_globalize(callback);
  message_send(c, msg_callback(callback));
  return Val_unit;
}
