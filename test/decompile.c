/*
 * decompile.c - A program to test the WML compiler. This tool was written
 *               from the WBXML 1.2 and WML 1.1 specs.
 *
 * Author: Chris Wulff, Vanteon (cwulff@vanteon.com)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include "decompile.h"

const WBXML_MB_U_INT32 ZERO_WBXML_MB_U_INT32 = {0,0,0,0};

DTD_TYPE_LIST DTDTypeList[] =
{
	{1, "UNKNOWN"},
	{2, "-//WAPFORUM//DTD WML 1.0//EN"},
	{3, "-//WAPFORUM//DTD WTA 1.0//EN"},
	{4, "-//WAPFORUM//DTD WML 1.1//EN"},
	{5, "-//WAPFORUM//DTD SI 1.0//EN"},
	{6, "-//WAPFORUM//DTD SL 1.0//EN"},
	{7, "-//WAPFORUM//DTD CO 1.0//EN"},
	{8, "-//WAPFORUM//DTD CHANNEL 1.1//EN"},
	{9, "-//WAPFORUM//DTD WML 1.2//EN"},
	{0, NULL}
};


/**************************************
 * DTD Public Type 4 (WML 1.1) Tables *
 **************************************/

CODEPAGE_TAG_NAME_LIST CodepageTagNames[] =
{
	{"a",         0, 0x1c},
	{"anchor",    0, 0x22},
	{"access",    0, 0x23},
	{"b",         0, 0x24},
	{"big",       0, 0x25},
	{"br",        0, 0x26},
	{"card",      0, 0x27},
	{"do",        0, 0x28},
	{"em",        0, 0x29},
	{"fieldset",  0, 0x2a},
	{"go",        0, 0x2b},
	{"head",      0, 0x2c},
	{"i",         0, 0x2d},
	{"img",       0, 0x2e},
	{"input",     0, 0x2f},
	{"meta",      0, 0x30},
	{"noop",      0, 0x31},
	{"p",         0, 0x20},
	{"postfield", 0, 0x21},
	{"pre",       0, 0x1b},
	{"prev",      0, 0x32},
	{"onevent",   0, 0x33},
	{"optgroup",  0, 0x34},
	{"option",    0, 0x35},
	{"refresh",   0, 0x36},
	{"select",    0, 0x37},
	{"setvar",    0, 0x3e},
	{"small",     0, 0x38},
	{"strong",    0, 0x39},
	{"table",     0, 0x1f},
	{"td",        0, 0x1d},
	{"template",  0, 0x3b},
	{"timer",     0, 0x3c},
	{"tr",        0, 0x1e},
	{"u",         0, 0x3d},
	{"wml",       0, 0x3f},
	{NULL, 0, 0}
};

CODEPAGE_ATTRSTART_NAME_LIST CodepageAttrstartNames[] =
{
	{"accept-charset",  NULL,                                0, 0x05},
	{"accesskey",       NULL,                                0, 0x5e},
	{"align",           NULL,                                0, 0x52},
	{"align",           "bottom",                            0, 0x06},
	{"align",           "center",                            0, 0x07},
	{"align",           "left",                              0, 0x08},
	{"align",           "middle",                            0, 0x09},
	{"align",           "right",                             0, 0x0a},
	{"align",           "top",                               0, 0x0b},
	{"alt",             NULL,                                0, 0x0c},
	{"class",           NULL,                                0, 0x54},
	{"columns",         NULL,                                0, 0x53},
	{"content",         NULL,                                0, 0x0d},
	{"content",         "application/vnd.wap.wmlc;charset=", 0, 0x5c},
	{"domain",          NULL,                                0, 0x0f},
	{"emptyok",         "false",                             0, 0x10},
	{"emptyok",         "true",                              0, 0x11},
	{"enctype",         NULL,                                0, 0x5f},
	{"enctype",         "application/x-www-form-urlencoded", 0, 0x60},
	{"enctype",         "multipart/form-data",               0, 0x61},
	{"format",          NULL,                                0, 0x12},
	{"forua",           "false",                             0, 0x56},
	{"forua",           "true",                              0, 0x57},
	{"height",          NULL,                                0, 0x13},
	{"href",            NULL,                                0, 0x4a},
	{"href",            "http://",                           0, 0x4b},
	{"href",            "https://",                          0, 0x4c},
	{"hspace",          NULL,                                0, 0x14},
	{"http-equiv",      NULL,                                0, 0x5a},
	{"http-equiv",      "Content-Type",                      0, 0x5b},
	{"http-equiv",      "Expires",                           0, 0x5d},
	{"id",              NULL,                                0, 0x55},
	{"ivalue",          NULL,                                0, 0x15},
	{"iname",           NULL,                                0, 0x16},
	{"label",           NULL,                                0, 0x18},
	{"localsrc",        NULL,                                0, 0x19},
	{"maxlength",       NULL,                                0, 0x1a},
	{"method",          "get",                               0, 0x1b},
	{"method",          "post",                              0, 0x1c},
	{"mode",            "nowrap",                            0, 0x1d},
	{"mode",            "wrap",                              0, 0x1e},
	{"multiple",        "false",                             0, 0x1f},
	{"multiple",        "true",                              0, 0x20},
	{"name",            NULL,                                0, 0x21},
	{"newcontext",      "false",                             0, 0x22},
	{"newcontext",      "true",                              0, 0x23},
	{"onenterbackward", NULL,                                0, 0x25},
	{"onenterforward",  NULL,                                0, 0x26},
	{"onpick",          NULL,                                0, 0x24},
	{"ontimer",         NULL,                                0, 0x27},
	{"optional",        "false",                             0, 0x28},
	{"optional",        "true",                              0, 0x29},
	{"path",            NULL,                                0, 0x2a},
	{"scheme",          NULL,                                0, 0x2e},
	{"sendreferer",     "false",                             0, 0x2f},
	{"sendreferer",     "true",                              0, 0x30},
	{"size",            NULL,                                0, 0x31},
	{"src",             NULL,                                0, 0x32},
	{"src",             "http://",                           0, 0x58},
	{"src",             "https://",                          0, 0x59},
	{"ordered",         "true",                              0, 0x33},
	{"ordered",         "false",                             0, 0x34},
	{"tabindex",        NULL,                                0, 0x35},
	{"title",           NULL,                                0, 0x36},
	{"type",            NULL,                                0, 0x37},
	{"type",            "accept",                            0, 0x38},
	{"type",            "delete",                            0, 0x39},
	{"type",            "help",                              0, 0x3a},
	{"type",            "password",                          0, 0x3b},
	{"type",            "onpick",                            0, 0x3c},
	{"type",            "onenterbackward",                   0, 0x3d},
	{"type",            "onenterforward",                    0, 0x3e},
	{"type",            "ontimer",                           0, 0x3f},
	{"type",            "options",                           0, 0x45},
	{"type",            "prev",                              0, 0x46},
	{"type",            "reset",                             0, 0x47},
	{"type",            "text",                              0, 0x48},
	{"type",            "vnd.",                              0, 0x49},
	{"value",           NULL,                                0, 0x4d},
	{"vspace",          NULL,                                0, 0x4e},
	{"width",           NULL,                                0, 0x4f},
	{"xml:lang",        NULL,                                0, 0x50},
	{NULL,              NULL,                                0, 0}
};

CODEPAGE_ATTRVALUE_NAME_LIST CodepageAttrvalueNames[] =
{
	{".com/",           0, 0x85},
	{".edu/",           0, 0x86},
	{".net/",           0, 0x87},
	{".org/",           0, 0x88},
	{"accept",          0, 0x89},
	{"bottom",          0, 0x8a},
	{"clear",           0, 0x8b},
	{"delete",          0, 0x8c},
	{"help",            0, 0x8d},
	{"http://",         0, 0x8e},
	{"http://www.",     0, 0x8f},
	{"https://",        0, 0x90},
	{"https://www.",    0, 0x91},
	{"middle",          0, 0x93},
	{"nowrap",          0, 0x94},
	{"onenterbackward", 0, 0x96},
	{"onenterforward",  0, 0x97},
	{"onpick",          0, 0x95},
	{"ontimer",         0, 0x98},
	{"options",         0, 0x99},
	{"password",        0, 0x9a},
	{"reset",           0, 0x9b},
	{"text",            0, 0x9d},
	{"top",             0, 0x9e},
	{"unknown",         0, 0x9f},
	{"wrap",            0, 0xa0},
	{"www.",            0, 0xa1},
	{NULL, 0, 0}
};


/**************************
 * Node Tree Construction *
 **************************/

/*
 * Function: NewNode
 *
 * Description:
 *
 *  Allocate and initialize a new node. This links the new node
 *  as the first child of the current node in the buffer. This causes
 *  child nodes to be linked in reverse order. If there is no current
 *  node, then the new node will be linked in as the first child at the
 *  top of the tree.
 *
 * Parameters:
 *
 *  buffer - WBXML buffer to link the new node into
 *  type   - Type of node to allocate
 *
 * Return value:
 *
 *  P_WBXML_NODE - A pointer to the newly allocated node.
 *
 */
P_WBXML_NODE NewNode(P_WBXML_INFO buffer, WBXML_NODE_TYPE type)
{
	if (buffer)
	{
		P_WBXML_NODE newnode = malloc(sizeof(WBXML_NODE));

		if (newnode)
		{
			newnode->m_prev = NULL;
			newnode->m_child = NULL;

			if (buffer->m_curnode)
			{
				/* Insert this node as the first child of the current node */
				newnode->m_parent = buffer->m_curnode;
				newnode->m_next = buffer->m_curnode->m_child;

				if (buffer->m_curnode->m_child)
				{
					((P_WBXML_NODE)buffer->m_curnode->m_child)->m_prev = newnode;
				}

				buffer->m_curnode->m_child = newnode;
			}
			else
			{
				/* Insert this node at the top of the tree */
				newnode->m_parent = NULL;
				newnode->m_next = buffer->m_tree;
				
				if (buffer->m_tree)
				{
					buffer->m_tree->m_prev = newnode;
				}

				buffer->m_tree = newnode;
			}

			newnode->m_page = buffer->m_curpage;
			newnode->m_type = type;
			newnode->m_data = NULL;
		}
		else
		{
			ParseError(ERR_NOT_ENOUGH_MEMORY);
		}

		return newnode;
	}
	else
	{
		ParseError(ERR_INTERNAL_BAD_PARAM);
	}

	return NULL;
}

/*
 * Function: FreeNode
 *
 * Description:
 *
 *  Free a node, all its children and forward siblings.
 *
 * Parameters:
 *
 *  node - The node to free
 *
 */
void FreeNode(P_WBXML_NODE node)
{
	if (node)
	{
		if (node->m_child)
		{
			FreeNode(node->m_child);
		}

		if (node->m_next)
		{
			FreeNode(node->m_next);
		}

		free(node);
	}
}

void AddDTDNode(P_WBXML_INFO buffer, const WBXML_DTD_TYPE dtdnum, const WBXML_MB_U_INT32 index)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_DTD_TYPE);
	newnode->m_data = malloc(sizeof(DTD_NODE_DATA));
	memcpy( &( ((DTD_NODE_DATA*)newnode->m_data)->m_dtdnum ), &(dtdnum[0]), sizeof(WBXML_MB_U_INT32) );
	memcpy( &( ((DTD_NODE_DATA*)newnode->m_data)->m_index ), &(index[0]), sizeof(WBXML_MB_U_INT32) );
}

void AddStringTableNode(P_WBXML_INFO buffer, const P_WBXML_STRING_TABLE strings)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_STRING_TABLE);
	newnode->m_data = malloc(sizeof(WBXML_STRING_TABLE));
	memcpy( newnode->m_data, strings, sizeof(WBXML_STRING_TABLE) );
}

void AddCodepageTagNode(P_WBXML_INFO buffer, WBXML_TAG tag)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_CODEPAGE_TAG);
	newnode->m_data = malloc(sizeof(WBXML_TAG));
	*((P_WBXML_TAG)newnode->m_data) = tag;
}

void AddCodepageLiteralTagNode(P_WBXML_INFO buffer, WBXML_MB_U_INT32 index)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_CODEPAGE_LITERAL_TAG);
	newnode->m_data = malloc(sizeof(WBXML_MB_U_INT32));
	memcpy( ((P_WBXML_MB_U_INT32)newnode->m_data), &index, sizeof(WBXML_MB_U_INT32) );
}

void AddAttrStartNode(P_WBXML_INFO buffer, WBXML_TAG tag)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_ATTRSTART);
	newnode->m_data = malloc(sizeof(WBXML_TAG));
	*((P_WBXML_TAG)newnode->m_data) = tag;
}

void AddAttrStartLiteralNode(P_WBXML_INFO buffer, WBXML_MB_U_INT32 index)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_ATTRSTART_LITERAL);
	newnode->m_data = malloc(sizeof(WBXML_MB_U_INT32));
	memcpy( ((P_WBXML_MB_U_INT32)newnode->m_data), &index, sizeof(WBXML_MB_U_INT32) );
}

void AddAttrValueNode(P_WBXML_INFO buffer, WBXML_TAG tag)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_ATTRVALUE);
	newnode->m_data = malloc(sizeof(WBXML_TAG));
	*((P_WBXML_TAG)newnode->m_data) = tag;
}

void AddAttrEndNode(P_WBXML_INFO buffer)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_ATTREND);
	newnode->m_data = NULL;
}

void AddStringNode(P_WBXML_INFO buffer, char* string)
{
	P_WBXML_NODE newnode = NewNode(buffer, NODE_STRING);
	newnode->m_data = strdup(string);
}

void AddVariableStringNode(P_WBXML_INFO buffer, char* string, WBXML_VARIABLE_TYPE type)
{
	// TODO: add this node
}

void AddVariableIndexNode(P_WBXML_INFO buffer, char* string, WBXML_VARIABLE_TYPE type)
{
	// TODO: add this node
}


/****************
 * Flow Control *
 ****************/

void Message(char* msg)
{
  printf("%s\n", msg);
}

void ParseError(WBXML_PARSE_ERROR error)
{
  switch (error)
  {
    case ERR_END_OF_DATA:
      Message("Input stream is incomplete (EOF).");
      break;

    case ERR_INTERNAL_BAD_PARAM:
      Message("Internal error: Bad parameter.");
      break;

	case ERR_TAG_NOT_FOUND:
      Message("Tag not found.");
      break;
		
	case ERR_FILE_NOT_FOUND:
      Message("File not found.");
      break;

	case ERR_FILE_NOT_READ:
      Message("File read error.");
      break;

	case ERR_NOT_ENOUGH_MEMORY:
      Message("Not enough memory");
      break;

    default:
      Message("Unknown error.");
      break;
  }

  exit(error);
}

void ParseWarning(WBXML_PARSE_WARNING warning)
{
  switch (warning)
  {
    case WARN_FUTURE_EXPANSION_EXT_0:
      Message("Token EXT_0 encountered. This token is reserved for future expansion.");
      break;

    case WARN_FUTURE_EXPANSION_EXT_1:
      Message("Token EXT_1 encountered. This token is reserved for future expansion.");
      break;

    case WARN_FUTURE_EXPANSION_EXT_2:
      Message("Token EXT_2 encountered. This token is reserved for future expansion.");
      break;

    default:
      Message("Unknown warning.");
      break;
  }
}

WBXML_LENGTH BytesLeft(P_WBXML_INFO buffer)
{
  if (buffer)
  {
    WBXML_LENGTH bytesRead = (buffer->m_curpos - buffer->m_start);
    if (bytesRead >= buffer->m_length)
    {
      return 0;
    }
    else
    {
      return (buffer->m_length - bytesRead);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return 0;
}

BOOL IsTag(P_WBXML_INFO buffer, WBXML_TAG tag)
{
  BOOL result = FALSE;

  if (buffer)
  {
    if (BytesLeft(buffer) >= sizeof(WBXML_TAG))
    {
      result = ((*((WBXML_TAG*) buffer->m_curpos)) == tag);
    }
    else
    {
		/* No more data, so nope, not this tag */
      result = FALSE;
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return result;
}

BOOL IsCodepageTag(P_WBXML_INFO buffer, CP_TAG_TYPE type)
{
	WBXML_U_INT8 result = *(buffer->m_curpos);

	/* NOTE THAT THESE ARE NOT UNIQUE! */
	switch (type)
	{
		case CP_TAG_TAG:
			return TRUE;
		case CP_TAG_ATTRSTART:
			return ((result & 0x80) != 0x80);
		case CP_TAG_ATTRVALUE:
			return ((result & 0x80) == 0x80);
		default:
			return FALSE;
	}
}

BOOL Is_attrValue  (P_WBXML_INFO buffer)
{
	WBXML_INFO tmpbuffer;
	memcpy(&tmpbuffer, buffer, sizeof(WBXML_INFO));
	tmpbuffer.m_curpos += SWITCHPAGE_SIZE;

	return ((Is_switchPage(buffer) && IsCodepageTag(&tmpbuffer, CP_TAG_ATTRVALUE)) ||
		    IsCodepageTag(buffer, CP_TAG_ATTRVALUE) ||
			Is_string(buffer) ||
			Is_extension(buffer) ||
			Is_entity(buffer) ||
			Is_pi(buffer) ||
			Is_opaque(buffer));
}

BOOL Is_extension  (P_WBXML_INFO buffer)
{
	WBXML_INFO tmpbuffer;
	memcpy(&tmpbuffer, buffer, sizeof(WBXML_INFO));
	tmpbuffer.m_curpos += SWITCHPAGE_SIZE;

	return ((Is_switchPage(buffer) &&
		     (IsTag(&tmpbuffer, TAG_EXT_0) ||
		      IsTag(&tmpbuffer, TAG_EXT_1) ||
		      IsTag(&tmpbuffer, TAG_EXT_2) ||
		      IsTag(&tmpbuffer, TAG_EXT_T_0) ||
		      IsTag(&tmpbuffer, TAG_EXT_T_1) ||
		      IsTag(&tmpbuffer, TAG_EXT_T_2) ||
		      IsTag(&tmpbuffer, TAG_EXT_I_0) ||
		      IsTag(&tmpbuffer, TAG_EXT_I_1) ||
		      IsTag(&tmpbuffer, TAG_EXT_I_2))) ||
		    (IsTag(buffer, TAG_EXT_0) ||
		     IsTag(buffer, TAG_EXT_1) ||
		     IsTag(buffer, TAG_EXT_2) ||
		     IsTag(buffer, TAG_EXT_T_0) ||
		     IsTag(buffer, TAG_EXT_T_1) ||
		     IsTag(buffer, TAG_EXT_T_2) ||
		     IsTag(buffer, TAG_EXT_I_0) ||
		     IsTag(buffer, TAG_EXT_I_1) ||
		     IsTag(buffer, TAG_EXT_I_2)));
}

BOOL Is_string     (P_WBXML_INFO buffer)
{
	return (Is_inline(buffer) ||
		    Is_tableref(buffer));
}

BOOL Is_switchPage (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_SWITCH_PAGE);
}

BOOL Is_inline     (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_STR_I);
}

BOOL Is_tableref   (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_STR_T);
}

BOOL Is_entity     (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_ENTITY);
}

BOOL Is_pi         (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_PI);
}

BOOL Is_opaque     (P_WBXML_INFO buffer)
{
	return IsTag(buffer, TAG_OPAQUE);
}

BOOL Is_zero(P_WBXML_INFO buffer)
{
  BOOL result = FALSE;

  if (buffer) 
  {
    if (BytesLeft(buffer) >= 1) 
    {
      result = ((*buffer->m_curpos) == 0);
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return result;
}


/***********************
 * Basic Type Decoders *
 ***********************/

void Read_u_int8(P_WBXML_INFO buffer, P_WBXML_U_INT8 result)
{
  if (buffer && result)
  {
    if (BytesLeft(buffer) >= 1) 
    {
      *result = *(buffer->m_curpos);
      (buffer->m_curpos)++;
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }
}

void Read_mb_u_int32(P_WBXML_INFO buffer, P_WBXML_MB_U_INT32 result)
{
  if (buffer && result)
  {
    int i;
    for (i = 0; i < MAX_MB_U_INT32_BYTES; i++)
    {
      if (BytesLeft(buffer) >= 1)
      {
        (*result)[i] = *(buffer->m_curpos);
        (buffer->m_curpos)++;

        if ( !( (*result)[i] & 0x80 ) )
          break;
      }
      else
      {
        ParseError(ERR_END_OF_DATA);
      }
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }
}

void Read_zero_index(P_WBXML_INFO buffer, P_WBXML_STRING_INDEX result)
{
  WBXML_U_INT8 dummy;
  Read_u_int8(buffer, &dummy);
  Read_mb_u_int32(buffer, result);
}

void Read_bytes(P_WBXML_INFO buffer, WBXML_LENGTH length, P_WBXML_BYTES result)
{
  if (buffer && result)
  {
    if (BytesLeft(buffer) >= length) 
    {
      *result = (WBXML_BYTES) malloc(length*sizeof(unsigned char));
      memcpy(*result, buffer->m_curpos, length);
      buffer->m_curpos += length;
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }
}

void ReadFixedTag(P_WBXML_INFO buffer, WBXML_TAG tag)
{
  if (buffer)
  {
    if (BytesLeft(buffer) >= sizeof(WBXML_TAG))
    {
      if ((*((WBXML_TAG*) buffer->m_curpos)) == tag)
      {
        buffer->m_curpos += sizeof(WBXML_TAG);
      }
      else
      {
        ParseError(ERR_TAG_NOT_FOUND);
      }
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }
}

WBXML_TAG ReadCodepageTag (P_WBXML_INFO buffer, CP_TAG_TYPE type)
{
  WBXML_TAG tag = 0;

  if (buffer)
  {
    if (BytesLeft(buffer) >= sizeof(WBXML_TAG))
    {
      tag = *((WBXML_TAG*) buffer->m_curpos);

	  switch (type)
	  {
	    case CP_TAG_TAG:
		  buffer->m_curpos += sizeof(WBXML_TAG);
		  break;

		case CP_TAG_ATTRSTART:
		  if ((tag & 0x80) != 0x80)
		  {
		    buffer->m_curpos += sizeof(WBXML_TAG);
		  }
		  else
		  {
			  ParseError(ERR_TAG_NOT_FOUND);
		  }
		  break;

		case CP_TAG_ATTRVALUE:
		  if ((tag & 0x80) == 0x80)
		  {
		    buffer->m_curpos += sizeof(WBXML_TAG);
		  }
		  else
		  {
			  ParseError(ERR_TAG_NOT_FOUND);
		  }
		  break;

		default:
		  ParseError(ERR_TAG_NOT_FOUND);
		  break;
      }
    }
    else
    {
      ParseError(ERR_END_OF_DATA);
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return tag;
}


/**************************
 * Basic Type Conversions *
 **************************/

long mb_u_int32_to_long(P_WBXML_MB_U_INT32 value)
{
  long result = 0;

  if (value)
  {
    int i;
    for (i = 0; i < MAX_MB_U_INT32_BYTES; i++)
    {
      result <<= 7;
      result |= ((*value)[i] & 0x7f);

      if ( !( (*value)[i] & 0x80 ) )
        break;
    }
  }
  else
  {
    ParseError(ERR_INTERNAL_BAD_PARAM);
  }

  return result;
}

void OutputEncodedString(const unsigned char* str)
{
	/* Work our way down the string looking for illegal chars */
	while (*str != 0)
	{
		if ((*str < 0x20) || (*str > 0x7F))
		{
			/* Out of range... encode */
			printf("&#x%02x;", *str);
		}
		else
		{
			switch (*str)
			{
				case '<':
				case '>':
				case '&':
				case '\'':
				case '\"':
					/* Special symbol... encode */
					printf("&#x%2x", *str);
					break;

				default:
					printf("%c", *str);
					break;
			}
		}

		str++;
	}
}


/*******************************
 * Document Structure Decoders *
 *******************************/

void Read_start      (P_WBXML_INFO buffer)
{
  Read_version(buffer);
  Read_publicid(buffer);
  Read_charset(buffer);
  Read_strtbl(buffer);
  Read_body(buffer);
}

void Read_strtbl     (P_WBXML_INFO buffer)
{
  WBXML_STRING_TABLE result;
  Read_mb_u_int32(buffer, &(result.m_length));
  Read_bytes(buffer, mb_u_int32_to_long(&(result.m_length)), &(result.m_strings));

  AddStringTableNode(buffer, &result);
}

void Read_body       (P_WBXML_INFO buffer)
{
  while (Is_pi(buffer))
  {
    Read_pi(buffer);
  }

  Read_element(buffer);

  while (Is_pi(buffer))
  {
    Read_pi(buffer);
  }
}

void Read_element    (P_WBXML_INFO buffer)
{
  WBXML_TAG stagvalue = 0;

  if (Is_switchPage(buffer))
  {
    Read_switchPage(buffer);
  }

  stagvalue = Read_stag(buffer);

  /* move the current node down to this one in the tree */
  if (buffer->m_curnode)
	  buffer->m_curnode = buffer->m_curnode->m_child;
  else buffer->m_curnode = buffer->m_tree;

  if ((stagvalue & CODEPAGE_TAG_HAS_ATTRS) == CODEPAGE_TAG_HAS_ATTRS)
  {
    do
    {
      Read_attribute(buffer);

    } while (!IsTag(buffer, TAG_END));

    ReadFixedTag(buffer, TAG_END);

	AddAttrEndNode(buffer);
  }

  if ((stagvalue & CODEPAGE_TAG_HAS_CONTENT) == CODEPAGE_TAG_HAS_CONTENT)
  {
    while (!IsTag(buffer, TAG_END))
	{
      Read_content(buffer);
	}

    ReadFixedTag(buffer, TAG_END);
  }

  /* move the current node back up one */
  buffer->m_curnode = buffer->m_curnode->m_parent;
}

void Read_content    (P_WBXML_INFO buffer)
{
	if (Is_string(buffer))
	{
		Read_string(buffer);
	}
	else if (Is_extension(buffer))
	{
		Read_extension(buffer);
	}
	else if (Is_entity(buffer))
	{
		Read_entity(buffer);
	}
	else if (Is_pi(buffer))
	{
		Read_pi(buffer);
	}
	else if (Is_opaque(buffer))
	{
		Read_opaque(buffer);
	}
	else
	{
		/* Assume it is an element */
		Read_element(buffer);
	}
}

WBXML_TAG Read_stag       (P_WBXML_INFO buffer)
{
	if (IsCodepageTag(buffer, CP_TAG_TAG))
	{
		WBXML_TAG tag = ReadCodepageTag(buffer, CP_TAG_TAG);

		AddCodepageTagNode(buffer, tag);

		return tag;
	}
	else if (IsTag(buffer, TAG_LITERAL))
	{
		WBXML_MB_U_INT32 index;

		ReadFixedTag(buffer, TAG_LITERAL);
		Read_index(buffer, &index);

		AddCodepageLiteralTagNode(buffer, index);
	}
	else
	{
		ParseError(ERR_TAG_NOT_FOUND);
	}

	return 0;
}

void Read_attribute  (P_WBXML_INFO buffer)
{
	Read_attrStart(buffer);

	while (Is_attrValue(buffer))
	{
		Read_attrValue(buffer);
	}
}

void Read_attrStart  (P_WBXML_INFO buffer)
{
  if (Is_switchPage(buffer))
  {
	WBXML_TAG tag;
    Read_switchPage(buffer);
    tag = ReadCodepageTag(buffer, CP_TAG_ATTRSTART);

	AddAttrStartNode(buffer, tag);
  }
  else if (IsCodepageTag(buffer, CP_TAG_ATTRSTART))
  {
	WBXML_TAG tag;
    tag = ReadCodepageTag(buffer, CP_TAG_ATTRSTART);

	AddAttrStartNode(buffer, tag);
  }
  else if (IsTag(buffer, TAG_LITERAL))
  {
    WBXML_MB_U_INT32 index;

    ReadFixedTag(buffer, TAG_LITERAL);
    Read_index(buffer, &index);

	AddAttrStartLiteralNode(buffer, index);
  }
  else
  {
    ParseError(ERR_TAG_NOT_FOUND);
  }
}

void Read_attrValue  (P_WBXML_INFO buffer)
{
  if (Is_switchPage(buffer))
  {
	WBXML_TAG tag;
    Read_switchPage(buffer);
    tag = ReadCodepageTag(buffer, CP_TAG_ATTRVALUE);
	AddAttrValueNode(buffer, tag);
  }
  else if (IsCodepageTag(buffer, CP_TAG_ATTRVALUE))
  {
	WBXML_TAG tag;
    tag = ReadCodepageTag(buffer, CP_TAG_ATTRVALUE);
	AddAttrValueNode(buffer, tag);
  }
  else if (Is_string(buffer))
  {
    Read_string(buffer);
  }
  else if (Is_extension(buffer))
  {
    Read_extension(buffer);
  }
  else if (Is_entity(buffer))
  {
    Read_entity(buffer);
  }
  else if (Is_opaque(buffer))
  {
    Read_opaque(buffer);
  }
  else
  {
    ParseError(ERR_TAG_NOT_FOUND);
  }
}

void Read_extension  (P_WBXML_INFO buffer)
{
	if (Is_switchPage(buffer))
	{
		Read_switchPage(buffer);
	}

	if (IsTag(buffer, TAG_EXT_I_0))
	{
		char* str = NULL;

		ReadFixedTag(buffer, TAG_EXT_I_0);
		Read_termstr_rtn(buffer, &str);

		AddVariableStringNode(buffer, str, VAR_ESCAPED); 
	}
	else if (IsTag(buffer, TAG_EXT_I_1))
	{
		char* str = NULL;

		ReadFixedTag(buffer, TAG_EXT_I_1);
		Read_termstr_rtn(buffer, &str);

		AddVariableStringNode(buffer, str, VAR_UNESCAPED); 
	}
	else if (IsTag(buffer, TAG_EXT_I_2))
	{
		char* str = NULL;

		ReadFixedTag(buffer, TAG_EXT_I_2);
		Read_termstr_rtn(buffer, &str);

		AddVariableStringNode(buffer, str, VAR_UNCHANGED); 
	}
	else if (IsTag(buffer, TAG_EXT_T_0))
	{
		WBXML_MB_U_INT32 index;

		ReadFixedTag(buffer, TAG_EXT_T_0);
		Read_index(buffer, &index);

		AddVariableIndexNode(buffer, index, VAR_ESCAPED);
	}
	else if (IsTag(buffer, TAG_EXT_T_1))
	{
		WBXML_MB_U_INT32 index;

		ReadFixedTag(buffer, TAG_EXT_T_1);
		Read_index(buffer, &index);

		AddVariableIndexNode(buffer, index, VAR_UNESCAPED);
	}
	else if (IsTag(buffer, TAG_EXT_T_2))
	{
		WBXML_MB_U_INT32 index;

		ReadFixedTag(buffer, TAG_EXT_T_2);
		Read_index(buffer, &index);

		AddVariableIndexNode(buffer, index, VAR_UNCHANGED);
	}
	else if (IsTag(buffer, TAG_EXT_0))
	{
		ReadFixedTag(buffer, TAG_EXT_0);

		ParseWarning(WARN_FUTURE_EXPANSION_EXT_0);
	}
	else if (IsTag(buffer, TAG_EXT_1))
	{
		ReadFixedTag(buffer, TAG_EXT_1);

		ParseWarning(WARN_FUTURE_EXPANSION_EXT_1);
	}
	else if (IsTag(buffer, TAG_EXT_2))
	{
		ReadFixedTag(buffer, TAG_EXT_2);

		ParseWarning(WARN_FUTURE_EXPANSION_EXT_2);
	}
	else
	{
		ParseError(ERR_TAG_NOT_FOUND);
	}
}

void Read_string     (P_WBXML_INFO buffer)
{
	if (Is_inline(buffer))
	{
		Read_inline(buffer);
	}
	else if (Is_tableref(buffer))
	{
		Read_tableref(buffer);
	}
	else
	{
		ParseError(ERR_TAG_NOT_FOUND);
	}
}

void Read_switchPage (P_WBXML_INFO buffer)
{
  WBXML_U_INT8 pageindex;

  ReadFixedTag(buffer, TAG_SWITCH_PAGE);
  Read_pageindex(buffer, &pageindex);
}

void Read_inline     (P_WBXML_INFO buffer)
{
	ReadFixedTag(buffer, TAG_STR_I);
	Read_termstr(buffer);
}

void Read_tableref   (P_WBXML_INFO buffer)
{
  WBXML_MB_U_INT32 index;

  ReadFixedTag(buffer, TAG_STR_T);
  Read_index(buffer, &index);
}

void Read_entity     (P_WBXML_INFO buffer)
{
	ReadFixedTag(buffer, TAG_ENTITY);
	Read_entcode(buffer);
}

void Read_entcode    (P_WBXML_INFO buffer)
{
	WBXML_MB_U_INT32 result;
	Read_mb_u_int32(buffer, &result);
}

void Read_pi         (P_WBXML_INFO buffer)
{
  ReadFixedTag(buffer, TAG_PI);
  Read_attrStart(buffer);
  
  while (Is_attrValue(buffer))
  {
    Read_attrValue(buffer);
  }

  ReadFixedTag(buffer, TAG_END);
}

void Read_opaque     (P_WBXML_INFO buffer)
{
  WBXML_MB_U_INT32 length;
  WBXML_BYTES      data;

  ReadFixedTag(buffer, TAG_OPAQUE);
  Read_length(buffer, &length);
  Read_bytes(buffer, mb_u_int32_to_long(&length), &data);
}

void Read_version    (P_WBXML_INFO buffer)
{
  WBXML_U_INT8 result;

  Read_u_int8(buffer, &result);
}

void Read_publicid   (P_WBXML_INFO buffer)
{
  if (Is_zero(buffer))
  {
    WBXML_MB_U_INT32 index;

    Read_index(buffer, &index);

	AddDTDNode(buffer, ZERO_WBXML_MB_U_INT32, index);
  }
  else
  {
    WBXML_MB_U_INT32 result;

    Read_mb_u_int32(buffer, &result);

	AddDTDNode(buffer, result, ZERO_WBXML_MB_U_INT32);
  }
}

void Read_charset    (P_WBXML_INFO buffer)
{
  WBXML_MB_U_INT32 result;

  Read_mb_u_int32(buffer, &result);
}

void Read_termstr_rtn(P_WBXML_INFO buffer, char** result)
{

#define STRING_BLOCK_SIZE 256

	int buflen = STRING_BLOCK_SIZE;
	char* strbuf = (char*) malloc(buflen);
	int i = 0;

	if (!result)
		ParseError(ERR_INTERNAL_BAD_PARAM);

	while ( (BytesLeft(buffer) >= 1) && (*(buffer->m_curpos) != 0) )
	{
		if (i>=buflen)
		{
			buflen += STRING_BLOCK_SIZE;
			strbuf = realloc(strbuf, buflen);
		}

		strbuf[i] = *(buffer->m_curpos);
		buffer->m_curpos++;
		i++;
	}

	strbuf[i] = 0;
	buffer->m_curpos++;

	if (*result)
		free(*result);

	*result = strbuf;
}

void Read_termstr    (P_WBXML_INFO buffer)
{
	char* strbuf = NULL;

	Read_termstr_rtn(buffer, &strbuf);

	AddStringNode(buffer, strbuf);

	free(strbuf);
}

void Read_index      (P_WBXML_INFO buffer, P_WBXML_MB_U_INT32 result)
{
  Read_mb_u_int32(buffer, result);
}

void Read_length     (P_WBXML_INFO buffer, P_WBXML_MB_U_INT32 result)
{
  Read_mb_u_int32(buffer, result);
}

void Read_zero       (P_WBXML_INFO buffer)
{
  WBXML_U_INT8 result;

  Read_u_int8(buffer, &result);

  if (result != (WBXML_U_INT8) 0)
  {
    ParseError(ERR_TAG_NOT_FOUND);
  }
}

void Read_pageindex  (P_WBXML_INFO buffer, P_WBXML_U_INT8 result)
{
  Read_u_int8(buffer, result);
}

void Init(P_WBXML_INFO buffer)
{
	buffer->m_start = NULL;
	buffer->m_curpos = NULL;
	buffer->m_length = 0;
	buffer->m_tree = NULL;
	buffer->m_curnode = NULL;
	buffer->m_curpage = 0;
}

void Free(P_WBXML_INFO buffer)
{
	if (buffer->m_start)
	{
		free(buffer->m_start);
		buffer->m_start = NULL;
	}

	buffer->m_curpos = NULL;
	buffer->m_length = 0;

	FreeNode(buffer->m_tree);
	buffer->m_tree = NULL;
}

long FileSize(FILE* file)
{
	long curpos = ftell(file);
	long endpos;
	fseek(file, 0, SEEK_END);
	endpos = ftell(file);
	fseek(file, curpos, SEEK_SET);

	return endpos;
}

void ReadBinary(P_WBXML_INFO buffer, char* filename)
{
	if (buffer && filename)
	{
		FILE* file = fopen(filename, "r");
		if (!file)
		{
			ParseError(ERR_FILE_NOT_FOUND);
		}

		buffer->m_length = FileSize(file);
		buffer->m_start = (P_WBXML) malloc(buffer->m_length);
		buffer->m_curpos = buffer->m_start;

		if (!buffer->m_start)
		{
			fclose(file);
			ParseError(ERR_NOT_ENOUGH_MEMORY);
		}

		if (fread(buffer->m_start, 1, buffer->m_length, file) != buffer->m_length)
		{
			fclose(file);
			ParseError(ERR_FILE_NOT_READ);
		}
		else
		{
			fclose(file);
		}
	}
	else
	{
		ParseError(ERR_INTERNAL_BAD_PARAM);
	}
}

const char* DTDTypeName(long dtdnum)
{
	int i = 0;

	/* Search the DTD list for a match */
	while (DTDTypeList[i].m_name)
	{
		if (DTDTypeList[i].m_id == dtdnum)
		{
			break;
		}

		i++;
	}

	return DTDTypeList[i].m_name;
}

const char* CodepageTagName(WBXML_CODEPAGE page, WBXML_TAG tag)
{
	int i = 0;

	/* Strip flags off of the tag */
	tag = (WBXML_TAG) (tag & CODEPAGE_TAG_MASK);

	/* Search the tag list for a match */
	while (CodepageTagNames[i].m_name)
	{
		if ((CodepageTagNames[i].m_page == page) &&
			(CodepageTagNames[i].m_tag == tag))
		{
			break;
		}

		i++;
	}

	return CodepageTagNames[i].m_name;
}

const char* CodepageAttrstartName(WBXML_CODEPAGE page, WBXML_TAG tag, char** value)
{
	int i = 0;

	/* Check Parameters */
	if (!value)
	{
		ParseError(ERR_INTERNAL_BAD_PARAM);
	}

	/* Search the tag list for a match */
	while (CodepageAttrstartNames[i].m_name)
	{
		if ((CodepageAttrstartNames[i].m_page == page) &&
			(CodepageAttrstartNames[i].m_tag == tag))
		{
			break;
		}

		i++;
	}

	/* Duplicate the value because it may be concatenated to */
	if (CodepageAttrstartNames[i].m_valueprefix)
	{
		*value = strdup(CodepageAttrstartNames[i].m_valueprefix);
	}
	else
	{
		*value = NULL;
	}

	/* Return the tag name */
	return CodepageAttrstartNames[i].m_name;
}

void CodepageAttrvalueName(WBXML_CODEPAGE page, WBXML_TAG tag, char** value)
{
	int i = 0;

	/* Check Parameters */
	if (!value)
	{
		ParseError(ERR_INTERNAL_BAD_PARAM);
	}

	/* Search the tag list for a match */
	while (CodepageAttrvalueNames[i].m_name)
	{
		if ((CodepageAttrvalueNames[i].m_page == page) &&
			(CodepageAttrvalueNames[i].m_tag == tag))
		{
			break;
		}

		i++;
	}

	/* concatenate the value */
	if (CodepageAttrvalueNames[i].m_name)
	{
		if (*value)
		{
			*value = realloc(*value, strlen(*value) + strlen(CodepageAttrvalueNames[i].m_name) + 1);
			strcat(*value, CodepageAttrvalueNames[i].m_name);
		}
		else
		{
			*value = strdup(CodepageAttrvalueNames[i].m_name);
		}
	}
}

const char* GetStringTableString(P_WBXML_NODE node, long index)
{
	/* Find the string table node */

	P_WBXML_NODE pStringsNode = node;

	while (pStringsNode->m_parent)
	{
		pStringsNode = pStringsNode->m_parent;
	}

	while (pStringsNode->m_next)
	{
		pStringsNode = pStringsNode->m_next;
	}

	while (pStringsNode->m_prev && pStringsNode->m_type != NODE_STRING_TABLE)
	{
		pStringsNode = pStringsNode->m_prev;
	}

	if (pStringsNode->m_type != NODE_STRING_TABLE)
	{
		return "!!NO STRING TABLE!!";
	}

	/* Find the indexed string */

	if ((index >= 0) && (index < mb_u_int32_to_long(&((P_WBXML_STRING_TABLE)pStringsNode->m_data)->m_length)))
	{
		return (const char*) &(((P_WBXML_STRING_TABLE)pStringsNode->m_data)->m_strings[index]);
	}
	else
	{
		return "!!STRING TABLE INDEX TOO LARGE!!";
	}
}

void DumpNode(P_WBXML_NODE node, int indent, BOOL *inattrs, BOOL hascontent, char** value)
{
	P_WBXML_NODE curnode = node->m_child;

	WBXML_TAG nodetype = 0;
	long dtdnum = 0;

	BOOL bAttributesFollow = FALSE;
	BOOL bHasContent = FALSE;

	int i;

	if (!(*inattrs))
	{
		for (i=0; i<indent; i++)
		{
			printf(" ");
		}
	}
	else
	{
		if ((node->m_type != NODE_ATTRVALUE) && (*value))
		{
			printf("=\"");
			OutputEncodedString((unsigned char*) *value);
			printf("\"");
			free(*value);
			*value = NULL;
		}
	}

	switch (node->m_type)
	{
		case NODE_DTD_TYPE:
			printf("<?xml version=\"1.0\"?>\n<!DOCTYPE wml PUBLIC ");

			dtdnum = mb_u_int32_to_long( &((DTD_NODE_DATA*)node->m_data)->m_dtdnum );
			if ( dtdnum == 0)
			{
				printf("\"%s\">\n\n", GetStringTableString(node, mb_u_int32_to_long(&((DTD_NODE_DATA*)node->m_data)->m_index)) );
			}
			else
			{
				printf("\"%s\">\n\n", DTDTypeName(dtdnum) );
			}
			break;

		case NODE_CODEPAGE_TAG:
			nodetype = *((P_WBXML_TAG)node->m_data);
			if ((nodetype & CODEPAGE_TAG_MASK) == nodetype)
			{
				printf("<%s/>\n", CodepageTagName(node->m_page, nodetype));
			}
			else
			{
				if ((nodetype & CODEPAGE_TAG_HAS_CONTENT) == CODEPAGE_TAG_HAS_CONTENT)
				{
					bHasContent = TRUE;
				}

				if ((nodetype & CODEPAGE_TAG_HAS_ATTRS) == CODEPAGE_TAG_HAS_ATTRS)
				{
					printf("<%s", CodepageTagName(node->m_page, nodetype));
					bAttributesFollow = TRUE;
				}
				else
				{
					printf("<%s>\n", CodepageTagName(node->m_page, nodetype));
				}
			}
			break;

		case NODE_CODEPAGE_LITERAL_TAG:
			printf("<%s>\n", GetStringTableString(node, mb_u_int32_to_long(((P_WBXML_MB_U_INT32)node->m_data))) );
			break;

		case NODE_ATTRSTART:
			printf(" %s", CodepageAttrstartName(node->m_page, *((P_WBXML_TAG)node->m_data), value) );
			break;

		case NODE_ATTRSTART_LITERAL:
			printf(" %s", GetStringTableString(node, mb_u_int32_to_long(((P_WBXML_MB_U_INT32)node->m_data))) );
			break;

		case NODE_ATTRVALUE:
			CodepageAttrvalueName(node->m_page, *((P_WBXML_TAG)node->m_data), value);
			break;

		case NODE_ATTREND:
			if (!hascontent)
			{
				printf("/");
			}
			printf(">\n");
			*inattrs = FALSE;
			break;

		case NODE_STRING:
			if (*inattrs)
			{
				/* concatenate the value */
				if (*value)
				{
					if (node->m_data)
					{
						*value = realloc(*value, strlen(*value) + strlen((char*) node->m_data) + 1);
						strcat(*value, (char*) node->m_data);
					}
				}
				else
				{
					if (node->m_data)
					{
						*value = strdup((char*) node->m_data);
					}
				}
			}
			else
			{
				OutputEncodedString((unsigned char*) node->m_data);
				printf("\n");
			}
			break;

		case NODE_VARIABLE_STRING:
			// TODO: output variable string
			break;

		case NODE_VARIABLE_INDEX:
			// TODO: output variable string
			break;

		default:
			break;
	}

	indent += 2;

	if (curnode)
	{
		while (curnode->m_next) curnode = curnode->m_next;

		while (curnode)
		{
			DumpNode(curnode, indent, &bAttributesFollow, bHasContent, value);
			curnode = curnode->m_prev;
		}
	}

	indent -= 2;

	/* Output the element end if we have one */
	if ((nodetype & CODEPAGE_TAG_HAS_CONTENT) == CODEPAGE_TAG_HAS_CONTENT)
	{
		for (i=0; i<indent; i++)
		{
			printf(" ");
		}

		switch (node->m_type)
		{
			case NODE_CODEPAGE_TAG:
				printf("</%s>\n", CodepageTagName(node->m_page, *((P_WBXML_TAG)node->m_data)) );
				break;

			case NODE_CODEPAGE_LITERAL_TAG:
				printf("</%s>\n", GetStringTableString(node, mb_u_int32_to_long(((P_WBXML_MB_U_INT32)node->m_data))) );
				break;

			default:
				break;
		}
	}
}

void DumpNodes(P_WBXML_INFO buffer)
{
	P_WBXML_NODE curnode = buffer->m_tree;
	BOOL bAttrsFollow = FALSE;
	char* value = NULL;

	if (curnode)
	{
		while (curnode->m_next) curnode = curnode->m_next;

		while (curnode)
		{
			DumpNode(curnode, 0, &bAttrsFollow, FALSE, &value);
			curnode = curnode->m_prev;
		}
	}
}

int main(int argc, char** argv)
{
	WBXML_INFO buffer;

	if (argc < 2)
	{
		printf("Usage: decompile <file>\n");
		return 0;
	}

    Init(&buffer);
	ReadBinary(&buffer, argv[1]);
	Read_start(&buffer);
	DumpNodes(&buffer);
	Free(&buffer);

	return 0;
}
