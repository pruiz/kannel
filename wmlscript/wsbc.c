/*
 *
 * wsbc.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999, 2000 Markku Rossi, etc.
 *		 All rights reserved.
 *
 * Byte-code handling functions.
 *
 */

#include <wsint.h>
#include <wsbc.h>

/*
 * Prototypes for static functions.
 */

/* Add a new pragma of type `type' to the byte-code `bc'.  The
   function returns a pointer to an internal pragma structure that
   must not be freed by the caller.  It is freed when the byte-code
   `bc' is freed.  The function returns NULL if the pragma structure
   could not be allocated. */
static WsBcPragma *add_pragma(WsBc *bc, WsBcPragmaType type);


/*
 * Global functions.
 */

WsBc *
ws_bc_alloc(WsBcStringEncoding string_encoding)
{
  WsBc *bc = ws_calloc(1, sizeof(WsBc));

  if (bc == NULL)
    return NULL;

  bc->string_encoding = string_encoding;

  return bc;
}


void
ws_bc_free(WsBc *bc)
{
  WsUInt16 i;
  WsUInt8 j;

  if (bc == NULL)
    return;

  /* Free constants. */
  for (i = 0; i < bc->num_constants; i++)
    {
      WsBcConstant *c = &bc->constants[i];

      if (c->type == WS_BC_CONST_TYPE_UTF8_STRING)
	ws_free(c->u.v_string.data);
    }
  ws_free(bc->constants);

  /* Free pragmas. */
  ws_free(bc->pragmas);

  /* Free function names. */
  for (j = 0; j < bc->num_function_names; j++)
    ws_free(bc->function_names[j].name);
  ws_free(bc->function_names);

  /* Free functions. */
  for (j = 0; j < bc->num_functions; j++)
    ws_free(bc->functions[j].code);
  ws_free(bc->functions);

  /* Free the byte-code structure. */
  ws_free(bc);
}


WsBool
ws_bc_encode(WsBc *bc, unsigned char **data_return, size_t *data_len_return)
{
  WsBuffer buffer;
  WsUInt32 ui;
  unsigned char data[64];
  unsigned char *p, *mb;
  size_t len;

  ws_buffer_init(&buffer);

  /* Append space for the header.  We do not know yet the size of the
     resulting byte-code. */
  if (!ws_buffer_append_space(&buffer, NULL, WS_BC_MAX_HEADER_LEN))
    goto error;


  /* Constants. */

  if (!ws_encode_buffer(&buffer,
			WS_ENC_MB_UINT16, bc->num_constants,
			WS_ENC_MB_UINT16, (WsUInt16) bc->string_encoding,
			WS_ENC_END))
    goto error;

  for (ui = 0 ; ui < bc->num_constants; ui++)
    {
      switch (bc->constants[ui].type)
	{
	case WS_BC_CONST_TYPE_INT:
	  if (WS_INT8_MIN <= bc->constants[ui].u.v_int
	      && bc->constants[ui].u.v_int <= WS_INT8_MAX)
	    {
	      if (!ws_encode_buffer(&buffer,
				    WS_ENC_UINT8, (WsUInt8) WS_BC_CONST_INT8,
				    WS_ENC_INT8,
				    (WsInt8) bc->constants[ui].u.v_int,
				    WS_ENC_END))
		goto error;
	    }
	  else if (WS_INT16_MIN <= bc->constants[ui].u.v_int
		   && bc->constants[ui].u.v_int <= WS_INT16_MAX)
	    {
	      if (!ws_encode_buffer(&buffer,
				    WS_ENC_UINT8, (WsUInt8) WS_BC_CONST_INT16,
				    WS_ENC_INT16,
				    (WsInt16) bc->constants[ui].u.v_int,
				    WS_ENC_END))
		goto error;
	    }
	  else
	    {
	      if (!ws_encode_buffer(&buffer,
				    WS_ENC_UINT8, (WsUInt8) WS_BC_CONST_INT32,
				    WS_ENC_INT32, bc->constants[ui].u.v_int,
				    WS_ENC_END))
		goto error;
	    }
	  break;

	case WS_BC_CONST_TYPE_FLOAT32:
	  if (!ws_encode_buffer(&buffer,
				WS_ENC_UINT8, (WsUInt8) WS_BC_CONST_FLOAT32,

				WS_ENC_FLOAT32,
				(double) bc->constants[ui].u.v_float32,

				WS_ENC_END))
	    goto error;
	  break;

	case WS_BC_CONST_TYPE_UTF8_STRING:
	  /* Encode the strings as requested. */
	  switch (bc->string_encoding)
	    {
	    case WS_BC_STRING_ENC_ISO_8859_1:
	      {
		WsUtf8String *string = ws_utf8_alloc();
		char *latin1;
		size_t latin1_len;
		WsBool success;

		if (string == NULL)
		  goto error;

		/* Create an UTF-8 string. */
		if (!ws_utf8_set_data(string,
				      bc->constants[ui].u.v_string.data,
				      bc->constants[ui].u.v_string.len))
		  {
		    ws_utf8_free(string);
		    goto error;
		  }

		/* Convert it to latin1. */
		latin1 = ws_utf8_to_latin1(string, '?', &latin1_len);

		/* We'r done with the UTF-8 string. */
		ws_utf8_free(string);

		if (latin1 == NULL)
		  goto error;

		/* Encode it. */
		success = ws_encode_buffer(
				&buffer,
				WS_ENC_UINT8,
				(WsUInt8) WS_BC_CONST_EXT_ENC_STRING,

				WS_ENC_MB_UINT32, (WsUInt32) latin1_len,
				WS_ENC_DATA, latin1, latin1_len,

				WS_ENC_END);
		ws_utf8_free_data(latin1);

		if (!success)
		  goto error;
	      }
	      break;

	    case WS_BC_STRING_ENC_UTF8:
	      if (!ws_encode_buffer(
				&buffer,
				WS_ENC_UINT8,
				(WsUInt8) WS_BC_CONST_UTF8_STRING,

				WS_ENC_MB_UINT32,
				(WsUInt32) bc->constants[ui].u.v_string.len,

				WS_ENC_DATA,
				bc->constants[ui].u.v_string.data,
				bc->constants[ui].u.v_string.len,

				WS_ENC_END))
		goto error;
	      break;
	    }
	  break;

	case WS_BC_CONST_TYPE_EMPTY_STRING:
	  if (!ws_encode_buffer(&buffer,
				WS_ENC_UINT8,
				(WsUInt8) WS_BC_CONST_EMPTY_STRING,

				WS_ENC_END))
	    goto error;
	  break;
	}
    }


  /* Pragmas. */

  if (!ws_encode_buffer(&buffer,
			WS_ENC_MB_UINT16, bc->num_pragmas,
			WS_ENC_END))
    goto error;

  for (ui = 0; ui < bc->num_pragmas; ui++)
    {
      switch (bc->pragmas[ui].type)
	{
	case WS_BC_PRAGMA_TYPE_ACCESS_DOMAIN:
	  if (!ws_encode_buffer(&buffer,
				WS_ENC_UINT8,
				(WsUInt8) WS_BC_PRAGMA_ACCESS_DOMAIN,

				WS_ENC_MB_UINT16, bc->pragmas[ui].index_1,
				WS_ENC_END))
	    goto error;
	  break;

	case WS_BC_PRAGMA_TYPE_ACCESS_PATH:
	  if (!ws_encode_buffer(&buffer,
				WS_ENC_UINT8,
				(WsUInt8) WS_BC_PRAGMA_ACCESS_PATH,

				WS_ENC_MB_UINT16, bc->pragmas[ui].index_1,
				WS_ENC_END))
	    goto error;
	  break;

	case WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY:
	  if (!ws_encode_buffer(&buffer,
				WS_ENC_UINT8,
				(WsUInt8) WS_BC_PRAGMA_USER_AGENT_PROPERTY,

				WS_ENC_MB_UINT16, bc->pragmas[ui].index_1,
				WS_ENC_MB_UINT16, bc->pragmas[ui].index_2,
				WS_ENC_END))
	    goto error;
	  break;

	case WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY_AND_SCHEME:
	  if (!ws_encode_buffer(
			&buffer,
			WS_ENC_UINT8,
			(WsUInt8) WS_BC_PRAGMA_USER_AGENT_PROPERTY_AND_SCHEME,

			WS_ENC_MB_UINT16, bc->pragmas[ui].index_1,
			WS_ENC_MB_UINT16, bc->pragmas[ui].index_2,
			WS_ENC_MB_UINT16, bc->pragmas[ui].index_3,
			WS_ENC_END))
	    goto error;
	  break;
	}
    }


  /* Function pool. */

  if (!ws_encode_buffer(&buffer,
			WS_ENC_UINT8, bc->num_functions,
			WS_ENC_END))
    goto error;

  /* Function names. */

  if (!ws_encode_buffer(&buffer,
			WS_ENC_UINT8, bc->num_function_names,
			WS_ENC_END))
    goto error;

  for (ui = 0; ui < bc->num_function_names; ui++)
    {
      size_t name_len = strlen(bc->function_names[ui].name);

      if (!ws_encode_buffer(&buffer,
			    WS_ENC_UINT8, bc->function_names[ui].index,
			    WS_ENC_UINT8, (WsUInt8) name_len,
			    WS_ENC_DATA, bc->function_names[ui].name, name_len,
			    WS_ENC_END))
	goto error;
    }

  /* Functions. */

  for (ui = 0; ui < bc->num_functions; ui++)
    if (!ws_encode_buffer(&buffer,
			  WS_ENC_UINT8, bc->functions[ui].num_arguments,
			  WS_ENC_UINT8, bc->functions[ui].num_locals,
			  WS_ENC_MB_UINT32, bc->functions[ui].code_size,

			  WS_ENC_DATA, bc->functions[ui].code,
			  (size_t) bc->functions[ui].code_size,

			  WS_ENC_END))
      goto error;


  /* Fix the byte-code header. */

  p = ws_buffer_ptr(&buffer);

  /* Encode the size of the byte-code excluding the byte-code header. */
  mb = ws_encode_mb_uint32(ws_buffer_len(&buffer) - WS_BC_MAX_HEADER_LEN,
			   data, &len);
  memcpy(p + WS_BC_MAX_HEADER_LEN - len, mb, len);

  /* Set the byte-code file version information. */
  WS_PUT_UINT8(p + WS_BC_MAX_HEADER_LEN - len - 1, WS_BC_VERSION);

  /* Calculate the beginning of the bc-array and its size. */
  *data_return = p + WS_BC_MAX_HEADER_LEN - len - 1;
  *data_len_return = ws_buffer_len(&buffer) - WS_BC_MAX_HEADER_LEN + len + 1;

  /* All done. */
  return WS_TRUE;


  /*
   * Error handling.
   */

 error:

  ws_buffer_uninit(&buffer);
  *data_return = NULL;
  *data_len_return = 0;

  return WS_FALSE;
}


void
ws_bc_data_free(unsigned char *data)
{
  size_t len = WS_MB_UINT32_MAX_ENCODED_LEN;

  if (data == NULL)
    return;

  /* Decode the mb-encoded length so we know how much space it uses. */
  (void) ws_decode_mb_uint32(data + 1, &len);

  /* Now we can compute the beginning of the array `data'. */
  ws_free(data - (WS_MB_UINT32_MAX_ENCODED_LEN - len));
}


WsBool
ws_bc_add_const_int(WsBc *bc, WsUInt16 *index_return, WsInt32 value)
{
  WsUInt16 i;
  WsBcConstant *nc;

  /* Do we already have a suitable integer constant? */
  for (i = 0; i < bc->num_constants; i++)
    if (bc->constants[i].type == WS_BC_CONST_TYPE_INT
	&& bc->constants[i].u.v_int == value)
      {
	/* Add a reference to this constant. */
	bc->constants[i].opt.refcount++;

	*index_return = i;
	return WS_TRUE;
      }

  /* Must add a new constant. */

  nc = ws_realloc(bc->constants,
		  (bc->num_constants + 1) * sizeof(WsBcConstant));
  if (nc == NULL)
    return WS_FALSE;

  bc->constants = nc;
  bc->constants[bc->num_constants].type = WS_BC_CONST_TYPE_INT;
  bc->constants[bc->num_constants].opt.refcount = 1;
  bc->constants[bc->num_constants].opt.original_index = bc->num_constants;
  bc->constants[bc->num_constants].u.v_int = value;

  *index_return = bc->num_constants++;

  return WS_TRUE;
}


WsBool
ws_bc_add_const_float32(WsBc *bc, WsUInt16 *index_return, WsFloat32 value)
{
  WsUInt16 i;
  WsBcConstant *nc;

  /* Do we already have a suitable integer constant? */
  for (i = 0; i < bc->num_constants; i++)
    if (bc->constants[i].type == WS_BC_CONST_TYPE_FLOAT32
	&& bc->constants[i].u.v_float32 == value)
      {
	/* Add a reference to this constant. */
	bc->constants[i].opt.refcount++;

	*index_return = i;
	return WS_TRUE;
      }

  /* Must add a new constant. */

  nc = ws_realloc(bc->constants,
		  (bc->num_constants + 1) * sizeof(WsBcConstant));
  if (nc == NULL)
    return WS_FALSE;

  bc->constants = nc;
  bc->constants[bc->num_constants].type = WS_BC_CONST_TYPE_FLOAT32;
  bc->constants[bc->num_constants].opt.refcount = 1;
  bc->constants[bc->num_constants].opt.original_index = bc->num_constants;
  bc->constants[bc->num_constants].u.v_float32 = value;

  *index_return = bc->num_constants++;

  return WS_TRUE;
}


WsBool
ws_bc_add_const_utf8_string(WsBc *bc, WsUInt16 *index_return,
			    const unsigned char *data, size_t len)
{
  WsUInt16 i;
  WsBcConstant *nc;

  /* Do we already have a suitable integer constant? */
  for (i = 0; i < bc->num_constants; i++)
    if (bc->constants[i].type == WS_BC_CONST_TYPE_UTF8_STRING
	&& bc->constants[i].u.v_string.len == len
	&& memcmp(bc->constants[i].u.v_string.data,
		  data, len) == 0)
      {
	/* Add a reference to this constant. */
	bc->constants[i].opt.refcount++;

	*index_return = i;
	return WS_TRUE;
      }

  /* Must add a new constant. */

  nc = ws_realloc(bc->constants,
		  (bc->num_constants + 1) * sizeof(WsBcConstant));
  if (nc == NULL)
    return WS_FALSE;

  bc->constants = nc;
  bc->constants[bc->num_constants].type = WS_BC_CONST_TYPE_UTF8_STRING;
  bc->constants[bc->num_constants].opt.refcount = 1;
  bc->constants[bc->num_constants].opt.original_index = bc->num_constants;
  bc->constants[bc->num_constants].u.v_string.len = len;
  bc->constants[bc->num_constants].u.v_string.data
    = ws_memdup(data, len);
  if (bc->constants[bc->num_constants].u.v_string.data == NULL)
    return WS_FALSE;

  *index_return = bc->num_constants++;

  return WS_TRUE;
}



WsBool
ws_bc_add_const_empty_string(WsBc *bc, WsUInt16 *index_return)
{
  WsUInt16 i;
  WsBcConstant *nc;

  /* Do we already have a suitable integer constant? */
  for (i = 0; i < bc->num_constants; i++)
    if (bc->constants[i].type == WS_BC_CONST_TYPE_EMPTY_STRING)
      {
	/* Add a reference to this constant. */
	bc->constants[i].opt.refcount++;

	*index_return = i;
	return WS_TRUE;
      }

  /* Must add a new constant. */

  nc = ws_realloc(bc->constants,
		  (bc->num_constants + 1) * sizeof(WsBcConstant));
  if (nc == NULL)
    return WS_FALSE;

  bc->constants = nc;
  bc->constants[bc->num_constants].type = WS_BC_CONST_TYPE_EMPTY_STRING;
  bc->constants[bc->num_constants].opt.refcount = 1;
  bc->constants[bc->num_constants].opt.original_index = bc->num_constants;

  *index_return = bc->num_constants++;

  return WS_TRUE;
}


WsBool
ws_bc_add_pragma_access_domain(WsBc *bc, const unsigned char *domain,
			       size_t domain_len)
{
  WsBcPragma *p = add_pragma(bc, WS_BC_PRAGMA_TYPE_ACCESS_DOMAIN);

  if (p == NULL)
    return WS_FALSE;

  if (!ws_bc_add_const_utf8_string(bc, &p->index_1, domain, domain_len))
    return WS_FALSE;

  return WS_TRUE;
}


WsBool
ws_bc_add_pragma_access_path(WsBc *bc, const unsigned char *path,
			     size_t path_len)
{
  WsBcPragma *p = add_pragma(bc, WS_BC_PRAGMA_TYPE_ACCESS_PATH);

  if (p == NULL)
    return WS_FALSE;

  if (!ws_bc_add_const_utf8_string(bc, &p->index_1, path, path_len))
    return WS_FALSE;

  return WS_TRUE;
}


WsBool
ws_bc_add_pragma_user_agent_property(WsBc *bc,
				     const unsigned char *name,
				     size_t name_len,
				     const unsigned char *property,
				     size_t property_len)
{
  WsBcPragma *p = add_pragma(bc, WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY);

  if (p == NULL)
    return WS_FALSE;

  if (!ws_bc_add_const_utf8_string(bc, &p->index_1, name, name_len)
      || !ws_bc_add_const_utf8_string(bc, &p->index_2, property, property_len))
    return WS_FALSE;

  return WS_TRUE;
}


WsBool
ws_bc_add_pragma_user_agent_property_and_scheme(
					WsBc *bc,
					const unsigned char *name,
					size_t name_len,
					const unsigned char *property,
					size_t property_len,
					const unsigned char *scheme,
					size_t scheme_len)
{
  WsBcPragma *p;

  p = add_pragma(bc, WS_BC_PRAGMA_TYPE_USER_AGENT_PROPERTY_AND_SCHEME);

  if (p == NULL)
    return WS_FALSE;

  if (!ws_bc_add_const_utf8_string(bc, &p->index_1, name, name_len)
      || !ws_bc_add_const_utf8_string(bc, &p->index_2, property, property_len)
      || !ws_bc_add_const_utf8_string(bc, &p->index_3, scheme, scheme_len))
    return WS_FALSE;

  return WS_TRUE;
}


WsBool
ws_bc_add_function(WsBc *bc, WsUInt8 *index_return, char *name,
		   WsUInt8 num_arguments, WsUInt8 num_locals,
		   WsUInt32 code_size, unsigned char *code)
{
  WsBcFunction *nf;

  /* First, add the function to the function pool. */

  nf = ws_realloc(bc->functions,
		  (bc->num_functions + 1) * sizeof(WsBcFunction));
  if (nf == NULL)
    return WS_FALSE;

  bc->functions = nf;
  bc->functions[bc->num_functions].num_arguments = num_arguments;
  bc->functions[bc->num_functions].num_locals = num_locals;
  bc->functions[bc->num_functions].code_size = code_size;
  bc->functions[bc->num_functions].code = ws_memdup(code, code_size);

  if (bc->functions[bc->num_functions].code == NULL)
    return WS_FALSE;

  /* Save the index of the function. */
  *index_return = bc->num_functions++;

  /* For external functions (which have name), add a name entry to the
     function name pool. */
  if (name)
    {
      WsBcFunctionName *nfn;

      nfn = ws_realloc(bc->function_names,
		       ((bc->num_function_names + 1)
			* sizeof(WsBcFunctionName)));
      if (nfn == NULL)
	return WS_FALSE;

      bc->function_names = nfn;
      bc->function_names[bc->num_function_names].index = *index_return;
      bc->function_names[bc->num_function_names].name = ws_strdup(name);

      if (bc->function_names[bc->num_function_names].name == NULL)
	return WS_FALSE;

      bc->num_function_names++;
    }

  /* All done. */
  return WS_TRUE;
}


/*
 * Static functions.
 */

static WsBcPragma *
add_pragma(WsBc *bc, WsBcPragmaType type)
{
  WsBcPragma *np;

  /* Add a new pragma slot. */
  np = ws_realloc(bc->pragmas, (bc->num_pragmas + 1) * sizeof(WsBcPragma));
  if (np == NULL)
    return NULL;

  bc->pragmas = np;
  bc->pragmas[bc->num_pragmas].type = type;

  return &bc->pragmas[bc->num_pragmas++];
}
