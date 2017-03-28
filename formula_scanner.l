%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "formula_parser.h"
%}

ws        [ \t\n]+
comment   %.*
operator  [\.()&|\[\]<>]
ident     [_a-z0-9]+
var       [A-Z]

%%

{ws}		;
{comment}	;
"true"		{ return TRUE; }
"false"		{ return FALSE; }
"mu"		{ return MU; }
"nu"		{ return NU; }
{operator}	{ return yytext[0]; }
{ident}		{ yylval.ident = strdup(yytext); return ACTION; }
{var}		{ yylval.var = yytext[0] - 'A'; return X; }

%%
