#include <mruby.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include "picohttpparser.h"
#include <string.h>
#include <stdio.h>

#define MAX_HEADER_NAME_LEN 1024
#define MAX_HEADERS         128

static struct RClass *_class_http;

/*********************************************************
 * main
 *********************************************************/

static void
hash_str_set(mrb_state *mrb, mrb_value hash, const char* key, size_t key_len, const char* value, size_t value_len)
{
  if (key_len == -1) key_len = strlen(key);
  if (value_len == -1) value_len = strlen(value);
  mrb_hash_set(mrb, hash, mrb_str_new(mrb, key, key_len), mrb_str_new(mrb, value, value_len));
}

static mrb_value
hash_str_get(mrb_state *mrb, mrb_value hash, const char* key, size_t key_len)
{
  if (key_len == -1) key_len = strlen(key);
  return mrb_hash_get(mrb, hash, mrb_str_new(mrb, key, key_len));
}

static void
str_concat(mrb_state *mrb, mrb_value str, const char* add, size_t add_len)
{
  if (add_len == -1) add_len = strlen(add);
  mrb_str_concat(mrb, str, mrb_str_new(mrb, add, add_len));
}

static int
hash_has_keys(mrb_state *mrb, mrb_value hash, const char* key, size_t key_len)
{
  return !mrb_nil_p(hash_str_get(mrb, hash, key, key_len));
}

static char
tou(char ch)
{
  if ('a' <= ch && ch <= 'z')
    ch -= 'a' - 'A';
  return ch;
}

static char
tol(char const ch)
{
  return ('A' <= ch && ch <= 'Z')
    ? ch - ('A' - 'a')
    : ch;
}

static int
header_is(const struct phr_header* header, const char* name, size_t len)
{
  const char* x, * y;
  if (header->name_len != len)
    return 0;
  for (x = header->name, y = name; len != 0; --len, ++x, ++y)
    if (tou(*x) != *y)
      return 0;
  return 1;
}

static size_t
find_ch(const char* s, size_t len, char ch)
{
  size_t i;
  for (i = 0; i != len; ++i, ++s)
    if (*s == ch)
      break;
  return i;
}

static int
hex_decode(const char ch)
{
  int r;
  if ('0' <= ch && ch <= '9')
    r = ch - '0';
  else if ('A' <= ch && ch <= 'F')
    r = ch - 'A' + 0xa;
  else if ('a' <= ch && ch <= 'f')
    r = ch - 'a' + 0xa;
  else
    r = -1;
  return r;
}

static char*
url_decode(mrb_state *mrb, const char* s, size_t len)
{
  char* dbuf, * d;
  size_t i;
  
  for (i = 0; i < len; ++i)
    if (s[i] == '%')
      goto NEEDS_DECODE;
  return (char*)s;
  
NEEDS_DECODE:
  dbuf = (char*) mrb_malloc(mrb, len - 1);
  memcpy(dbuf, s, i);
  d = dbuf + i;
  while (i < len) {
    if (s[i] == '%') {
      int hi, lo;
      if ((hi = hex_decode(s[i + 1])) == -1 || (lo = hex_decode(s[i + 2])) == -1) {
        mrb_free(mrb, dbuf);
        return NULL;
      }
      *d++ = hi * 16 + lo;
      i += 3;
    } else
      *d++ = s[i++];
  }
  *d = '\0';
  return dbuf;
}

static int
store_url_decoded(mrb_state *mrb, mrb_value hash, const char* key, size_t key_len, const char* value, size_t value_len)
{
  char* decoded = url_decode(mrb, value, value_len);
  if (decoded == NULL) {
    return -1;
  }

  if (decoded == value) {
    hash_str_set(mrb, hash, key, key_len, value, value_len);
  } else {
    hash_str_set(mrb, hash, key, key_len, decoded, -1);
    mrb_free(mrb, decoded);
  }
  return 0;
}

static void
normalize_response_header_name(char* const dest, const char* const src, size_t const len)
{
  size_t i;
  for(i = 0; i < len; i++) {
    dest[i] = tol(src[i]);
  }
}

static void
concat_multiline_header(mrb_state *mrb, mrb_value v, const char *cont, size_t cont_len)
{
  str_concat(mrb, v, "\n", -1);
  str_concat(mrb, v, cont, cont_len);
}

static mrb_value
mrb_http_parse_http_request(mrb_state *mrb, mrb_value self)
{
  mrb_value last_value;
  const char* method = NULL;
  size_t method_len = 0;
  const char* path = NULL;
  size_t path_len = 0;
  int minor_version = 0;
  struct phr_header headers[MAX_HEADERS];
  size_t num_headers = MAX_HEADERS;
  char tmp[MAX_HEADER_NAME_LEN + sizeof("HTTP_") - 1] = {0};
  size_t question_at = 0;
  mrb_value arg, hash;
  size_t i;

  mrb_get_args(mrb, "o", &arg);
  if (mrb_nil_p(arg) || mrb_type(arg) != MRB_TT_STRING) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  int ret = phr_parse_request(
    RSTRING_PTR(arg),
    RSTRING_LEN(arg),
    &method,
    &method_len,
    &path,
    &path_len,
    &minor_version,
    headers,
    &num_headers,
    0
  );
  if (ret < 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  hash = mrb_hash_new(mrb, 32);

  hash_str_set(mrb, hash, "REQUEST_METHOD", -1, method, method_len);
  hash_str_set(mrb, hash, "REQUEST_URI", -1, path, path_len);
  hash_str_set(mrb, hash, "SCRIPT_NAME", -1, "", 0);

  path_len = find_ch(path, path_len, '#'); /* strip off all text after # after storing request_uri */
  question_at = find_ch(path, path_len, '?');

  if (store_url_decoded(mrb, hash, "PATH_INFO", sizeof("PATH_INFO") - 1, path, question_at) != 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (question_at != path_len) {
    ++question_at;
  }

  hash_str_set(mrb, hash, "QUERY_STRING", -1, path+question_at, path_len - question_at);
  sprintf(tmp, "HTTP/1.%d", minor_version);
  hash_str_set(mrb, hash, "SERVER_PROTOCOL", -1, tmp, -1);

  for (i = 0; i < num_headers; ++i) {
    if (headers[i].name != NULL) {
      const char* name;
      size_t name_len;
      if (header_is(headers + i, "CONTENT-TYPE", sizeof("CONTENT-TYPE") - 1)) {
        name = "CONTENT_TYPE";
        name_len = sizeof("CONTENT_TYPE") - 1;
      } else if (header_is(headers + i, "CONTENT-LENGTH",
          sizeof("CONTENT-LENGTH") - 1)) {
        name = "CONTENT_LENGTH";
        name_len = sizeof("CONTENT_LENGTH") - 1;
      } else {
        const char* s;
        char* d;
        size_t n;
        if (sizeof(tmp) - 5 < headers[i].name_len) {
           return mrb_nil_value();
        }
        strcpy(tmp, "HTTP_");
        for (s = headers[i].name, n = headers[i].name_len, d = tmp + 5;
          n != 0;
          s++, --n, d++) {
          *d = *s == '-' ? '_' : tou(*s);
        }
        name = tmp;
        name_len = headers[i].name_len + 5;
      }

      if (hash_has_keys(mrb, hash, name, name_len)) {
        mrb_value v = hash_str_get(mrb, hash, name, name_len);
        str_concat(mrb, v, ", ", -1);
        str_concat(mrb, v, headers[i].value, headers[i].value_len);
        last_value = v;
      } else {
        hash_str_set(mrb, hash, name, name_len, headers[i].value, headers[i].value_len);
        last_value = mrb_str_new(mrb, headers[i].value, headers[i].value_len);
      }
    } else {
      /* continuing lines of a mulitiline header */
      str_concat(mrb, last_value, headers[i].value, headers[i].value_len);
    }
  }

  return hash;
}

static mrb_value
mrb_http_parse_http_response(mrb_state *mrb, mrb_value self)
{
  int minor_version, status;
  const char* msg;
  size_t msg_len;
  struct phr_header headers[MAX_HEADERS];
  size_t num_headers = MAX_HEADERS;
  size_t last_len = 0;
  mrb_value arg, hash;

  mrb_get_args(mrb, "o", &arg);
  if (mrb_nil_p(arg) || mrb_type(arg) != MRB_TT_STRING) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  int ret = phr_parse_response(
    RSTRING_PTR(arg),
    RSTRING_CAPA(arg),
    &minor_version,
    &status,
    &msg,
    &msg_len,
    headers,
    &num_headers,
    last_len);

  char name[MAX_HEADER_NAME_LEN]; /* temp buffer for normalized names */
  mrb_value last_element_value_sv = mrb_nil_value();
  size_t i;
  hash = mrb_hash_new(mrb, 20);
  for (i = 0; i < num_headers; i++) {
    struct phr_header const h = headers[i];
    if (h.name != NULL) {
      if(h.name_len > sizeof(name)) {
        /* skip if name_len is too long */
        continue;
      }
      normalize_response_header_name(name, h.name, h.name_len);
      hash_str_set(mrb, hash, h.name, h.name_len, h.value, h.value_len);
      last_element_value_sv = mrb_str_new(mrb, h.value, h.value_len);
    } else {
      if (!mrb_nil_p(last_element_value_sv)) {
        concat_multiline_header(mrb, last_element_value_sv, h.value, h.value_len);
      }
    }
  }

  mrb_value arr = mrb_ary_new(mrb);
  mrb_ary_push(mrb, arr, mrb_fixnum_value(ret));
  mrb_ary_push(mrb, arr, mrb_fixnum_value(minor_version));
  mrb_ary_push(mrb, arr, mrb_fixnum_value(status));
  mrb_ary_push(mrb, arr, mrb_str_new(mrb, msg, msg_len));
  mrb_ary_push(mrb, arr, hash);
  return arr;
}

/*********************************************************
 * register
 *********************************************************/

void
mrb_http_init(mrb_state* mrb) {
  _class_http = mrb_define_module(mrb, "HTTP");
  mrb_define_class_method(mrb, _class_http, "parse_http_request", mrb_http_parse_http_request, ARGS_REQ(1));
  mrb_define_class_method(mrb, _class_http, "parse_http_response", mrb_http_parse_http_response, ARGS_REQ(1));
}

/* vim:set et ts=2 sts=2 sw=2 tw=0: */
