enum FormulaType {
#define T(name, val, sym) name = val,
#include "formulatype.h"
#undef T
};

typedef char *Set;

typedef struct Formula {
  int type, surrounding_binder, nr_of_deps, valid, child_begin, child_end;
  struct Formula *f;
  Set value;
  union {
    /* binary operations */
    struct Formula *g;
    /* temporal */
    char *label; /* transition label */
    int l;       /* label id */
    /* fixpoints + variables */
    int var;
  };
  int deps[26];
} Formula;

int f_parse(FILE *file, Formula **f);
void f_print(Formula *f);
