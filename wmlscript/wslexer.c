/*
 *
 * wslexer.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 Markku Rossi, etc.
 *		 All rights reserved.
 *
 * Lexical analyzer.
 *
 */

#include <wsint.h>
#include <wsstree.h>
#include <wsgram.h>

/********************* Types and definitions ****************************/

/* A predicate to check whether the character `ch' is a decimal
   digit. */
#define WS_IS_DECIMAL_DIGIT(ch) ('0' <= (ch) && (ch) <= '9')

/* Convert the decimal digit `ch' to an integer number. */
#define WS_DECIMAL_TO_INT(ch) ((ch) - '0')

/* A predicate to check whether the character `ch' is a non-zero
   decimal digit. */
#define WS_IS_NON_ZERO_DIGIT(ch) ('1' <= (ch) && (ch) <= '9')

/* A predicate to check whether the character `ch' is an octal digit. */
#define WS_IS_OCTAL_DIGIT(ch) ('0' <= (ch) && (ch) <= '7')

/* Convert the octal digit `ch' to an integer number. */
#define WS_OCTAL_TO_INT(ch) ((ch) - '0')

/* A predicate to check whether the character `ch' is a hex digit. */
#define WS_IS_HEX_DIGIT(ch) (('0' <= (ch) && (ch) <= '9')	\
			     || ('a' <= (ch) && (ch) <= 'f')	\
			     || ('A' <= (ch) && (ch) <= 'F'))

/* Convert the hex digit `ch' to an integer number. */
#define WS_HEX_TO_INT(ch)		\
  ('0' <= (ch) && (ch) <= '9'		\
   ? ((ch) - '0')			\
   : ('a' <= (ch) && (ch) <= 'f'	\
      ? ((ch) - 'a' + 10)		\
      : (ch) - 'A' + 10))

/* A predicate to check whether the character `ch' is an identifier
   starter letter. */
#define WS_IS_IDENTIFIER_LETTER(ch)	\
  (('a' <= (ch) && (ch) <= 'z')		\
   || ('A' <= (ch) && (ch) <= 'Z')	\
   || (ch) == '_')

/********************* Prototypes for static functions ******************/

/* Check whether the identifier `id', `len' is a keyword.  If the
   identifier is a keyword, the function returns WS_TRUE and sets the
   keywords token ID to `token_return'.  Otherwise the function
   returns WS_FALSE. */
static WsBool lookup_keyword(char *id, size_t len, int *token_return);

/* XXX */
static WsBool read_float_from_point(WsCompiler *compiler, WsBuffer *buffer,
				    WsFloat32 *result);

/* XXX */
static WsBool read_float_from_exp(WsCompiler *compiler, WsBuffer *buffer,
				  WsFloat32 *result);

/********************* Static variables *********************************/

#define N(n) n, sizeof(n) - 1

static struct
{
  char *name;
  int name_len;
  int token;
} keywords[] =
{
  {N("access"),		tACCESS},
  {N("agent"),		tAGENT},
  {N("break"),		tBREAK},
  {N("case"),		tCASE},
  {N("catch"),		tCATCH},
  {N("class"),		tCLASS},
  {N("const"),		tCONST},
  {N("continue"),	tCONTINUE},
  {N("debugger"),	tDEBUGGER},
  {N("default"),	tDEFAULT},
  {N("delete"),		tDELETE},
  {N("div"),		tIDIV},
  {N("do"),		tDO},
  {N("domain"),		tDOMAIN},
  {N("else"),		tELSE},
  {N("enum"),		tENUM},
  {N("equiv"),		tEQUIV},
  {N("export"),		tEXPORT},
  {N("extends"),	tEXTENDS},
  {N("extern"),		tEXTERN},
  {N("false"),		tFALSE},
  {N("finally"),	tFINALLY},
  {N("for"),		tFOR},
  {N("function"),	tFUNCTION},
  {N("header"),		tHEADER},
  {N("http"),		tHTTP},
  {N("if"),		tIF},
  {N("import"),		tIMPORT},
  {N("in"),		tIN},
  {N("invalid"),	tINVALID},
  {N("isvalid"),	tISVALID},
  {N("lib"),		tLIB},
  {N("meta"),		tMETA},
  {N("name"),		tNAME},
  {N("new"),		tNEW},
  {N("null"),		tNULL},
  {N("path"),		tPATH},
  {N("private"),	tPRIVATE},
  {N("public"),		tPUBLIC},
  {N("return"),		tRETURN},
  {N("sizeof"),		tSIZEOF},
  {N("struct"),		tSTRUCT},
  {N("super"),		tSUPER},
  {N("switch"),		tSWITCH},
  {N("this"),		tTHIS},
  {N("throw"),		tTHROW},
  {N("true"),		tTRUE},
  {N("try"),		tTRY},
  {N("typeof"),		tTYPEOF},
  {N("url"),		tURL},
  {N("use"),		tUSE},
  {N("user"),		tUSER},
  {N("var"),		tVAR},
  {N("void"),		tVOID},
  {N("while"),		tWHILE},
  {N("with"),		tWITH},
};

static int num_keywords = sizeof(keywords) / sizeof(keywords[0]);

/********************* Global functions *********************************/

int
ws_yy_lex(YYSTYPE *yylval, YYLTYPE *yylloc, void *context)
{
  WsCompiler *compiler = (WsCompiler *) context;
  WsUInt32 ch, ch2;
  WsBuffer buffer;
  unsigned char *p;
  WsBool success;
  char buf[256];

  /* Just check that we get the correct amount of arguments. */
  assert(compiler->magic = 0xfefe0101);

  while (ws_stream_getc(compiler->input, &ch))
    {
      /* Save the token's line number. */
      yylloc->first_line = compiler->linenum;

      switch (ch)
	{
	case '\t':		/* Whitespace */
	case '\v':
	case '\f':
	case ' ':
	  continue;
	  break;

	case '\n':		/* Line terminator */
	case '\r':
	  if (ch == '\r' && ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 != '\n')
		ws_stream_ungetc(compiler->input, ch2);
	    }
	  compiler->linenum++;
	  continue;
	  break;

	case '!':		/* !, != */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '=')
		return tNE;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '!';
	  break;

	case '%':		/* %, %= */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '=')
		return tREMA;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '%';
	  break;

	case '&':		/* &, &&, &= */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '&')
		return tAND;
	      if (ch2 == '=')
		return tANDA;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '&';
	  break;

	case '*':		/* *, *= */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '=')
		return tMULA;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '*';
	  break;

	case '+':		/* +, ++, += */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '+')
		return tPLUSPLUS;
	      if (ch2 == '=')
		return tADDA;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '+';
	  break;

	case '-':		/* -, --, -= */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '-')
		return tMINUSMINUS;
	      if (ch2 == '=')
		return tSUBA;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '-';
	  break;

	case '.':
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (WS_IS_DECIMAL_DIGIT(ch2))
		{
		  /* DecimalFloatLiteral. */
		  ws_buffer_init(&buffer);

		  if (!ws_buffer_append_space(&buffer, &p, 2))
		    {
		      ws_error_memory(compiler);
		      ws_buffer_uninit(&buffer);
		      return EOF;
		    }

		  p[0] = '.';
		  p[1] = (unsigned char) ch2;

		  success = read_float_from_point(compiler, &buffer,
						  &yylval->vfloat);
		  ws_buffer_uninit(&buffer);

		  if (!success)
		    return EOF;

		  return tFLOAT32;
		}

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '.';
	  break;

	case '/':		/* /, /=, block or single line comment */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '*')
		{
		  /* Block comment. */
		  while (1)
		    {
		      if (!ws_stream_getc(compiler->input, &ch))
			goto eof_in_comment;

		      if (ch == '\n' || ch == '\r')
			{
			  /* Line terminator. */
			  if (ch == '\r' && ws_stream_getc(compiler->input,
							   &ch2))
			    {
			      if (ch2 != '\n')
				ws_stream_ungetc(compiler->input, ch2);
			    }
			  compiler->linenum++;

			  /* Continue reading the block comment. */
			  continue;
			}

		      if (ch == '*' && ws_stream_getc(compiler->input, &ch2))
			{
			  if (ch2 == '/')
			    /* The end of the comment found. */
			    break;
			  ws_stream_ungetc(compiler->input, ch2);
			}
		    }
		  /* Continue after the comment. */
		  continue;
		}
	      if (ch2 == '/')
		{
		  /* Single line comment. */
		  while (1)
		    {
		      if (!ws_stream_getc(compiler->input, &ch))
			{
			eof_in_comment:
			  ws_src_error(compiler, 0, "EOF in comment");
			  return EOF;
			}
		      if (ch == '\n' || ch == '\r')
			{
			  /* Line terminator. */
			  if (ch == '\r' && ws_stream_getc(compiler->input,
							   &ch2))
			    {
			      if (ch2 != '\n')
				ws_stream_ungetc(compiler->input, ch2);
			    }
			  /* The end of the line (and the comment)
                             reached. */
			  compiler->linenum++;
			  break;
			}
		    }
		  /* Continue after the comment. */
		  continue;
		}
	      if (ch2 == '=')
		return tDIVA;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '/';
	  break;

	case '<':		/* <, <<, <<=, <= */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '<')
		{
		  if (ws_stream_getc(compiler->input, &ch2))
		    {
		      if (ch2 == '=')
			return tLSHIFTA;

		      ws_stream_ungetc(compiler->input, ch2);
		    }
		  return tLSHIFT;
		}
	      if (ch2 == '=')
		return tLE;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '<';
	  break;

	case '=':		/* =, == */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '=')
		return tEQ;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '=';
	  break;

	case '>':		/* >, >=, >>, >>=, >>>, >>>= */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '>')
		{
		  if (ws_stream_getc(compiler->input, &ch2))
		    {
		      if (ch2 == '>')
			{
			  if (ws_stream_getc(compiler->input, &ch2))
			    {
			      if (ch2 == '=')
				return tRSZSHIFTA;

			      ws_stream_ungetc(compiler->input, ch2);
			    }
			  return tRSZSHIFT;
			}
		      if (ch2 == '=')
			return tRSSHIFTA;

		      ws_stream_ungetc(compiler->input, ch2);
		    }
		  return tRSSHIFT;
		}
	      if (ch2 == '=')
		return tGE;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '>';
	  break;

	case '^':		/* ^, ^= */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '=')
		return tXORA;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '^';
	  break;

	case '|':		/* |, |=, || */
	  if (ws_stream_getc(compiler->input, &ch2))
	    {
	      if (ch2 == '=')
		return tORA;
	      if (ch2 == '|')
		return tOR;

	      ws_stream_ungetc(compiler->input, ch2);
	    }
	  return '|';
	  break;

	case '#':		/* The simple cases. */
	case '(':
	case ')':
	case ',':
	case ':':
	case ';':
	case '?':
	case '{':
	case '}':
	case '~':
	  return (int) ch;
	  break;

	case '\'':		/* String literals. */
	case '"':
	  {
	    WsUInt32 string_end_ch = ch;
	    WsUtf8String *str = ws_utf8_alloc();

	    if (str == NULL)
	      {
		ws_error_memory(compiler);
		return EOF;
	      }

	    while (1)
	      {
		if (!ws_stream_getc(compiler->input, &ch))
		  {
		  eof_in_string_literal:
		    ws_src_error(compiler, 0, "EOF in string literal");
		    ws_utf8_free(str);
		    return EOF;
		  }
		if (ch == string_end_ch)
		  /* The end of string reached. */
		  break;

		if (ch == '\\')
		  {
		    /* An escape sequence. */
		    if (!ws_stream_getc(compiler->input, &ch))
		      goto eof_in_string_literal;

		    switch (ch)
		      {
		      case '\'':
		      case '"':
		      case '\\':
		      case '/':
			/* The character as-is. */
			break;

		      case 'b':
			ch = '\b';
			break;

		      case 'f':
			ch = '\f';
			break;

		      case 'n':
			ch = '\n';
			break;

		      case 'r':
			ch = '\r';
			break;

		      case 't':
			ch = '\t';
			break;

		      case 'x':
		      case 'u':
			{
			  int i, len;
			  int type = ch;

			  if (ch == 'x')
			    len = 2;
			  else
			    len = 4;

			  ch = 0;
			  for (i = 0; i < len; i++)
			    {
			      if (!ws_stream_getc(compiler->input, &ch2))
				goto eof_in_string_literal;
			      if (!WS_IS_HEX_DIGIT(ch2))
				{
				  ws_src_error(compiler, 0,
					       "malformed `\\%c' escape in "
					       "string literal", (char) type);
				  ch = 0;
				  break;
				}
			      ch *= 16;
			      ch += WS_HEX_TO_INT(ch2);
			    }
			}
			break;

		      default:
			if (WS_IS_OCTAL_DIGIT(ch))
			  {
			    int i;
			    int limit = 3;

			    ch = WS_OCTAL_TO_INT(ch);
			    if (ch > 3)
			      limit = 2;

			    for (i = 1; i < limit; i++)
			      {
				if (!ws_stream_getc(compiler->input, &ch2))
				  goto eof_in_string_literal;
				if (!WS_IS_OCTAL_DIGIT(ch2))
				  {
				    ws_stream_ungetc(compiler->input, ch2);
				    break;
				  }

				ch *= 8;
				ch += WS_OCTAL_TO_INT(ch2);
			      }
			  }
			else
			  {
			    ws_src_error(compiler, 0,
					 "unknown escape sequence `\\%c' in "
					 "string literal", (char) ch);
			    ch = 0;
			  }
			break;
		      }
		    /* FALLTHROUGH */
		  }

		if (!ws_utf8_append_char(str, ch))
		  {
		    ws_error_memory(compiler);
		    ws_utf8_free(str);
		    return EOF;
		  }
	      }

	    if (!ws_lexer_register_utf8(compiler, str))
	      {
		ws_error_memory(compiler);
		ws_utf8_free(str);
		return EOF;
	      }

	    assert(str != NULL);
	    yylval->string = str;

	    return tSTRING;
	  }
	  break;

	default:
	  /* Identifiers, keywords and number constants. */

	  if (WS_IS_IDENTIFIER_LETTER(ch))
	    {
	      WsBool got;
	      int token;
	      unsigned char *p;
	      unsigned char *np;
	      size_t len = 0;

	      /* An identifier or a keyword. */
	      p = ws_malloc(256);
	      if (p == NULL)
		{
		  ws_error_memory(compiler);
		  return EOF;
		}

	      do
		{
		  /* Add one extra for the possible terminator
		     character. */
		  np = ws_realloc(p, len + 2);
		  if (np == NULL)
		    {
		      ws_error_memory(compiler);
		      ws_free(p);
		      return EOF;
		    }

		  p = np;

		  /* This is ok since the only valid identifier names
		     can be written in 7 bit ASCII. */
		  p[len++] = (unsigned char) ch;
		}
	      while ((got = ws_stream_getc(compiler->input, &ch))
		     && (WS_IS_IDENTIFIER_LETTER(ch)
			 || WS_IS_DECIMAL_DIGIT(ch)));

	      if (got)
		/* Put back the terminator character. */
		ws_stream_ungetc(compiler->input, ch);

	      /* Is it a keyword? */
	      if (lookup_keyword((char *) p, len, &token))
		{
		  /* Yes it is... */
		  ws_free(p);

		  /* ...except one case: `div='. */
		  if (token == tIDIV)
		    {
		      if (ws_stream_getc(compiler->input, &ch))
			{
			  if (ch == '=')
			    return tIDIVA;

			  ws_stream_ungetc(compiler->input, ch);
			}
		    }

		  /* Return the token value. */
		  return token;
		}

	      /* It is a normal identifier.  Let's pad the name with a
                 null-character.  We have already allocated space for
                 it. */
	      p[len] = '\0';

	      if (!ws_lexer_register_block(compiler, p))
		{
		  ws_error_memory(compiler);
		  ws_free(p);
		  return EOF;
		}

	      assert(p != NULL);
	      yylval->identifier = (char *) p;

	      return tIDENTIFIER;
	    }

	  if (WS_IS_NON_ZERO_DIGIT(ch))
	    {
	      /* A decimal integer literal or a decimal float
                 literal. */
	      yylval->integer = WS_DECIMAL_TO_INT(ch);
	      while (ws_stream_getc(compiler->input, &ch))
		{
		  if (WS_IS_DECIMAL_DIGIT(ch))
		    {
		      /* Check the really too large integer constants
                         here. */
		      if (yylval->integer > (WS_INT32_MAX / 10))
			{
			  ws_src_error(compiler, 0,
				       "integer literal too large");
			  return EOF;
			}
		      yylval->integer *= 10;
		      yylval->integer += WS_DECIMAL_TO_INT(ch);
		    }
		  else if (ch == '.' || ch == 'e' || ch == 'E')
		    {
		      /* DecimalFloatLiteral. */

		      ws_buffer_init(&buffer);
		      snprintf(buf, sizeof(buf), "%u", yylval->integer);

		      if (!ws_buffer_append(&buffer,
					    (unsigned char *) buf,
					    strlen(buf)))
			{
			float_error_memory:
			  ws_error_memory(compiler);
			  ws_buffer_uninit(&buffer);
			  return EOF;
			}

		      if (ch == '.')
			{
			  if (!ws_buffer_append_space(&buffer, &p, 1))
			    goto float_error_memory;

			  p[0] = '.';

			  success = read_float_from_point(compiler, &buffer,
							  &yylval->vfloat);
			}
		      else
			{
			  ws_stream_ungetc(compiler->input, ch);

			  success = read_float_from_exp(compiler, &buffer,
							&yylval->vfloat);
			}
		      ws_buffer_uninit(&buffer);

		      if (!success)
			return EOF;

		      return tFLOAT32;
		    }
		  else
		    {
		      ws_stream_ungetc(compiler->input, ch);
		      break;
		    }
		}

	      /* We do not check that the last digit was ok.  This
                 check must be done when we know whether the constant
                 did have an unary `-' operator in front of it. */

	      /* Read a DecimalIntegerLiteral. */
	      return tINTEGER;
	    }

	  if (ch == '0')
	    {
	      /* The integer constant 0, an octal number or a
                 HexIntegerLiteral. */
	      if (ws_stream_getc(compiler->input, &ch2))
		{
		  if (ch2 == 'x' || ch2 == 'X')
		    {
		      /* HexIntegerLiteral. */
		      yylval->integer = 0;
		      while (ws_stream_getc(compiler->input, &ch))
			{
			  if (WS_IS_HEX_DIGIT(ch))
			    {
			      /* Check the really too large integer
                                 constants here. */
			      if (yylval->integer > (WS_INT32_MAX / 16))
				{
				  ws_src_error(compiler, 0,
					       "hex integer literal too "
					       "large");
				  yylval->integer = 0;
				  return EOF;
				}
			      yylval->integer *= 16;
			      yylval->integer += WS_HEX_TO_INT(ch);
			    }
			  else
			    {
			      ws_stream_ungetc(compiler->input, ch);
			      break;
			    }
			}

		      /* Read a HexIntegerLiteral. */
		      return tINTEGER;
		    }
		  if (WS_IS_OCTAL_DIGIT(ch2))
		    {
		      /* OctalIntegerLiteral. */
		      yylval->integer = WS_OCTAL_TO_INT(ch2);
		      while (ws_stream_getc(compiler->input, &ch))
			{
			  if (WS_IS_OCTAL_DIGIT(ch))
			    {
			      /* Check the really too large integer
                                 constants here. */
			      if (yylval->integer > (WS_INT32_MAX / 8))
				{
				  ws_src_error(compiler, 0,
					       "octal integer literal too "
					       "large");
				  yylval->integer = 0;
				  return EOF;
				}
			      yylval->integer *= 8;
			      yylval->integer += WS_OCTAL_TO_INT(ch);
			    }
			  else
			    {
			      ws_stream_ungetc(compiler->input, ch);
			      break;
			    }
			}

		      /* Read an OctalIntegerLiteral. */
		      return tINTEGER;
		    }
		  if (ch2 == '.' || ch2 == 'e' || ch2 == 'E')
		    {
		      /* DecimalFloatLiteral. */
		      ws_buffer_init(&buffer);

		      if (ch2 == '.')
			{
			  if (!ws_buffer_append_space(&buffer, &p, 1))
			    goto float_error_memory;

			  p[0] = '.';

			  success = read_float_from_point(compiler, &buffer,
							  &yylval->vfloat);
			}
		      else
			{
			  ws_stream_ungetc(compiler->input, ch);

			  success = read_float_from_exp(compiler, &buffer,
							&yylval->vfloat);
			}
		      ws_buffer_uninit(&buffer);

		      if (!success)
			return EOF;

		      return tFLOAT32;
		    }

		  ws_stream_ungetc(compiler->input, ch2);
		}

	      /* Integer literal 0. */
	      yylval->integer = 0;
	      return tINTEGER;
	    }

	  /* XXX */
	  ws_fatal("ws_yy_lex: default not implemented yet");
	  break;
	}
    }

  return EOF;
}

/********************* Static functions *********************************/

static WsBool
lookup_keyword(char *id, size_t len, int *token_return)
{
  int i;

  /* XXX Should be reimplemented as binary search. */

  for (i = 0; i < num_keywords; i++)
    {
      int result;

      if (keywords[i].name_len == len)
	{
	  result = memcmp(id, keywords[i].name, len);

	  if (result == 0)
	    {
	      /* Found a match. */
	      *token_return = keywords[i].token;
	      return WS_TRUE;
	    }
	  if (result < 0)
	    /* Moved over the possible match. */
	    return WS_FALSE;
	}
    }

  /* No match. */
  return WS_FALSE;
}


static WsBool
read_float_from_point(WsCompiler *compiler, WsBuffer *buffer,
		      WsFloat32 *result)
{
  int ch;
  unsigned char *p;

  while (ws_stream_getc(compiler->input, &ch))
    {
      if (WS_IS_DECIMAL_DIGIT(ch))
	{
	  if (!ws_buffer_append_space(buffer, &p, 1))
	    {
	      ws_error_memory(compiler);
	      return WS_FALSE;
	    }
	  p[0] = (unsigned char) ch;
	}
      else
	{
	  ws_stream_ungetc(compiler->input, ch);
	  break;
	}
    }

  return read_float_from_exp(compiler, buffer, result);
}


static WsBool
read_float_from_exp(WsCompiler *compiler, WsBuffer *buffer, WsFloat32 *result)
{
  int ch;
  unsigned char *p;
  int sign = '+';

  /* Do we have an exponent part. */
  if (!ws_stream_getc(compiler->input, &ch))
    goto done;
  if (ch != 'e' && ch != 'E')
    {
      /* No exponent part. */
      ws_stream_ungetc(compiler->input, ch);
      goto done;
    }

  /* Sign. */
  if (!ws_stream_getc(compiler->input, &ch))
    {
      /* This is an error. */
      ws_src_error(compiler, 0, "truncated float literal");
      return WS_FALSE;
    }
  if (ch == '-')
    sign = '-';
  else if (ch == '+')
    sign = '+';
  else
    ws_stream_ungetc(compiler->input, ch);

  /* DecimalDigits. */
  if (!ws_stream_getc(compiler->input, &ch))
    {
      ws_src_error(compiler, 0, "truncated float literal");
      return WS_FALSE;
    }
  if (!WS_IS_DECIMAL_DIGIT(ch))
    {
      ws_src_error(compiler, 0, "no decimal digits in exponent part");
      return WS_FALSE;
    }

  /* Append exponent part read so far. */
  if (!ws_buffer_append_space(buffer, &p, 2))
    {
      ws_error_memory(compiler);
      return WS_FALSE;
    }
  p[0] = 'e';
  p[1] = sign;

  /* Read decimal digits. */
  while (WS_IS_DECIMAL_DIGIT(ch))
    {
      if (!ws_buffer_append_space(buffer, &p, 1))
	{
	  ws_error_memory(compiler);
	  return WS_FALSE;
	}
      p[0] = (unsigned char) ch;

      if (!ws_stream_getc(compiler->input, &ch))
	/* EOF.  This is ok. */
	goto done;
    }
  /* Unget the extra character. */
  ws_stream_ungetc(compiler->input, ch);

  /* FALLTHROUGH */

 done:

  if (!ws_buffer_append_space(buffer, &p, 1))
    {
      ws_error_memory(compiler);
      return WS_FALSE;
    }
  p[0] = 0;

  *result = (WsFloat32) strtod((char *) ws_buffer_ptr(buffer), NULL);

  return WS_TRUE;
}
