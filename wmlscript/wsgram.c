
/*  A Bison parser, made from wmlscript/wsgram.y
    by GNU Bison version 1.28  */

#define YYBISON 1  /* Identify Bison output.  */

#define yyparse ws_yy_parse
#define yylex ws_yy_lex
#define yyerror ws_yy_error
#define yylval ws_yy_lval
#define yychar ws_yy_char
#define yydebug ws_yy_debug
#define yynerrs ws_yy_nerrs
#define YYLSP_NEEDED

#define	tINVALID	257
#define	tTRUE	258
#define	tFALSE	259
#define	tINTEGER	260
#define	tFLOAT	261
#define	tSTRING	262
#define	tIDENTIFIER	263
#define	tACCESS	264
#define	tAGENT	265
#define	tBREAK	266
#define	tCONTINUE	267
#define	tIDIV	268
#define	tIDIVA	269
#define	tDOMAIN	270
#define	tELSE	271
#define	tEQUIV	272
#define	tEXTERN	273
#define	tFOR	274
#define	tFUNCTION	275
#define	tHEADER	276
#define	tHTTP	277
#define	tIF	278
#define	tISVALID	279
#define	tMETA	280
#define	tNAME	281
#define	tPATH	282
#define	tRETURN	283
#define	tTYPEOF	284
#define	tUSE	285
#define	tUSER	286
#define	tVAR	287
#define	tWHILE	288
#define	tURL	289
#define	tDELETE	290
#define	tIN	291
#define	tLIB	292
#define	tNEW	293
#define	tNULL	294
#define	tTHIS	295
#define	tVOID	296
#define	tWITH	297
#define	tCASE	298
#define	tCATCH	299
#define	tCLASS	300
#define	tCONST	301
#define	tDEBUGGER	302
#define	tDEFAULT	303
#define	tDO	304
#define	tENUM	305
#define	tEXPORT	306
#define	tEXTENDS	307
#define	tFINALLY	308
#define	tIMPORT	309
#define	tPRIVATE	310
#define	tPUBLIC	311
#define	tSIZEOF	312
#define	tSTRUCT	313
#define	tSUPER	314
#define	tSWITCH	315
#define	tTHROW	316
#define	tTRY	317
#define	tEQ	318
#define	tLE	319
#define	tGE	320
#define	tNE	321
#define	tAND	322
#define	tOR	323
#define	tPLUSPLUS	324
#define	tMINUSMINUS	325
#define	tLSHIFT	326
#define	tRSSHIFT	327
#define	tRSZSHIFT	328
#define	tADDA	329
#define	tSUBA	330
#define	tMULA	331
#define	tDIVA	332
#define	tANDA	333
#define	tORA	334
#define	tXORA	335
#define	tREMA	336
#define	tLSHIFTA	337
#define	tRSSHIFTA	338
#define	tRSZSHIFTA	339

#line 1 "wmlscript/wsgram.y"

/*
 *
 * wsgram.y
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Bison grammar for the WMLScript compiler.
 *
 */

#include "wmlscript/wsint.h"

#define YYPARSE_PARAM	pctx
#define YYLEX_PARAM	pctx

/* The required yyerror() function.  This is actually not used but to
   report the internal parser errors.  All other errors are reported
   by using the `wserror.h' functions. */
extern void yyerror(char *msg);

#if WS_DEBUG
/* Just for debugging purposes. */
WsCompilerPtr global_compiler = NULL;
#endif /* WS_DEBUG */


#line 33 "wmlscript/wsgram.y"
typedef union
{
    WsUInt32 integer;
    WsFloat vfloat;
    char *identifier;
    WsUtf8String *string;

    WsBool boolean;
    WsList *list;
    WsPair *pair;

    WsPragmaMetaBody *meta_body;

    WsStatement *stmt;
    WsExpression *expr;
} YYSTYPE;

#ifndef YYLTYPE
typedef
  struct yyltype
    {
      int timestamp;
      int first_line;
      int first_column;
      int last_line;
      int last_column;
      char *text;
   }
  yyltype;

#define YYLTYPE yyltype
#endif

#include <stdio.h>

#ifndef __cplusplus
#ifndef __STDC__
#define const
#endif
#endif



#define	YYFINAL		257
#define	YYFLAG		-32768
#define	YYNTBASE	109

#define YYTRANSLATE(x) ((unsigned)(x) <= 339 ? yytranslate[x] : 163)

static const char yytranslate[] = {     0,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,   106,     2,   107,     2,   104,    97,     2,    87,
    88,   102,   100,    89,   101,   108,   103,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,    94,    86,    98,
    92,    99,    93,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,    96,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,    90,    95,    91,   105,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     1,     3,     4,     5,     6,
     7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
    17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
    27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
    37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
    47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
    57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
    67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
    77,    78,    79,    80,    81,    82,    83,    84,    85
};

#if YYDEBUG != 0
static const short yyprhs[] = {     0,
     0,     3,     5,     7,     9,    12,    16,    18,    20,    22,
    24,    28,    31,    34,    37,    42,    45,    47,    49,    51,
    54,    58,    62,    65,    69,    71,    73,    75,    77,    80,
    89,    90,    92,    93,    95,    96,    98,   100,   104,   106,
   108,   110,   113,   115,   117,   120,   123,   125,   129,   131,
   132,   134,   136,   139,   143,   146,   148,   152,   155,   156,
   159,   167,   173,   179,   181,   191,   202,   206,   207,   209,
   211,   215,   217,   221,   225,   229,   233,   237,   241,   245,
   249,   253,   257,   261,   265,   269,   271,   277,   279,   283,
   285,   289,   291,   295,   297,   301,   303,   307,   309,   313,
   317,   319,   323,   327,   331,   335,   337,   341,   345,   349,
   351,   355,   359,   361,   365,   369,   373,   377,   379,   382,
   385,   388,   391,   394,   397,   400,   403,   405,   408,   411,
   413,   416,   421,   426,   428,   430,   432,   434,   436,   438,
   440,   444,   447,   451,   453
};

static const short yyrhs[] = {   110,
   125,     0,   125,     0,     1,     0,   111,     0,   110,   111,
     0,    31,   112,    86,     0,     1,     0,   113,     0,   114,
     0,   116,     0,    35,     9,     8,     0,    10,   115,     0,
    16,     8,     0,    28,     8,     0,    16,     8,    28,     8,
     0,    26,   117,     0,   118,     0,   119,     0,   120,     0,
    27,   121,     0,    23,    18,   121,     0,    32,    11,   121,
     0,   122,   123,     0,   122,   123,   124,     0,     8,     0,
     8,     0,     8,     0,   126,     0,   125,   126,     0,   127,
    21,     9,    87,   128,    88,   132,   129,     0,     0,    19,
     0,     0,   130,     0,     0,    86,     0,     9,     0,   130,
    89,     9,     0,   132,     0,   135,     0,    86,     0,   144,
    86,     0,   139,     0,   140,     0,    13,    86,     0,    12,
    86,     0,   142,     0,    90,   133,    91,     0,     1,     0,
     0,   134,     0,   131,     0,   134,   131,     0,    33,   136,
    86,     0,    33,     1,     0,   137,     0,   136,    89,   137,
     0,     9,   138,     0,     0,    92,   146,     0,    24,    87,
   144,    88,   131,    17,   131,     0,    24,    87,   144,    88,
   131,     0,    34,    87,   144,    88,   131,     0,   141,     0,
    20,    87,   143,    86,   143,    86,   143,    88,   131,     0,
    20,    87,    33,   136,    86,   143,    86,   143,    88,   131,
     0,    29,   143,    86,     0,     0,   144,     0,   145,     0,
   144,    89,   145,     0,   146,     0,     9,    92,   145,     0,
     9,    77,   145,     0,     9,    78,   145,     0,     9,    82,
   145,     0,     9,    75,   145,     0,     9,    76,   145,     0,
     9,    83,   145,     0,     9,    84,   145,     0,     9,    85,
   145,     0,     9,    79,   145,     0,     9,    81,   145,     0,
     9,    80,   145,     0,     9,    15,   145,     0,   147,     0,
   147,    93,   145,    94,   145,     0,   148,     0,   147,    69,
   148,     0,   149,     0,   148,    68,   149,     0,   150,     0,
   149,    95,   150,     0,   151,     0,   150,    96,   151,     0,
   152,     0,   151,    97,   152,     0,   153,     0,   152,    64,
   153,     0,   152,    67,   153,     0,   154,     0,   153,    98,
   154,     0,   153,    99,   154,     0,   153,    65,   154,     0,
   153,    66,   154,     0,   155,     0,   154,    72,   155,     0,
   154,    73,   155,     0,   154,    74,   155,     0,   156,     0,
   155,   100,   156,     0,   155,   101,   156,     0,   157,     0,
   156,   102,   157,     0,   156,   103,   157,     0,   156,    14,
   157,     0,   156,   104,   157,     0,   158,     0,    30,   157,
     0,    25,   157,     0,    70,     9,     0,    71,     9,     0,
   100,   157,     0,   101,   157,     0,   105,   157,     0,   106,
   157,     0,   159,     0,     9,    70,     0,     9,    71,     0,
   160,     0,     9,   161,     0,     9,   107,     9,   161,     0,
     9,   108,     9,   161,     0,     9,     0,     3,     0,     4,
     0,     5,     0,     6,     0,     7,     0,     8,     0,    87,
   144,    88,     0,    87,    88,     0,    87,   162,    88,     0,
   145,     0,   162,    89,   145,     0
};

#endif

#if YYDEBUG != 0
static const short yyrline[] = { 0,
   120,   122,   123,   129,   131,   134,   136,   140,   142,   143,
   146,   151,   155,   166,   177,   199,   203,   205,   206,   209,
   227,   247,   282,   285,   289,   290,   291,   295,   297,   300,
   315,   317,   320,   323,   326,   328,   331,   348,   366,   375,
   376,   378,   380,   381,   382,   384,   386,   389,   397,   404,
   407,   410,   416,   420,   423,   427,   433,   437,   452,   455,
   459,   462,   466,   469,   472,   476,   481,   488,   490,   494,
   496,   500,   502,   504,   506,   508,   510,   512,   514,   516,
   518,   520,   522,   524,   526,   530,   532,   536,   538,   542,
   544,   548,   550,   554,   556,   560,   562,   566,   568,   570,
   574,   576,   578,   580,   582,   586,   588,   590,   592,   596,
   598,   600,   604,   606,   608,   610,   612,   616,   618,   620,
   622,   624,   626,   642,   644,   646,   650,   652,   654,   658,
   660,   670,   672,   676,   679,   681,   683,   685,   687,   689,
   691,   695,   698,   702,   708
};
#endif


#if YYDEBUG != 0 || defined (YYERROR_VERBOSE)

static const char * const yytname[] = {   "$","error","$undefined.","tINVALID",
"tTRUE","tFALSE","tINTEGER","tFLOAT","tSTRING","tIDENTIFIER","tACCESS","tAGENT",
"tBREAK","tCONTINUE","tIDIV","tIDIVA","tDOMAIN","tELSE","tEQUIV","tEXTERN","tFOR",
"tFUNCTION","tHEADER","tHTTP","tIF","tISVALID","tMETA","tNAME","tPATH","tRETURN",
"tTYPEOF","tUSE","tUSER","tVAR","tWHILE","tURL","tDELETE","tIN","tLIB","tNEW",
"tNULL","tTHIS","tVOID","tWITH","tCASE","tCATCH","tCLASS","tCONST","tDEBUGGER",
"tDEFAULT","tDO","tENUM","tEXPORT","tEXTENDS","tFINALLY","tIMPORT","tPRIVATE",
"tPUBLIC","tSIZEOF","tSTRUCT","tSUPER","tSWITCH","tTHROW","tTRY","tEQ","tLE",
"tGE","tNE","tAND","tOR","tPLUSPLUS","tMINUSMINUS","tLSHIFT","tRSSHIFT","tRSZSHIFT",
"tADDA","tSUBA","tMULA","tDIVA","tANDA","tORA","tXORA","tREMA","tLSHIFTA","tRSSHIFTA",
"tRSZSHIFTA","';'","'('","')'","','","'{'","'}'","'='","'?'","':'","'|'","'^'",
"'&'","'<'","'>'","'+'","'-'","'*'","'/'","'%'","'~'","'!'","'#'","'.'","CompilationUnit",
"Pragmas","Pragma","PragmaDeclaration","ExternalCompilationUnitPragma","AccessControlPragma",
"AccessControlSpecifier","MetaPragma","MetaSpecifier","MetaName","MetaHttpEquiv",
"MetaUserAgent","MetaBody","MetaPropertyName","MetaContent","MetaScheme","FunctionDeclarations",
"FunctionDeclaration","ExternOpt","FormalParameterListOpt","SemicolonOpt","FormalParameterList",
"Statement","Block","StatementListOpt","StatementList","VariableStatement","VariableDeclarationList",
"VariableDeclaration","VariableInitializedOpt","IfStatement","IterationStatement",
"ForStatement","ReturnStatement","ExpressionOpt","Expression","AssignmentExpression",
"ConditionalExpression","LogicalORExpression","LogicalANDExpression","BitwiseORExpression",
"BitwiseXORExpression","BitwiseANDExpression","EqualityExpression","RelationalExpression",
"ShiftExpression","AdditiveExpression","MultiplicativeExpression","UnaryExpression",
"PostfixExpression","CallExpression","PrimaryExpression","Arguments","ArgumentList", NULL
};
#endif

static const short yyr1[] = {     0,
   109,   109,   109,   110,   110,   111,   111,   112,   112,   112,
   113,   114,   115,   115,   115,   116,   117,   117,   117,   118,
   119,   120,   121,   121,   122,   123,   124,   125,   125,   126,
   127,   127,   128,   128,   129,   129,   130,   130,   131,   131,
   131,   131,   131,   131,   131,   131,   131,   132,   132,   133,
   133,   134,   134,   135,   135,   136,   136,   137,   138,   138,
   139,   139,   140,   140,   141,   141,   142,   143,   143,   144,
   144,   145,   145,   145,   145,   145,   145,   145,   145,   145,
   145,   145,   145,   145,   145,   146,   146,   147,   147,   148,
   148,   149,   149,   150,   150,   151,   151,   152,   152,   152,
   153,   153,   153,   153,   153,   154,   154,   154,   154,   155,
   155,   155,   156,   156,   156,   156,   156,   157,   157,   157,
   157,   157,   157,   157,   157,   157,   158,   158,   158,   159,
   159,   159,   159,   160,   160,   160,   160,   160,   160,   160,
   160,   161,   161,   162,   162
};

static const short yyr2[] = {     0,
     2,     1,     1,     1,     2,     3,     1,     1,     1,     1,
     3,     2,     2,     2,     4,     2,     1,     1,     1,     2,
     3,     3,     2,     3,     1,     1,     1,     1,     2,     8,
     0,     1,     0,     1,     0,     1,     1,     3,     1,     1,
     1,     2,     1,     1,     2,     2,     1,     3,     1,     0,
     1,     1,     2,     3,     2,     1,     3,     2,     0,     2,
     7,     5,     5,     1,     9,    10,     3,     0,     1,     1,
     3,     1,     3,     3,     3,     3,     3,     3,     3,     3,
     3,     3,     3,     3,     3,     1,     5,     1,     3,     1,
     3,     1,     3,     1,     3,     1,     3,     1,     3,     3,
     1,     3,     3,     3,     3,     1,     3,     3,     3,     1,
     3,     3,     1,     3,     3,     3,     3,     1,     2,     2,
     2,     2,     2,     2,     2,     2,     1,     2,     2,     1,
     2,     4,     4,     1,     1,     1,     1,     1,     1,     1,
     3,     2,     3,     1,     3
};

static const short yydefact[] = {     0,
     7,    32,     0,     0,     4,     2,    28,     0,     0,     0,
     0,     0,     8,     9,    10,     7,     5,     1,    29,     0,
     0,     0,    12,     0,     0,     0,    16,    17,    18,    19,
     0,     6,     0,    13,    14,     0,    25,    20,     0,     0,
    11,    33,     0,    21,    26,    23,    22,    37,     0,    34,
    15,    27,    24,     0,     0,    49,     0,    35,    38,   135,
   136,   137,   138,   139,   140,   134,     0,     0,     0,     0,
     0,    68,     0,     0,     0,     0,     0,    41,     0,     0,
     0,     0,     0,    52,    39,     0,     0,    40,    43,    44,
    64,    47,     0,    70,    72,    86,    88,    90,    92,    94,
    96,    98,   101,   106,   110,   113,   118,   127,   130,    36,
    30,     0,   128,   129,     0,     0,     0,     0,     0,     0,
     0,     0,     0,     0,     0,     0,     0,     0,     0,   131,
    46,    45,    68,     0,   134,   120,     0,    69,   119,    55,
    59,     0,    56,     0,   121,   122,     0,   123,   124,   125,
   126,    48,    53,    42,     0,     0,     0,     0,     0,     0,
     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     0,     0,     0,     0,     0,     0,    85,    77,    78,    74,
    75,    82,    84,    83,    76,    79,    80,    81,   142,   144,
     0,    73,     0,     0,     0,     0,     0,    67,     0,    58,
    54,     0,     0,   141,    71,    89,     0,    91,    93,    95,
    97,    99,   100,   104,   105,   102,   103,   107,   108,   109,
   111,   112,   116,   114,   115,   117,   143,     0,   132,   133,
     0,    68,     0,    60,    57,     0,     0,   145,    68,     0,
    62,    63,    87,     0,    68,     0,    68,     0,    61,     0,
     0,     0,    65,    66,     0,     0,     0
};

static const short yydefgoto[] = {   255,
     4,     5,    12,    13,    14,    23,    15,    27,    28,    29,
    30,    38,    39,    46,    53,     6,     7,     8,    49,   111,
    50,    84,    85,    86,    87,    88,   142,   143,   200,    89,
    90,    91,    92,   137,    93,    94,    95,    96,    97,    98,
    99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
   109,   130,   191
};

static const short yypact[] = {    59,
    37,-32768,    28,    63,-32768,    95,-32768,    23,     8,    54,
    13,   -20,-32768,-32768,-32768,-32768,-32768,    95,-32768,    48,
    67,    75,-32768,    77,   112,   118,-32768,-32768,-32768,-32768,
   126,-32768,    62,   123,-32768,   112,-32768,-32768,   156,   112,
-32768,   163,   166,-32768,-32768,   169,-32768,-32768,   105,   107,
-32768,-32768,-32768,     1,   185,-32768,   132,   111,-32768,-32768,
-32768,-32768,-32768,-32768,-32768,   340,   115,   120,   124,   125,
   296,   308,   296,    58,   127,   189,   198,-32768,   308,   296,
   296,   296,   296,-32768,-32768,   119,   175,-32768,-32768,-32768,
-32768,-32768,   -18,-32768,-32768,   -48,   145,   121,   128,   137,
   -61,   -25,    45,     5,    44,-32768,-32768,-32768,-32768,-32768,
-32768,   308,-32768,-32768,   308,   308,   308,   308,   308,   308,
   308,   308,   308,   308,   308,   222,   308,   206,   208,-32768,
-32768,-32768,   264,   308,   -38,-32768,   134,   146,-32768,-32768,
   129,   -10,-32768,   308,-32768,-32768,    43,-32768,-32768,-32768,
-32768,-32768,-32768,-32768,   308,   296,   308,   296,   296,   296,
   296,   296,   296,   296,   296,   296,   296,   296,   296,   296,
   296,   296,   296,   296,   296,   296,-32768,-32768,-32768,-32768,
-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,
    66,-32768,   149,   149,   230,   154,    70,-32768,   296,-32768,
-32768,   230,    82,-32768,-32768,   145,   147,   121,   128,   137,
   -61,   -25,   -25,    45,    45,    45,    45,     5,     5,     5,
    44,    44,-32768,-32768,-32768,-32768,-32768,   308,-32768,-32768,
    -1,   308,    22,-32768,-32768,    22,   308,-32768,   308,   157,
   225,-32768,-32768,   158,   308,    22,   308,   160,-32768,   161,
    22,    22,-32768,-32768,   250,   251,-32768
};

static const short yypgoto[] = {-32768,
-32768,   249,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,
-32768,     3,-32768,-32768,-32768,   252,    47,-32768,-32768,-32768,
-32768,   -83,   200,-32768,-32768,-32768,    60,    55,-32768,-32768,
-32768,-32768,-32768,  -132,   -72,  -107,    61,-32768,   102,   101,
   104,   114,   103,   -52,   -63,   -44,   -29,    16,-32768,-32768,
-32768,    -8,-32768
};


#define	YYLAST		448


static const short yytable[] = {   138,
   196,    56,   162,   153,   177,   163,   147,   178,   179,   180,
   181,   182,   183,   184,   185,   186,   187,   188,   190,   192,
   156,    31,    56,    21,    60,    61,    62,    63,    64,    65,
    66,   113,   114,    67,    68,    22,    -3,     9,    44,   164,
   165,    69,    47,    20,   157,    70,    71,   205,   126,   207,
    72,    73,    19,    10,    74,    75,    33,   173,   140,     1,
   138,   197,    11,    16,    19,    32,   141,   154,   128,   129,
   155,   203,   166,   167,    34,   201,    24,     2,   202,   -31,
    25,     2,    35,   -31,   239,    26,   136,   202,   139,     3,
    57,    76,    77,     3,    36,   148,   149,   150,   151,   240,
   214,   215,   216,   217,   171,   172,   244,    78,    79,   212,
   213,    57,   248,     2,   250,   -31,   168,   169,   170,    37,
   238,    80,    81,   218,   219,   220,    82,    83,    40,   243,
   204,   155,    56,    41,    60,    61,    62,    63,    64,    65,
    66,   221,   222,    67,    68,   174,   175,   176,    42,   241,
    43,    69,   242,   227,   228,    70,    71,   233,   155,   138,
    72,    73,   249,    45,    74,    75,   138,   253,   254,   236,
   155,    48,   138,    51,   138,    56,    52,    60,    61,    62,
    63,    64,    65,    66,   229,   230,    67,    68,   223,   224,
   225,   226,    54,    59,    69,    55,   110,   145,    70,    71,
   131,    76,    77,    72,    73,   132,   146,    74,    75,   152,
   133,   134,   158,   144,   193,   159,   194,    78,    79,   198,
   199,    57,   -50,   160,    60,    61,    62,    63,    64,    65,
    66,    80,    81,   161,   155,   126,    82,    83,   141,   232,
   237,   246,   245,   247,    76,    77,    71,   251,   252,   256,
   257,    73,    17,    58,   231,    18,   235,   206,   208,   234,
    78,    79,   209,   211,    57,   -51,    60,    61,    62,    63,
    64,    65,    66,   210,    80,    81,     0,     0,     0,    82,
    83,     0,     0,     0,     0,     0,     0,     0,    71,     0,
     0,    76,    77,    73,     0,     0,   195,     0,    60,    61,
    62,    63,    64,    65,   135,     0,     0,     0,    79,   189,
    60,    61,    62,    63,    64,    65,    66,     0,     0,     0,
    71,    80,    81,     0,     0,    73,    82,    83,     0,     0,
     0,     0,    71,    76,    77,     0,     0,    73,     0,     0,
     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    79,     0,     0,     0,   112,     0,     0,     0,     0,     0,
     0,     0,     0,    80,    81,    76,    77,     0,    82,    83,
     0,     0,     0,     0,     0,     0,     0,    76,    77,     0,
     0,     0,    79,     0,     0,     0,     0,     0,     0,     0,
     0,     0,     0,     0,    79,    80,    81,     0,     0,     0,
    82,    83,     0,     0,     0,     0,     0,    80,    81,   113,
   114,     0,    82,    83,   115,   116,   117,   118,   119,   120,
   121,   122,   123,   124,   125,     0,   126,     0,     0,     0,
     0,   127,     0,     0,     0,     0,     0,     0,     0,     0,
     0,     0,     0,     0,     0,     0,   128,   129
};

static const short yycheck[] = {    72,
   133,     1,    64,    87,   112,    67,    79,   115,   116,   117,
   118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
    69,     9,     1,    16,     3,     4,     5,     6,     7,     8,
     9,    70,    71,    12,    13,    28,     0,    10,    36,    65,
    66,    20,    40,    21,    93,    24,    25,   155,    87,   157,
    29,    30,     6,    26,    33,    34,     9,    14,     1,     1,
   133,   134,    35,     1,    18,    86,     9,    86,   107,   108,
    89,   144,    98,    99,     8,    86,    23,    19,    89,    21,
    27,    19,     8,    21,    86,    32,    71,    89,    73,    31,
    90,    70,    71,    31,    18,    80,    81,    82,    83,   232,
   164,   165,   166,   167,   100,   101,   239,    86,    87,   162,
   163,    90,   245,    19,   247,    21,    72,    73,    74,     8,
   228,   100,   101,   168,   169,   170,   105,   106,    11,   237,
    88,    89,     1,     8,     3,     4,     5,     6,     7,     8,
     9,   171,   172,    12,    13,   102,   103,   104,    87,   233,
    28,    20,   236,    88,    89,    24,    25,    88,    89,   232,
    29,    30,   246,     8,    33,    34,   239,   251,   252,    88,
    89,     9,   245,     8,   247,     1,     8,     3,     4,     5,
     6,     7,     8,     9,   193,   194,    12,    13,   173,   174,
   175,   176,    88,     9,    20,    89,    86,     9,    24,    25,
    86,    70,    71,    29,    30,    86,     9,    33,    34,    91,
    87,    87,    68,    87,     9,    95,     9,    86,    87,    86,
    92,    90,    91,    96,     3,     4,     5,     6,     7,     8,
     9,   100,   101,    97,    89,    87,   105,   106,     9,    86,
    94,    17,    86,    86,    70,    71,    25,    88,    88,     0,
     0,    30,     4,    54,   195,     4,   202,   156,   158,   199,
    86,    87,   159,   161,    90,    91,     3,     4,     5,     6,
     7,     8,     9,   160,   100,   101,    -1,    -1,    -1,   105,
   106,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,
    -1,    70,    71,    30,    -1,    -1,    33,    -1,     3,     4,
     5,     6,     7,     8,     9,    -1,    -1,    -1,    87,    88,
     3,     4,     5,     6,     7,     8,     9,    -1,    -1,    -1,
    25,   100,   101,    -1,    -1,    30,   105,   106,    -1,    -1,
    -1,    -1,    25,    70,    71,    -1,    -1,    30,    -1,    -1,
    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    87,    -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,
    -1,    -1,    -1,   100,   101,    70,    71,    -1,   105,   106,
    -1,    -1,    -1,    -1,    -1,    -1,    -1,    70,    71,    -1,
    -1,    -1,    87,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    -1,    -1,    -1,    -1,    87,   100,   101,    -1,    -1,    -1,
   105,   106,    -1,    -1,    -1,    -1,    -1,   100,   101,    70,
    71,    -1,   105,   106,    75,    76,    77,    78,    79,    80,
    81,    82,    83,    84,    85,    -1,    87,    -1,    -1,    -1,
    -1,    92,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    -1,    -1,    -1,    -1,    -1,    -1,   107,   108
};
#define YYPURE 1

/* -*-C-*-  Note some compilers choke on comments on `#line' lines.  */
#line 3 "/usr/share/misc/bison.simple"
/* This file comes from bison-1.28.  */

/* Skeleton output parser for bison,
   Copyright (C) 1984, 1989, 1990 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* This is the parser code that is written into each bison parser
  when the %semantic_parser declaration is not specified in the grammar.
  It was written by Richard Stallman by simplifying the hairy parser
  used when %semantic_parser is specified.  */

#ifndef YYSTACK_USE_ALLOCA
#ifdef alloca
#define YYSTACK_USE_ALLOCA
#else /* alloca not defined */
#ifdef __GNUC__
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#else /* not GNU C.  */
#if (!defined (__STDC__) && defined (sparc)) || defined (__sparc__) || defined (__sparc) || defined (__sgi) || (defined (__sun) && defined (__i386))
#define YYSTACK_USE_ALLOCA
#include <alloca.h>
#else /* not sparc */
/* We think this test detects Watcom and Microsoft C.  */
/* This used to test MSDOS, but that is a bad idea
   since that symbol is in the user namespace.  */
#if (defined (_MSDOS) || defined (_MSDOS_)) && !defined (__TURBOC__)
#if 0 /* No need for malloc.h, which pollutes the namespace;
	 instead, just don't use alloca.  */
#include <malloc.h>
#endif
#else /* not MSDOS, or __TURBOC__ */
#if defined(_AIX)
/* I don't know what this was needed for, but it pollutes the namespace.
   So I turned it off.   rms, 2 May 1997.  */
/* #include <malloc.h>  */
 #pragma alloca
#define YYSTACK_USE_ALLOCA
#else /* not MSDOS, or __TURBOC__, or _AIX */
#if 0
#ifdef __hpux /* haible@ilog.fr says this works for HPUX 9.05 and up,
		 and on HPUX 10.  Eventually we can turn this on.  */
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#endif /* __hpux */
#endif
#endif /* not _AIX */
#endif /* not MSDOS, or __TURBOC__ */
#endif /* not sparc */
#endif /* not GNU C */
#endif /* alloca not defined */
#endif /* YYSTACK_USE_ALLOCA not defined */

#ifdef YYSTACK_USE_ALLOCA
#define YYSTACK_ALLOC alloca
#else
#define YYSTACK_ALLOC malloc
#endif

/* Note: there must be only one dollar sign in this file.
   It is replaced by the list of actions, each action
   as one case of the switch.  */

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		-2
#define YYEOF		0
#define YYACCEPT	goto yyacceptlab
#define YYABORT 	goto yyabortlab
#define YYERROR		goto yyerrlab1
/* Like YYERROR except do call yyerror.
   This remains here temporarily to ease the
   transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */
#define YYFAIL		goto yyerrlab
#define YYRECOVERING()  (!!yyerrstatus)
#define YYBACKUP(token, value) \
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    { yychar = (token), yylval = (value);			\
      yychar1 = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { yyerror ("syntax error: cannot back up"); YYERROR; }	\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

#ifndef YYPURE
#define YYLEX		yylex()
#endif

#ifdef YYPURE
#ifdef YYLSP_NEEDED
#ifdef YYLEX_PARAM
#define YYLEX		yylex(&yylval, &yylloc, YYLEX_PARAM)
#else
#define YYLEX		yylex(&yylval, &yylloc)
#endif
#else /* not YYLSP_NEEDED */
#ifdef YYLEX_PARAM
#define YYLEX		yylex(&yylval, YYLEX_PARAM)
#else
#define YYLEX		yylex(&yylval)
#endif
#endif /* not YYLSP_NEEDED */
#endif

/* If nonreentrant, generate the variables here */

#ifndef YYPURE

int	yychar;			/*  the lookahead symbol		*/
YYSTYPE	yylval;			/*  the semantic value of the		*/
				/*  lookahead symbol			*/

#ifdef YYLSP_NEEDED
YYLTYPE yylloc;			/*  location data for the lookahead	*/
				/*  symbol				*/
#endif

int yynerrs;			/*  number of parse errors so far       */
#endif  /* not YYPURE */

#if YYDEBUG != 0
int yydebug;			/*  nonzero means print parse trace	*/
/* Since this is uninitialized, it does not stop multiple parsers
   from coexisting.  */
#endif

/*  YYINITDEPTH indicates the initial size of the parser's stacks	*/

#ifndef	YYINITDEPTH
#define YYINITDEPTH 200
#endif

/*  YYMAXDEPTH is the maximum size the stacks can grow to
    (effective only if the built-in stack extension method is used).  */

#if YYMAXDEPTH == 0
#undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
#define YYMAXDEPTH 10000
#endif

/* Define __yy_memcpy.  Note that the size argument
   should be passed with type unsigned int, because that is what the non-GCC
   definitions require.  With GCC, __builtin_memcpy takes an arg
   of type size_t, but it can handle unsigned int.  */

#if __GNUC__ > 1		/* GNU C and GNU C++ define this.  */
#define __yy_memcpy(TO,FROM,COUNT)	__builtin_memcpy(TO,FROM,COUNT)
#else				/* not GNU C or C++ */
#ifndef __cplusplus

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__yy_memcpy (to, from, count)
     char *to;
     char *from;
     unsigned int count;
{
  register char *f = from;
  register char *t = to;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#else /* __cplusplus */

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__yy_memcpy (char *to, char *from, unsigned int count)
{
  register char *t = to;
  register char *f = from;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#endif
#endif

#line 217 "/usr/share/misc/bison.simple"

/* The user can define YYPARSE_PARAM as the name of an argument to be passed
   into yyparse.  The argument should have type void *.
   It should actually point to an object.
   Grammar actions can access the variable by casting it
   to the proper pointer type.  */

#ifdef YYPARSE_PARAM
#ifdef __cplusplus
#define YYPARSE_PARAM_ARG void *YYPARSE_PARAM
#define YYPARSE_PARAM_DECL
#else /* not __cplusplus */
#define YYPARSE_PARAM_ARG YYPARSE_PARAM
#define YYPARSE_PARAM_DECL void *YYPARSE_PARAM;
#endif /* not __cplusplus */
#else /* not YYPARSE_PARAM */
#define YYPARSE_PARAM_ARG
#define YYPARSE_PARAM_DECL
#endif /* not YYPARSE_PARAM */

/* Prevent warning if -Wstrict-prototypes.  */
#ifdef __GNUC__
#ifdef YYPARSE_PARAM
int yyparse (void *);
#else
int yyparse (void);
#endif
#endif

int
yyparse(YYPARSE_PARAM_ARG)
     YYPARSE_PARAM_DECL
{
  register int yystate;
  register int yyn;
  register short *yyssp;
  register YYSTYPE *yyvsp;
  int yyerrstatus;	/*  number of tokens to shift before error messages enabled */
  int yychar1 = 0;		/*  lookahead token as an internal (translated) token number */

  short	yyssa[YYINITDEPTH];	/*  the state stack			*/
  YYSTYPE yyvsa[YYINITDEPTH];	/*  the semantic value stack		*/

  short *yyss = yyssa;		/*  refer to the stacks thru separate pointers */
  YYSTYPE *yyvs = yyvsa;	/*  to allow yyoverflow to reallocate them elsewhere */

#ifdef YYLSP_NEEDED
  YYLTYPE yylsa[YYINITDEPTH];	/*  the location stack			*/
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;

#define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)
#else
#define YYPOPSTACK   (yyvsp--, yyssp--)
#endif

  int yystacksize = YYINITDEPTH;
  int yyfree_stacks = 0;

#ifdef YYPURE
  int yychar;
  YYSTYPE yylval;
  int yynerrs;
#ifdef YYLSP_NEEDED
  YYLTYPE yylloc;
#endif
#endif

  YYSTYPE yyval;		/*  the variable used to return		*/
				/*  semantic values from the action	*/
				/*  routines				*/

  int yylen;

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Starting parse\n");
#endif

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss - 1;
  yyvsp = yyvs;
#ifdef YYLSP_NEEDED
  yylsp = yyls;
#endif

/* Push a new state, which is found in  yystate  .  */
/* In all cases, when you get here, the value and location stacks
   have just been pushed. so pushing a state here evens the stacks.  */
yynewstate:

  *++yyssp = yystate;

  if (yyssp >= yyss + yystacksize - 1)
    {
      /* Give user a chance to reallocate the stack */
      /* Use copies of these so that the &'s don't force the real ones into memory. */
      YYSTYPE *yyvs1 = yyvs;
      short *yyss1 = yyss;
#ifdef YYLSP_NEEDED
      YYLTYPE *yyls1 = yyls;
#endif

      /* Get the current used size of the three stacks, in elements.  */
      int size = yyssp - yyss + 1;

#ifdef yyoverflow
      /* Each stack pointer address is followed by the size of
	 the data in use in that stack, in bytes.  */
#ifdef YYLSP_NEEDED
      /* This used to be a conditional around just the two extra args,
	 but that might be undefined if yyoverflow is a macro.  */
      yyoverflow("parser stack overflow",
		 &yyss1, size * sizeof (*yyssp),
		 &yyvs1, size * sizeof (*yyvsp),
		 &yyls1, size * sizeof (*yylsp),
		 &yystacksize);
#else
      yyoverflow("parser stack overflow",
		 &yyss1, size * sizeof (*yyssp),
		 &yyvs1, size * sizeof (*yyvsp),
		 &yystacksize);
#endif

      yyss = yyss1; yyvs = yyvs1;
#ifdef YYLSP_NEEDED
      yyls = yyls1;
#endif
#else /* no yyoverflow */
      /* Extend the stack our own way.  */
      if (yystacksize >= YYMAXDEPTH)
	{
	  yyerror("parser stack overflow");
	  if (yyfree_stacks)
	    {
	      free (yyss);
	      free (yyvs);
#ifdef YYLSP_NEEDED
	      free (yyls);
#endif
	    }
	  return 2;
	}
      yystacksize *= 2;
      if (yystacksize > YYMAXDEPTH)
	yystacksize = YYMAXDEPTH;
#ifndef YYSTACK_USE_ALLOCA
      yyfree_stacks = 1;
#endif
      yyss = (short *) YYSTACK_ALLOC (yystacksize * sizeof (*yyssp));
      __yy_memcpy ((char *)yyss, (char *)yyss1,
		   size * (unsigned int) sizeof (*yyssp));
      yyvs = (YYSTYPE *) YYSTACK_ALLOC (yystacksize * sizeof (*yyvsp));
      __yy_memcpy ((char *)yyvs, (char *)yyvs1,
		   size * (unsigned int) sizeof (*yyvsp));
#ifdef YYLSP_NEEDED
      yyls = (YYLTYPE *) YYSTACK_ALLOC (yystacksize * sizeof (*yylsp));
      __yy_memcpy ((char *)yyls, (char *)yyls1,
		   size * (unsigned int) sizeof (*yylsp));
#endif
#endif /* no yyoverflow */

      yyssp = yyss + size - 1;
      yyvsp = yyvs + size - 1;
#ifdef YYLSP_NEEDED
      yylsp = yyls + size - 1;
#endif

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Stack size increased to %d\n", yystacksize);
#endif

      if (yyssp >= yyss + yystacksize - 1)
	YYABORT;
    }

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Entering state %d\n", yystate);
#endif

  goto yybackup;
 yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* yychar is either YYEMPTY or YYEOF
     or a valid token in external form.  */

  if (yychar == YYEMPTY)
    {
#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Reading a token: ");
#endif
      yychar = YYLEX;
    }

  /* Convert token to internal form (in yychar1) for indexing tables with */

  if (yychar <= 0)		/* This means end of input. */
    {
      yychar1 = 0;
      yychar = YYEOF;		/* Don't call YYLEX any more */

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Now at end of input.\n");
#endif
    }
  else
    {
      yychar1 = YYTRANSLATE(yychar);

#if YYDEBUG != 0
      if (yydebug)
	{
	  fprintf (stderr, "Next token is %d (%s", yychar, yytname[yychar1]);
	  /* Give the individual parser a way to print the precise meaning
	     of a token, for further debugging info.  */
#ifdef YYPRINT
	  YYPRINT (stderr, yychar, yylval);
#endif
	  fprintf (stderr, ")\n");
	}
#endif
    }

  yyn += yychar1;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != yychar1)
    goto yydefault;

  yyn = yytable[yyn];

  /* yyn is what to do for this token type in this state.
     Negative => reduce, -yyn is rule number.
     Positive => shift, yyn is new state.
       New state is final state => don't bother to shift,
       just return success.
     0, or most negative number => error.  */

  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrlab;

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Shifting token %d (%s), ", yychar, yytname[yychar1]);
#endif

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
#ifdef YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  /* count tokens shifted since error; after three, turn off error status.  */
  if (yyerrstatus) yyerrstatus--;

  yystate = yyn;
  goto yynewstate;

/* Do the default action for the current state.  */
yydefault:

  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;

/* Do a reduction.  yyn is the number of a rule to reduce with.  */
yyreduce:
  yylen = yyr2[yyn];
  if (yylen > 0)
    yyval = yyvsp[1-yylen]; /* implement default value of the action */

#if YYDEBUG != 0
  if (yydebug)
    {
      int i;

      fprintf (stderr, "Reducing via rule %d (line %d), ",
	       yyn, yyrline[yyn]);

      /* Print the symbols being reduced, and their result.  */
      for (i = yyprhs[yyn]; yyrhs[i] > 0; i++)
	fprintf (stderr, "%s ", yytname[yyrhs[i]]);
      fprintf (stderr, " -> %s\n", yytname[yyr1[yyn]]);
    }
#endif


  switch (yyn) {

case 3:
#line 124 "wmlscript/wsgram.y"
{ ws_error_syntax(pctx, yylsp[0].first_line); ;
    break;}
case 7:
#line 137 "wmlscript/wsgram.y"
{ ws_error_syntax(pctx, yylsp[1].first_line); ;
    break;}
case 11:
#line 148 "wmlscript/wsgram.y"
{ ws_pragma_use(pctx, yylsp[-1].first_line, yyvsp[-1].identifier, yyvsp[0].string); ;
    break;}
case 13:
#line 157 "wmlscript/wsgram.y"
{
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Pass this to the byte-code */
		    if (!ws_bc_add_pragma_access_domain(compiler->bc, yyvsp[0].string->data,
						        yyvsp[0].string->len))
		        ws_error_memory(pctx);
		    ws_lexer_free_utf8(compiler, yyvsp[0].string);
		;
    break;}
case 14:
#line 167 "wmlscript/wsgram.y"
{
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Pass this to the byte-code */
		    if (!ws_bc_add_pragma_access_path(compiler->bc, yyvsp[0].string->data,
						      yyvsp[0].string->len))
		        ws_error_memory(pctx);

		    ws_lexer_free_utf8(compiler, yyvsp[0].string);
		;
    break;}
case 15:
#line 178 "wmlscript/wsgram.y"
{
		    WsCompiler *compiler = (WsCompiler *) pctx;
		    WsBool success = WS_TRUE;

		    /* Pass these to the byte-code */
		    if (!ws_bc_add_pragma_access_domain(compiler->bc, yyvsp[-2].string->data,
						        yyvsp[-2].string->len))
		        success = WS_FALSE;

		    if (!ws_bc_add_pragma_access_path(compiler->bc, yyvsp[0].string->data,
						      yyvsp[0].string->len))
		        success = WS_FALSE;

		    if (!success)
		        ws_error_memory(pctx);

		    ws_lexer_free_utf8(compiler, yyvsp[-2].string);
		    ws_lexer_free_utf8(compiler, yyvsp[0].string);
		;
    break;}
case 20:
#line 211 "wmlscript/wsgram.y"
{
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Meta information for the origin servers.  Show it
                     * to the user if requested. */
		    if (compiler->params.meta_name_cb)
		        (*compiler->params.meta_name_cb)(
					yyvsp[0].meta_body->property_name, yyvsp[0].meta_body->content,
					yyvsp[0].meta_body->scheme,
					compiler->params.meta_name_cb_context);

		    /* We do not need the MetaBody anymore. */
		    ws_pragma_meta_body_free(compiler, yyvsp[0].meta_body);
		;
    break;}
case 21:
#line 229 "wmlscript/wsgram.y"
{
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Meta information HTTP header that should be
                     * included to an HTTP response header.  Show it to
                     * the user if requested. */
		    if (compiler->params.meta_http_equiv_cb)
		        (*compiler->params.meta_http_equiv_cb)(
				yyvsp[0].meta_body->property_name,
				yyvsp[0].meta_body->content,
				yyvsp[0].meta_body->scheme,
				compiler->params.meta_http_equiv_cb_context);

		    /* We do not need the MetaBody anymore. */
		    ws_pragma_meta_body_free(compiler, yyvsp[0].meta_body);
		;
    break;}
case 22:
#line 249 "wmlscript/wsgram.y"
{
		    WsBool success;
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Pass this pragma to the byte-code */
		    if (yyvsp[0].meta_body) {
		        if (yyvsp[0].meta_body->scheme)
		  	    success
			  = ws_bc_add_pragma_user_agent_property_and_scheme(
						compiler->bc,
						yyvsp[0].meta_body->property_name->data,
						yyvsp[0].meta_body->property_name->len,
						yyvsp[0].meta_body->content->data,
						yyvsp[0].meta_body->content->len,
						yyvsp[0].meta_body->scheme->data,
						yyvsp[0].meta_body->scheme->len);
		        else
		  	    success = ws_bc_add_pragma_user_agent_property(
						compiler->bc,
						yyvsp[0].meta_body->property_name->data,
						yyvsp[0].meta_body->property_name->len,
						yyvsp[0].meta_body->content->data,
						yyvsp[0].meta_body->content->len);

		        /* Free the MetaBody. */
		        ws_pragma_meta_body_free(compiler, yyvsp[0].meta_body);

		        if (!success)
		  	    ws_error_memory(pctx);
		    }
		;
    break;}
case 23:
#line 284 "wmlscript/wsgram.y"
{ yyval.meta_body = ws_pragma_meta_body(pctx, yyvsp[-1].string, yyvsp[0].string, NULL); ;
    break;}
case 24:
#line 286 "wmlscript/wsgram.y"
{ yyval.meta_body = ws_pragma_meta_body(pctx, yyvsp[-2].string, yyvsp[-1].string, yyvsp[0].string); ;
    break;}
case 30:
#line 303 "wmlscript/wsgram.y"
{
		    char *name = ws_strdup(yyvsp[-5].identifier);

		    ws_lexer_free_block(pctx, yyvsp[-5].identifier);

		    if (name)
		        ws_function(pctx, yyvsp[-7].boolean, name, yylsp[-5].first_line, yyvsp[-3].list, yyvsp[-1].list);
		    else
		        ws_error_memory(pctx);
		;
    break;}
case 31:
#line 316 "wmlscript/wsgram.y"
{ yyval.boolean = WS_FALSE; ;
    break;}
case 32:
#line 317 "wmlscript/wsgram.y"
{ yyval.boolean = WS_TRUE;  ;
    break;}
case 33:
#line 322 "wmlscript/wsgram.y"
{ yyval.list = ws_list_new(pctx); ;
    break;}
case 37:
#line 333 "wmlscript/wsgram.y"
{
		    char *id = ws_f_strdup(((WsCompiler *) pctx)->pool_stree,
					    yyvsp[0].identifier);
		    WsPair *pair = ws_pair_new(pctx, (void *) yylsp[0].first_line, id);

		    ws_lexer_free_block(pctx, yyvsp[0].identifier);

		    if (id == NULL || pair == NULL) {
		        ws_error_memory(pctx);
		        yyval.list = NULL;
		    } else {
		        yyval.list = ws_list_new(pctx);
		        ws_list_append(pctx, yyval.list, pair);
		    }
		;
    break;}
case 38:
#line 349 "wmlscript/wsgram.y"
{
		    char *id = ws_f_strdup(((WsCompiler *) pctx)->pool_stree,
					   yyvsp[0].identifier);
		    WsPair *pair = ws_pair_new(pctx, (void *) yylsp[-2].first_line, id);

		    ws_lexer_free_block(pctx, yyvsp[0].identifier);

		    if (id == NULL || pair == NULL) {
		        ws_error_memory(pctx);
		        yyval.list = NULL;
		    } else
		        ws_list_append(pctx, yyvsp[-2].list, pair);
		;
    break;}
case 39:
#line 368 "wmlscript/wsgram.y"
{
		    if (yyvsp[0].list)
		        yyval.stmt = ws_stmt_block(pctx, yyvsp[0].list->first_line, yyvsp[0].list->last_line,
				           yyvsp[0].list);
		    else
		        yyval.stmt = NULL;
		;
    break;}
case 41:
#line 377 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_empty(pctx, yylsp[0].first_line); ;
    break;}
case 42:
#line 379 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_expr(pctx, yyvsp[-1].expr->line, yyvsp[-1].expr); ;
    break;}
case 45:
#line 383 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_continue(pctx, yylsp[-1].first_line); ;
    break;}
case 46:
#line 385 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_break(pctx, yylsp[-1].first_line); ;
    break;}
case 48:
#line 390 "wmlscript/wsgram.y"
{
		    yyval.list = yyvsp[-1].list;
		    if (yyval.list) {
		        yyval.list->first_line = yylsp[-2].first_line;
		        yyval.list->last_line = yylsp[0].first_line;
		    }
		;
    break;}
case 49:
#line 398 "wmlscript/wsgram.y"
{
		    ws_error_syntax(pctx, yylsp[1].first_line);
		    yyval.list = NULL;
		;
    break;}
case 50:
#line 406 "wmlscript/wsgram.y"
{ yyval.list = ws_list_new(pctx); ;
    break;}
case 52:
#line 412 "wmlscript/wsgram.y"
{
		    yyval.list = ws_list_new(pctx);
		    ws_list_append(pctx, yyval.list, yyvsp[0].stmt);
		;
    break;}
case 53:
#line 417 "wmlscript/wsgram.y"
{ ws_list_append(pctx, yyvsp[-1].list, yyvsp[0].stmt); ;
    break;}
case 54:
#line 422 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_variable(pctx, yylsp[-2].first_line, yyvsp[-1].list); ;
    break;}
case 55:
#line 424 "wmlscript/wsgram.y"
{ ws_error_syntax(pctx, yylsp[0].first_line); ;
    break;}
case 56:
#line 429 "wmlscript/wsgram.y"
{
		    yyval.list = ws_list_new(pctx);
		    ws_list_append(pctx, yyval.list, yyvsp[0].pair);
		;
    break;}
case 57:
#line 434 "wmlscript/wsgram.y"
{ ws_list_append(pctx, yyvsp[-2].list, yyvsp[0].pair); ;
    break;}
case 58:
#line 439 "wmlscript/wsgram.y"
{
		    char *id = ws_f_strdup(((WsCompiler *) pctx)->pool_stree,
					   yyvsp[-1].identifier);

		    ws_lexer_free_block(pctx, yyvsp[-1].identifier);
		    if (id == NULL) {
		        ws_error_memory(pctx);
		        yyval.pair = NULL;
		    } else
		        yyval.pair = ws_pair_new(pctx, id, yyvsp[0].expr);
		;
    break;}
case 59:
#line 454 "wmlscript/wsgram.y"
{ yyval.expr = NULL; ;
    break;}
case 60:
#line 456 "wmlscript/wsgram.y"
{ yyval.expr = yyvsp[0].expr; ;
    break;}
case 61:
#line 461 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_if(pctx, yylsp[-6].first_line, yyvsp[-4].expr, yyvsp[-2].stmt, yyvsp[0].stmt); ;
    break;}
case 62:
#line 463 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_if(pctx, yylsp[-4].first_line, yyvsp[-2].expr, yyvsp[0].stmt, NULL); ;
    break;}
case 63:
#line 468 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_while(pctx, yylsp[-4].first_line, yyvsp[-2].expr, yyvsp[0].stmt); ;
    break;}
case 65:
#line 475 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_for(pctx, yylsp[-8].first_line, NULL, yyvsp[-6].expr, yyvsp[-4].expr, yyvsp[-2].expr, yyvsp[0].stmt); ;
    break;}
case 66:
#line 478 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_for(pctx, yylsp[-9].first_line, yyvsp[-6].list, NULL, yyvsp[-4].expr, yyvsp[-2].expr, yyvsp[0].stmt); ;
    break;}
case 67:
#line 483 "wmlscript/wsgram.y"
{ yyval.stmt = ws_stmt_return(pctx, yylsp[-2].first_line, yyvsp[-1].expr); ;
    break;}
case 68:
#line 489 "wmlscript/wsgram.y"
{ yyval.expr = NULL; ;
    break;}
case 71:
#line 497 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_comma(pctx, yylsp[-1].first_line, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 73:
#line 503 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, '=', yyvsp[0].expr); ;
    break;}
case 74:
#line 505 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tMULA, yyvsp[0].expr); ;
    break;}
case 75:
#line 507 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tDIVA, yyvsp[0].expr); ;
    break;}
case 76:
#line 509 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tREMA, yyvsp[0].expr); ;
    break;}
case 77:
#line 511 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tADDA, yyvsp[0].expr); ;
    break;}
case 78:
#line 513 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tSUBA, yyvsp[0].expr); ;
    break;}
case 79:
#line 515 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tLSHIFTA, yyvsp[0].expr); ;
    break;}
case 80:
#line 517 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tRSSHIFTA, yyvsp[0].expr); ;
    break;}
case 81:
#line 519 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tRSZSHIFTA, yyvsp[0].expr); ;
    break;}
case 82:
#line 521 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tANDA, yyvsp[0].expr); ;
    break;}
case 83:
#line 523 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tXORA, yyvsp[0].expr); ;
    break;}
case 84:
#line 525 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tORA, yyvsp[0].expr); ;
    break;}
case 85:
#line 527 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tIDIVA, yyvsp[0].expr); ;
    break;}
case 87:
#line 533 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_conditional(pctx, yylsp[-3].first_line, yyvsp[-4].expr, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 89:
#line 539 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_logical(pctx, yylsp[-1].first_line, WS_ASM_SCOR, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 91:
#line 545 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_logical(pctx, yylsp[-1].first_line, WS_ASM_SCAND, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 93:
#line 551 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_OR, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 95:
#line 557 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_XOR, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 97:
#line 563 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_AND, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 99:
#line 569 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_EQ, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 100:
#line 571 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_NE, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 102:
#line 577 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_LT, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 103:
#line 579 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_GT, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 104:
#line 581 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_LE, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 105:
#line 583 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_GE, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 107:
#line 589 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_LSHIFT, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 108:
#line 591 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_RSSHIFT, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 109:
#line 593 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_RSZSHIFT, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 111:
#line 599 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_ADD, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 112:
#line 601 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_SUB, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 114:
#line 607 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_MUL, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 115:
#line 609 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_DIV, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 116:
#line 611 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_IDIV, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 117:
#line 613 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_REM, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 119:
#line 619 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_TYPEOF, yyvsp[0].expr); ;
    break;}
case 120:
#line 621 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_ISVALID, yyvsp[0].expr); ;
    break;}
case 121:
#line 623 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_unary_var(pctx, yylsp[-1].first_line, WS_TRUE, yyvsp[0].identifier); ;
    break;}
case 122:
#line 625 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_unary_var(pctx, yylsp[-1].first_line, WS_FALSE, yyvsp[0].identifier); ;
    break;}
case 123:
#line 627 "wmlscript/wsgram.y"
{
                    /* There is no direct way to compile unary `+'.
                     * It doesn't do anything except require type conversion
		     * (section 7.2, 7.3.2), and we do that by converting
		     * it to a binary expression: `UnaryExpression - 0'.
                     * Using `--UnaryExpression' would not be correct because
                     * it might overflow if UnaryExpression is the smallest
                     * possible integer value (see 6.2.7.1).
                     * Using `UnaryExpression + 0' would not be correct
                     * because binary `+' accepts strings, which makes the
		     * type conversion different.
                     */
                    yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_SUB, yyvsp[0].expr,
                              ws_expr_const_integer(pctx, yylsp[-1].first_line, 0));
		;
    break;}
case 124:
#line 643 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_UMINUS, yyvsp[0].expr); ;
    break;}
case 125:
#line 645 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_B_NOT, yyvsp[0].expr); ;
    break;}
case 126:
#line 647 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_NOT, yyvsp[0].expr); ;
    break;}
case 128:
#line 653 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_postfix_var(pctx, yylsp[-1].first_line, WS_TRUE, yyvsp[-1].identifier); ;
    break;}
case 129:
#line 655 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_postfix_var(pctx, yylsp[-1].first_line, WS_FALSE, yyvsp[-1].identifier); ;
    break;}
case 131:
#line 661 "wmlscript/wsgram.y"
{
		    WsFunctionHash *f = ws_function_hash(pctx, yyvsp[-1].identifier);

		    /* Add an usage count for the local script function. */
		    if (f)
		      f->usage_count++;

		    yyval.expr = ws_expr_call(pctx, yylsp[-1].first_line, ' ', NULL, yyvsp[-1].identifier, yyvsp[0].list);
		;
    break;}
case 132:
#line 671 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_call(pctx, yylsp[-1].first_line, '#', yyvsp[-3].identifier, yyvsp[-1].identifier, yyvsp[0].list); ;
    break;}
case 133:
#line 673 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_call(pctx, yylsp[-1].first_line, '.', yyvsp[-3].identifier, yyvsp[-1].identifier, yyvsp[0].list); ;
    break;}
case 134:
#line 678 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_symbol(pctx, yylsp[0].first_line, yyvsp[0].identifier); ;
    break;}
case 135:
#line 680 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_const_invalid(pctx, yylsp[0].first_line); ;
    break;}
case 136:
#line 682 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_const_true(pctx, yylsp[0].first_line); ;
    break;}
case 137:
#line 684 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_const_false(pctx, yylsp[0].first_line); ;
    break;}
case 138:
#line 686 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_const_integer(pctx, yylsp[0].first_line, yyvsp[0].integer); ;
    break;}
case 139:
#line 688 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_const_float(pctx, yylsp[0].first_line, yyvsp[0].vfloat); ;
    break;}
case 140:
#line 690 "wmlscript/wsgram.y"
{ yyval.expr = ws_expr_const_string(pctx, yylsp[0].first_line, yyvsp[0].string); ;
    break;}
case 141:
#line 692 "wmlscript/wsgram.y"
{ yyval.expr = yyvsp[-1].expr; ;
    break;}
case 142:
#line 697 "wmlscript/wsgram.y"
{ yyval.list = ws_list_new(pctx); ;
    break;}
case 143:
#line 699 "wmlscript/wsgram.y"
{ yyval.list = yyvsp[-1].list; ;
    break;}
case 144:
#line 704 "wmlscript/wsgram.y"
{
		    yyval.list = ws_list_new(pctx);
		    ws_list_append(pctx, yyval.list, yyvsp[0].expr);
		;
    break;}
case 145:
#line 709 "wmlscript/wsgram.y"
{ ws_list_append(pctx, yyvsp[-2].list, yyvsp[0].expr); ;
    break;}
}
   /* the action file gets copied in in place of this dollarsign */
#line 543 "/usr/share/misc/bison.simple"

  yyvsp -= yylen;
  yyssp -= yylen;
#ifdef YYLSP_NEEDED
  yylsp -= yylen;
#endif

#if YYDEBUG != 0
  if (yydebug)
    {
      short *ssp1 = yyss - 1;
      fprintf (stderr, "state stack now");
      while (ssp1 != yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

  *++yyvsp = yyval;

#ifdef YYLSP_NEEDED
  yylsp++;
  if (yylen == 0)
    {
      yylsp->first_line = yylloc.first_line;
      yylsp->first_column = yylloc.first_column;
      yylsp->last_line = (yylsp-1)->last_line;
      yylsp->last_column = (yylsp-1)->last_column;
      yylsp->text = 0;
    }
  else
    {
      yylsp->last_line = (yylsp+yylen-1)->last_line;
      yylsp->last_column = (yylsp+yylen-1)->last_column;
    }
#endif

  /* Now "shift" the result of the reduction.
     Determine what state that goes to,
     based on the state we popped back to
     and the rule number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTBASE] + *yyssp;
  if (yystate >= 0 && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTBASE];

  goto yynewstate;

yyerrlab:   /* here on detecting error */

  if (! yyerrstatus)
    /* If not already recovering from an error, report this error.  */
    {
      ++yynerrs;

#ifdef YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (yyn > YYFLAG && yyn < YYLAST)
	{
	  int size = 0;
	  char *msg;
	  int x, count;

	  count = 0;
	  /* Start X at -yyn if nec to avoid negative indexes in yycheck.  */
	  for (x = (yyn < 0 ? -yyn : 0);
	       x < (sizeof(yytname) / sizeof(char *)); x++)
	    if (yycheck[x + yyn] == x)
	      size += strlen(yytname[x]) + 15, count++;
	  msg = (char *) malloc(size + 15);
	  if (msg != 0)
	    {
	      strcpy(msg, "parse error");

	      if (count < 5)
		{
		  count = 0;
		  for (x = (yyn < 0 ? -yyn : 0);
		       x < (sizeof(yytname) / sizeof(char *)); x++)
		    if (yycheck[x + yyn] == x)
		      {
			strcat(msg, count == 0 ? ", expecting `" : " or `");
			strcat(msg, yytname[x]);
			strcat(msg, "'");
			count++;
		      }
		}
	      yyerror(msg);
	      free(msg);
	    }
	  else
	    yyerror ("parse error; also virtual memory exceeded");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror("parse error");
    }

  goto yyerrlab1;
yyerrlab1:   /* here on error raised explicitly by an action */

  if (yyerrstatus == 3)
    {
      /* if just tried and failed to reuse lookahead token after an error, discard it.  */

      /* return failure if at end of input */
      if (yychar == YYEOF)
	YYABORT;

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Discarding token %d (%s).\n", yychar, yytname[yychar1]);
#endif

      yychar = YYEMPTY;
    }

  /* Else will try to reuse lookahead token
     after shifting the error token.  */

  yyerrstatus = 3;		/* Each real token shifted decrements this */

  goto yyerrhandle;

yyerrdefault:  /* current state does not do anything special for the error token. */

#if 0
  /* This is wrong; only states that explicitly want error tokens
     should shift them.  */
  yyn = yydefact[yystate];  /* If its default is to accept any token, ok.  Otherwise pop it.*/
  if (yyn) goto yydefault;
#endif

yyerrpop:   /* pop the current state because it cannot handle the error token */

  if (yyssp == yyss) YYABORT;
  yyvsp--;
  yystate = *--yyssp;
#ifdef YYLSP_NEEDED
  yylsp--;
#endif

#if YYDEBUG != 0
  if (yydebug)
    {
      short *ssp1 = yyss - 1;
      fprintf (stderr, "Error: state stack now");
      while (ssp1 != yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

yyerrhandle:

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yyerrdefault;

  yyn += YYTERROR;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != YYTERROR)
    goto yyerrdefault;

  yyn = yytable[yyn];
  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrpop;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrpop;

  if (yyn == YYFINAL)
    YYACCEPT;

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Shifting error token, ");
#endif

  *++yyvsp = yylval;
#ifdef YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  yystate = yyn;
  goto yynewstate;

 yyacceptlab:
  /* YYACCEPT comes here.  */
  if (yyfree_stacks)
    {
      free (yyss);
      free (yyvs);
#ifdef YYLSP_NEEDED
      free (yyls);
#endif
    }
  return 0;

 yyabortlab:
  /* YYABORT comes here.  */
  if (yyfree_stacks)
    {
      free (yyss);
      free (yyvs);
#ifdef YYLSP_NEEDED
      free (yyls);
#endif
    }
  return 1;
}
#line 712 "wmlscript/wsgram.y"


void
yyerror(char *msg)
{
#if WS_DEBUG
  fprintf(stderr, "*** %s:%d: wsc: %s - this msg will be removed ***\n",
	  global_compiler->input_name, global_compiler->linenum, msg);
#endif /* WS_DEBUG */
}
