/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2003 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

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

