typedef union
{
    WsUInt32 integer;
    WsFloat vfloat;
    char *identifier;
    WsUtf8String *string;

    WsBool boolean;
    WsList *list;
    WsFormalParm *parm;
    WsVarDec *vardec;

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

