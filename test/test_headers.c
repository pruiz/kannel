/*
 * test_headers.c - test wsp header packing and unpacking.
 *
 * Richard Braakman <dark@wapit.com>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gw/wsp_headers.h"
#include "gw/wsp-strings.h"


/* Test the http_header_combine function. */
static void test_header_combine(void)
{
    List *old;
    List *new;
    List *tmp;

    old = http_create_empty_headers();
    new = http_create_empty_headers();
    tmp = http_create_empty_headers();

    http_header_add(old, "Accept", "text/html");
    http_header_add(old, "Accept", "text/plain");
    http_header_add(old, "Accept-Language", "en");
    http_header_add(old, "Accept", "image/jpeg");

    http_header_combine(tmp, old);
    if (list_len(tmp) != 4) {
        error(0, "http_combine_header with an empty 'old' did not just append.");
    }

    http_header_combine(old, new);
    if (list_len(old) != 4) {
        error(0, "http_combine_header with an empty 'new' changed 'old'.");
    }

    http_header_add(new, "Accept", "text/html");
    http_header_add(new, "Accept", "text/plain");
    
    http_header_combine(old, new);
    if (list_len(old) != 3 ||
        octstr_compare(list_get(old, 0),
                       octstr_create_immutable("Accept-Language: en")) != 0 ||
        octstr_compare(list_get(old, 1),
                       octstr_create_immutable("Accept: text/html")) != 0 ||
        octstr_compare(list_get(old, 2),
                       octstr_create_immutable("Accept: text/plain")) != 0) {
        error(0, "http_header_combine failed.");
    }
}
 

static void split_headers(Octstr *headers, List **split, List **expected)
{
    long start;
    long pos;

    *split = list_create();
    *expected = list_create();
    start = 0;
    for (pos = 0; pos < octstr_len(headers); pos++) {
        if (octstr_get_char(headers, pos) == '\n') {
            int c;
            Octstr *line;

            if (pos == start) {
                /* Skip empty lines */
                start = pos + 1;
                continue;
            }

            line = octstr_copy(headers, start, pos - start);
            start = pos + 1;

            c = octstr_get_char(line, 0);
            octstr_delete(line, 0, 2);
            if (c == '|') {
                list_append(*split, line);
                list_append(*expected, octstr_duplicate(line));
            } else if (c == '<') {
                list_append(*split, line);
            } else if (c == '>') {
                list_append(*expected, line);
            } else if (c == '#') {
                /* comment */
                octstr_destroy(line);
            } else {
                warning(0, "Bad line in test headers file");
                octstr_destroy(line);
            }
        }
    }
}

int main(int argc, char **argv)
{
    Octstr *headers;
    List *expected;
    List *split;
    Octstr *packed;
    List *unpacked;
    long i;

    gwlib_init();
    wsp_strings_init();

    get_and_set_debugs(argc, argv, NULL);

    headers = octstr_read_file("test/header_test");
    split_headers(headers, &split, &expected);
    packed = wsp_headers_pack(split, 0);
    unpacked = wsp_headers_unpack(packed, 0);

    if (list_len(unpacked) != list_len(expected)) {
        error(0, "Expected %ld headers, generated %ld.\n",
              list_len(expected), list_len(unpacked));
    } else {
        for (i = 0; i < list_len(unpacked); i++) {
            Octstr *got, *exp;
            got = list_get(unpacked, i);
            exp = list_get(expected, i);
            if (octstr_compare(got, exp) != 0) {
                error(0, "Exp: %s", octstr_get_cstr(exp));
                error(0, "Got: %s", octstr_get_cstr(got));
            }
        }
    }

    test_header_combine();

    octstr_destroy(headers);
    list_destroy(split, octstr_destroy_item);
    list_destroy(expected, octstr_destroy_item);
    octstr_destroy(packed);
    list_destroy(unpacked, octstr_destroy_item);

    wsp_strings_shutdown();
    gwlib_shutdown();
    exit(0);
}
