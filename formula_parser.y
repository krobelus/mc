%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formula.h"

static Formula *f_type(int type);
static Formula *f_var(char var);
static Formula *f_binary(int type, Formula *f, Formula *g);
static Formula *f_modal(int type, char *label, Formula *f);
static Formula *f_fixpoint(int type, char var, Formula *f);

extern int f_size;
extern char *(label_name)(int id);

extern int yylex();

void yyerror(void **f, char *s)
{
  fprintf(stderr, "error: %s\n", s);
  exit(1);
}

%}

%parse-param { void **formula };

%union {
  char  *ident;
  int var;
  void *formula;
}

%token <ident>   ACTION;
%token <var>     X;
%token           FALSE;
%token           TRUE;
%token           MU;
%token           NU;

%type <formula> formula;
%%

grammar: formula { *formula = $1; } ;

formula:
    FALSE       { f_size++; $$ = f_type(TFalse); }
    | TRUE      { f_size++; $$ = f_type(TTrue); }
    | X         { f_size++; $$ = f_type(TVar); ((Formula*)$$)->var = $1; }
    | '(' formula '&' '&' formula ')' { f_size++; $$ = f_binary(TAnd, $2, $5); }
    | '(' formula '|' '|' formula ')' { f_size++; $$ = f_binary(TOr, $2, $5); }
    | '<' ACTION '>' formula { f_size++; $$ = f_modal(TDiamond, $2, $4); }
    | '[' ACTION ']' formula { f_size++; $$ = f_modal(TBracket, $2, $4); }
    | MU X '.' formula       { f_size++; $$ = f_fixpoint(TMu, $2, $4); }
    | NU X '.' formula       { f_size++; $$ = f_fixpoint(TNu, $2, $4); }
    ;


%%

extern FILE *yyin;

int f_parse(FILE *fin, Formula **f)
{
  yyin = fin;
  yyparse((void *)f);
  fclose(yyin);
  return 0;
}

Formula *f_type(int type) {
  Formula *_f = malloc(sizeof(Formula));
  _f->type = type;
  return _f;
}

Formula *f_var(char var) {
  Formula *_f = f_type(TVar);
  _f->var = var;
  return _f;
}

Formula *f_binary(int type, Formula *f, Formula *g) {
  Formula *_f = f_type(type);
  _f->f = f;
  _f->g = g;
  return _f;
}

Formula *f_modal(int type, char *label, Formula *f) {
  Formula *_f = f_type(type);
  _f->label = label;
  _f->f = f;
  return _f;
}

Formula *f_fixpoint(int type, char var, Formula *f) {
  Formula *_f = f_type(type);
  _f->var = var;
  _f->f = f;
  return _f;
}

void _f_print(Formula *f) {
  switch (f->type) {
  case TFalse:
    printf("false");
    break;
  case TTrue:
    printf("true");
    break;
  case TVar:
    printf("%c", f->var + 'A');
    break;
  case TAnd:
    printf("(");
    _f_print(f->f);
    printf("&&");
    _f_print(f->g);
    printf(")");
    break;
  case TOr:
    printf("(");
    _f_print(f->f);
    printf("||");
    _f_print(f->g);
    printf(")");
    break;
  case TDiamond:
    printf("<%s>", label_name(f->l));
    _f_print(f->f);
    break;
  case TBracket:
    printf("[%s]", label_name(f->l));
    _f_print(f->f);
    break;
  case TMu:
    printf("mu %c. ", f->var + 'A');
    _f_print(f->f);
    break;
  case TNu:
    printf("nu %c. ", f->var + 'A');
    _f_print(f->f);
    break;
  };
}

void f_print(Formula *f) {
  _f_print(f);
  printf("\n");
}
