#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#define __USE_POSIX199506 /* for getc_unlocked */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formula.h"

/*** options ***/
static int x, verbose, Emerson_Lei = 1, print_states, no_stats, threads = 1;

/*** LTS ***/
static int initial_state, nr_of_transitions, nr_of_states, nr_of_labels;
static int labels_size;
static char *labels = NULL;
typedef struct { int s, t; } Transition;
static Transition **transitions;
static int *transition_counts;

int label_id(char *name) {
  int i = 0, o = 0;
  while (labels && o < labels_size && labels[o]) {
    if (strcmp(name, &labels[o]) == 0)
      return i;
    i++;
    o += strlen(&labels[o]) + 1;
  }
  labels_size += (strlen(name) + 1);
  labels = realloc(labels, labels_size * sizeof(*labels));
  strcpy(labels + o, name);
  nr_of_labels++;
  return i;
}

char *label_name(int id) {
  char *l = labels;
  while (l && id--)
    l += strlen(l) + 1;
  return l;
}

void read_lts(FILE *file) {
  int s = 0, l, t = 0, e, _s, *sbegin, *send, *edges, state, c, i;
  fscanf(file, "des (%i,%i,%i)", &initial_state, &nr_of_transitions,
         &nr_of_states);
  while (getc_unlocked(file) == ' ')
    ;
  edges = malloc(2 * nr_of_transitions * sizeof(*edges));
  char label[5000];
  sbegin = malloc(nr_of_states * sizeof(*sbegin));
  memset(sbegin, -1, nr_of_states * sizeof(*sbegin));
  send = malloc(nr_of_states * sizeof(*send));
  for (e = 0, _s = -1; e < 2 * nr_of_transitions; e += 2) {
    state = 0;
    while ((c = getc_unlocked(file)) != '\n') {
      switch (state) {
      default:
        assert(0);
      case 0:
        assert(c == '('), s = 0, i = 0, t = 0, state = 1;
        break;
      case 1:
        if (isdigit(c))
          s = 10 * s + (c - '0');
        else
          assert(c == ','), state = 2;
        break;
      case 2:
        assert(c == '"'), state = 3;
        break;
      case 3:
        if (c == '"')
          label[i] = '\0', state = 4;
        else
          label[i++] = c;
        break;
      case 4:
        assert(c == ','), state = 5;
        break;
      case 5:
        if (isdigit(c))
          t = 10 * t + (c - '0');
        else
          assert(c == ')');
        break;
      }
    }
    l = label_id(label);
    edges[e] = l, edges[e + 1] = t;
    if (s != _s) {
      sbegin[s] = e;
      if (_s >= 0)
        send[_s] = e;
      _s = s;
    }
  }
  send[s] = e;
  transition_counts = calloc(1, nr_of_labels * sizeof(*transition_counts));
  transitions = malloc(nr_of_labels * sizeof(*transitions));
  for (e = 0; e < 2 * nr_of_transitions; e += 2) {
    transition_counts[edges[e]]++;
  }
  int li[nr_of_labels];
  memset(li, 0, sizeof(li));
  for (l = 0; l < nr_of_labels; l++)
    transitions[l] = malloc(transition_counts[l] * sizeof(*transitions[l]));
  for (s = 0; s < nr_of_states; s++) {
    e = sbegin[s];
    if (e == -1)
      continue;
    for (; e < send[s]; e += 2) {
      l = edges[e], t = edges[e + 1];
      Transition *act = &transitions[l][li[l]++];
      act->s = s;
      act->t = t;
    }
  }
  free(edges), free(sbegin), free(send);
}

/*** state set ***/
#define SB (nr_of_states * sizeof(char))
static Set False, True;

static Set s_new() { return malloc(SB); }
static int s_get(Set set, int s) { return set[s]; }
static void s_set(Set set, int s, int v) { set[s] = v; }
static void s_reset(Set set, int v) { memset(set, v, SB); }
static void s_copy(Set dst, Set src) { memcpy(dst, src, SB); }
static int s_equal(Set a, Set b) { return !memcmp(a, b, SB); }
static int s_identical(Set a, Set b) { return a == b; }
static void s_delete(Set set) { free(set); }
static void s_print(Set set) {
  for (int s = 0; s < nr_of_states; s++)
    if (s_get(set, s))
      printf("%d\n", s);
}

/*** concurrency ***/
typedef struct Worker {
  pthread_t thread;
  Formula *f;
  struct Worker *parent;
  int evalcount[9], lfp_iterations, gfp_iterations;
} Worker;

Worker *workers;
static int active_workers = 1;

static void eval(Worker *W, Formula *f);
static void *w_eval(void *arg) {
  Worker *W = arg;
  memset(W->evalcount, 0, sizeof(W->evalcount));
  memset(&W->lfp_iterations, 0, sizeof(W->lfp_iterations));
  memset(&W->gfp_iterations, 0, sizeof(W->gfp_iterations));
  { eval(W, W->f); }
  for (int i = 0; i < sizeof(W->evalcount) / sizeof(*W->evalcount); i++)
    __sync_fetch_and_add(&W->parent->evalcount[i], W->evalcount[i]);
  __sync_fetch_and_add(&W->parent->lfp_iterations, W->lfp_iterations);
  __sync_fetch_and_add(&W->parent->gfp_iterations, W->gfp_iterations);
  return NULL;
}

/*** formula ***/
static Formula *A[26];       /* points to the node that binds this variable */
static Formula **f_preorder; /* neatly sorted */
int f_size = 0;

int isopen(Formula *f) { return f->nr_of_deps; }
int isconst(Formula *f) { return f->type <= TTrue; }
int isvar(Formula *f) { return f->type >= TVar && f->type <= TNu; }
int isop(Formula *f) { return f->type >= TAnd; }
int isbinop(Formula *f) { return f->type == TAnd || f->type == TOr; }
int isnode(Formula *f) { return f->type >= TMu; }
int isbinder(Formula *f) { return f->type == TMu || f->type == TNu; }
int ismodal(Formula *f) { return f->type == TDiamond || f->type == TBracket; }

static void f_init_preorder(Formula *f, int sb) {
  static int id = 0;
  f_preorder[id++] = f;
  f->child_begin = id;
  f->surrounding_binder = sb;
  if (isbinder(f))
    sb = f->type;
  if (isnode(f))
    f_init_preorder(f->f, sb);
  if (isbinop(f))
    f_init_preorder(f->g, sb);
  f->child_end = id;
}

static void f_deps(Formula *target, Formula *f, char bindings[26]) {
  if (isbinder(f)) {
    char copy[26];
    memcpy(copy, bindings, sizeof(copy));
    copy[f->var] = 1;
    f_deps(target, f->f, copy);
  } else if (f->type == TVar && !bindings[f->var]) {
    target->deps[f->var] = 1;
    target->nr_of_deps++;
  } else if (isnode(f))
    f_deps(target, f->f, bindings);
  if (isbinop(f))
    f_deps(target, f->g, bindings);
}

static void f_init(Formula *f) {
  False = s_new(), s_reset(False, 0);
  True = s_new(), s_reset(True, 1);
  // wtf
  f_preorder = malloc((2 + f_size) * sizeof(*f_preorder));
  f_init_preorder(f, 0);
  char bindings[26];
  for (int i = 0; i < f_size; i++) {
    f = f_preorder[i];
    if (ismodal(f)) {
      char *label = f->label;
      f->l = label_id(label);
      free(label);
    }
    f->nr_of_deps = 0;
    f->valid = 0;
    if (isbinder(f))
      f->value = s_new(), s_reset(f->value, f->type == TNu), A[f->var] = f;
    else if (isvar(f))
      f->value = A[f->var]->value, f->valid = 1;
    else if (isop(f))
      f->value = s_new();
    memset(bindings, 0, sizeof(bindings));
    memset(f->deps, 0, sizeof(f->deps));
    f_deps(f, f, bindings);
  }
}

#define for_each_child_of(parent, i, child)                                    \
  for (child = f_preorder[i = parent->child_begin]; i < parent->child_end;     \
       child = f_preorder[++i])

/*** algorithm ***/
static void eval(Worker *W, Formula *f) {
  if (f->valid)
    return;
  W->evalcount[f->type]++;
  switch (f->type) {
  case TFalse:
    f->value = False;
    break;
  case TTrue:
    f->value = True;
    break;
  case TAnd:
  case TOr: {
    int w = __sync_fetch_and_add(&active_workers, 1);
    if (w >= threads) {
      __sync_fetch_and_add(&active_workers, -1);
      eval(W, f->f);
      eval(W, f->g);
    } else {
      Worker *C = &workers[w];
      C->parent = W, C->f = f->f;
      pthread_create(&C->thread, NULL, w_eval, C);
      eval(W, f->g);
      pthread_join(C->thread, NULL);
      __sync_fetch_and_add(&active_workers, -1);
    }
    if (f->type == TAnd)
      for (int i = 0; i < nr_of_states; i++)
        s_set(f->value, i, s_get(f->f->value, i) && s_get(f->g->value, i));
    else
      for (int i = 0; i < nr_of_states; i++)
        s_set(f->value, i, s_get(f->f->value, i) || s_get(f->g->value, i));
    break;
  }
  case TDiamond:
  case TBracket: {
    int bracket = f->type == TBracket;
    eval(W, f->f);
    s_reset(f->value, bracket);
    for (int i = 0; i < transition_counts[f->l]; i++) {
      int s = transitions[f->l][i].s, t = transitions[f->l][i].t;
      if (bracket ^ s_get(f->f->value, t))
        s_set(f->value, s, !bracket);
    }
    break;
  }
  case TMu:
  case TNu: {
    int i;
    Formula *child;
    if (Emerson_Lei) {
      int other = f->type == TMu ? TNu : TMu;
      if (f->surrounding_binder == other)
        for_each_child_of(f, i,
                          child) if (child->type == f->type && isopen(child))
            s_reset(child->value, child->type == TNu);
    }
    int done, mu = f->type == TMu;
    int *iterations = mu ? &W->lfp_iterations : &W->gfp_iterations;
    do {
      (*iterations)++;
      eval(W, f->f);
      done = s_equal(f->f->value, f->value);
      if (!s_identical(f->value, f->f->value))
        s_copy(f->value, f->f->value);
      if (!Emerson_Lei)
        for_each_child_of(f, i,
                          child) if (isbinder(child) && child->deps[f->var])
            s_reset(child->value, child->type == TNu);
    } while (!done);
    break;
  }
  };
  if (verbose) {
    f_print(f);
    if (verbose > 1) {
      printf("states: ");
      for (int i = 0; i < nr_of_states; i++)
        if (s_get(f->value, i))
          printf("%d ", i);
      printf("\n");
    }
  }
  if (Emerson_Lei && !isopen(f))
    f->valid = 1;
}

/*** main ***/
static void cleanup() {
  free(labels);
  free(transition_counts);
  for (int i = 0; i < nr_of_labels; i++)
    free(transitions[i]);
  s_delete(False), s_delete(True);
  for (int i = 0; i < f_size; i++) {
    Formula *f = f_preorder[i];
    if (isnode(f))
      s_delete(f->value);
    free(f);
  }
  free(f_preorder);
}

static void usage(char *argv[], int full) {
  printf("usage: %s [-h] [-v] [-a (emersonlei|naive)] [-s] [-i] lts.aut "
         "formula.mcf\n",
         argv[0]);
  if (full)
    printf("\n"
           "options:\n"
           "    -h                            show this very help message\n"
           "    -v | -V                       verbose mode\n"
           "    -a --algorithm <algorithm>    select checking algorithm\n"
           "    -s --print-states             output each satisfying state on "
           "a separate line\n"
           "    -n --no-stats                 do not print most stats\n"
           "\n");
  exit(1);
}

int main(int argc, char *argv[]) {
  int i = 1, count = 0, initial_state_satisfies_f;
  char c, *arg;
  FILE *formulafile, *ltsfile;
  while (i < argc && argv[i][0] == '-') {
    arg = argv[i];
    c = arg[1];
    if (c == 'h')
      usage(argv, 1);
    else if (c == 'v' || c == 'V')
      verbose = 1 + (c == 'V');
    else if (c == 'a' || !strcmp(arg, "--algorithm"))
      Emerson_Lei = strcmp(argv[++i], "naive");
    else if (c == 's' || !strcmp(arg, "--print-states"))
      print_states = 1;
    else if (c == 'n' || !strcmp(arg, "--no-stats"))
      no_stats = 1;
    else if (c == 't' || !strcmp(arg, "--threads"))
      threads = atoi(argv[++i]);
    else if (c == 'x')
      x = 1;
    i++;
  }
  if (i + 1 >= argc)
    usage(argv, 0);
  if (!(formulafile = fopen(argv[i + 1], "r"))) {
    printf("*** error: cannot open '%s'\n", argv[i + 1]);
    usage(argv, 0);
  }
  Formula *f;
  f_parse(formulafile, &f);
  if (!(ltsfile = fopen(argv[i], "r"))) {
    printf("*** error: cannot open '%s'\n", argv[i]);
    usage(argv, 0);
  }
  read_lts(ltsfile);

  workers = calloc(1, threads * sizeof(*workers));
  f_init(f);
  Worker *mc = &workers[0];
  eval(mc, f);
  if (print_states)
    s_print(f->value);
  if (!no_stats) {
#define T(name, val, sym)                                                      \
  printf("evalcount_%s = %d\n", sym, mc->evalcount[val]);
#include "formulatype.h"
#undef T
    printf("lfp_iterations = %d\n", mc->lfp_iterations);
    printf("gfp_iterations = %d\n", mc->gfp_iterations);
  }
  for (i = 0; i < nr_of_states; i++)
    count += s_get(f->value, i);
  printf("nr_of_satisfying_states = %d\n", count);
  initial_state_satisfies_f = s_get(f->value, initial_state);
  printf("initial_state_satisfies_f = %d\n", initial_state_satisfies_f);
  cleanup();
  return initial_state_satisfies_f;
}
