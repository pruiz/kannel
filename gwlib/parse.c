/* 
 * parse.c - implement parse.h interface
 *
 * Richard Braakman <dark@wapit.com>
 */

#include "gwlib/gwlib.h"

struct context {
	Octstr *data;
	long pos;
	long limit;
	List *limit_stack;
	int error;
};

Context *parse_context_create(Octstr *str) {
	Context *result;

	result = gw_malloc(sizeof(*result));
	result->data = str;
	result->pos = 0;
	result->limit = octstr_len(str);
	result->limit_stack = NULL;
	result->error = 0;

	return result;
}

void parse_context_destroy(Context *context) {
	gw_assert(context != NULL);

	if (context->limit_stack) {
		while (list_len(context->limit_stack) > 0)
			gw_free(list_extract_first(context->limit_stack));
		list_destroy(context->limit_stack);
	}

	gw_free(context);
}

int parse_error(Context *context) {
	gw_assert(context != NULL);

	return context->error;
}

void parse_clear_error(Context *context) {
	gw_assert(context != NULL);

	context->error = 0;
}

void parse_set_error(Context *context) {
	gw_assert(context != NULL);
	
	context->error = 1;
}

int parse_limit(Context *context, long length) {
	long *elem;

	gw_assert(context != NULL);

	if (context->pos + length > context->limit) {
		context->error = 1;
		return -1;
	}

	if (context->limit_stack == NULL)
		context->limit_stack = list_create();

	elem = gw_malloc(sizeof(*elem));
	*elem = context->limit;
	list_insert(context->limit_stack, 0, elem);
	context->limit = context->pos + length;
	return 0;
}

int parse_pop_limit(Context *context) {
	long *elem;

	gw_assert(context != NULL);

	if (context->limit_stack == NULL
	    || list_len(context->limit_stack) == 0) {
		context->error = 1;
		return -1;
	}

	elem = list_extract_first(context->limit_stack);
	context->limit = *elem;
	gw_free(elem);
	return 0;
}

long parse_octets_left(Context *context) {
	gw_assert(context != NULL);

	return context->limit - context->pos;
}

int parse_skip(Context *context, long count) {
	gw_assert(context != NULL);

	if (context->pos + count > context->limit) {
		context->pos = context->limit;
		context->error = 1;
		return -1;
	}

	context->pos += count;
	return 0;
}

void parse_skip_to_limit(Context *context) {
	gw_assert(context != NULL);

	context->pos = context->limit;
}

int parse_skip_to(Context *context, long pos) {
	gw_assert(context != NULL);

	if (pos < 0) {
		context->error = 1;
		return -1;
	}

	if (pos > context->limit) {
		context->pos = context->limit;
		context->error = 1;
		return -1;
	}

	context->pos = pos;
	return 0;
}

int parse_peek_char(Context *context) {
	gw_assert(context != NULL);
	
	if (context->pos == context->limit) {
		context->error = 1;
		return -1;
	}

	return octstr_get_char(context->data, context->pos++);
}

int parse_get_char(Context *context) {
	gw_assert(context != NULL);

	if (context->pos == context->limit) {
		context->error = 1;
		return -1;
	}

	return octstr_get_char(context->data, context->pos++);
}

Octstr *parse_get_octets(Context *context, long length) {
	Octstr *result;

	gw_assert(context != NULL);

	if (context->pos + length > context->limit) {
		context->error = 1;
		return NULL;
	}

	result = octstr_copy(context->data, context->pos, length);
	context->pos += length;
	return result;
}

unsigned long parse_get_uintvar(Context *context) {
	long pos;
	unsigned long value;

	gw_assert(context != NULL);

	pos = octstr_extract_uintvar(context->data, &value, context->pos);
	if (pos < 0 || pos > context->limit) {
		context->error = 1;
		return 0;
	}

	return value;
}

Octstr *parse_get_nul_string(Context *context) {
	Octstr *result;
	long pos;

	gw_assert(context != NULL);

	pos = octstr_search_char(context->data, 0, context->pos);
	if (pos < 0 || pos >= context->limit) {
		context->error = 1;
		return NULL;
	}

	result = octstr_copy(context->data, context->pos, pos - context->pos);
	context->pos = pos + 1;
	
	return result;
}
