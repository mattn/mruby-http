#include <mruby.h>
#include <mruby/proc.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <http_parser.h>
#include <stdio.h>
#include <string.h>

#define MAX_HEADER_NAME_LEN 1024
#define MAX_HEADERS         128

static struct RClass *_class_http;
static struct RClass *_class_http_parser;

/*********************************************************
 * main
 *********************************************************/

typedef struct {
  mrb_state *mrb;
  struct http_parser parser;
  struct http_parser_url handle;
  struct http_parser_settings settings;
  int was_header_value;
  mrb_value headers;
  mrb_value last_header_field;
  mrb_value last_header_value;
} http_parser_context;

static void
http_parser_context_free(mrb_state *mrb, void *p)
{
  free(p);
}

static const struct mrb_data_type http_parser_context_type = {
  "http_parser_context", http_parser_context_free,
};


static int
parser_settings_on_url(http_parser* parser, const char *at, size_t len)
{
  http_parser_context *context = (http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  if(http_parser_parse_url(at, len, FALSE, &(context->handle)) != 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  return 0;
}

static int
parser_settings_on_header_field(http_parser* parser, const char* at, size_t len)
{
  http_parser_context *context = (http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  if (context->was_header_value) {
    if (!mrb_nil_p(context->last_header_field)) {
      mrb_str_concat(mrb, context->last_header_field, context->last_header_value);
      context->last_header_value = mrb_nil_value();
    }
    context->last_header_field = mrb_str_new(mrb, at, len);
    context->was_header_value = FALSE;
  } else {
    mrb_str_concat(mrb, context->last_header_field, mrb_str_new(mrb, at, len));
  }
  return 0;
}

static int
parser_settings_on_header_value(http_parser* parser, const char* at, size_t len)
{
  http_parser_context *context = (http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  if(!context->was_header_value) {
    context->last_header_value = mrb_str_new(mrb, at, len);
    context->was_header_value = TRUE;
  } else {
    mrb_str_concat(mrb, context->last_header_value, mrb_str_new(mrb, at, len));
  }
  return 0;
}

static int
parser_settings_on_headers_complete(http_parser* parser)
{
  http_parser_context *context = (http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  if(!mrb_nil_p(context->last_header_field)) {
    mrb_hash_set(mrb, context->headers, context->last_header_field, context->last_header_value);
  }
  return 1;
}

static int
parser_settings_on_message_complete(http_parser* parser)
{
  return 0;
}

static mrb_value
mrb_http_parser_init(mrb_state *mrb, mrb_value self)
{
  http_parser_context* context = NULL;

  context = (http_parser_context*) mrb_malloc(mrb, sizeof(http_parser_context));
  memset(context, 0, sizeof(http_parser_context));
  context->mrb = mrb;
  mrb_iv_set(mrb, self, mrb_intern(mrb, "data"), mrb_obj_value(
    Data_Wrap_Struct(mrb, mrb->object_class,
    &http_parser_context_type, (void*) context)));
  return self;
}

static mrb_value
mrb_http_parser_parse(mrb_state *mrb, mrb_value self)
{
  mrb_value value_context;
  http_parser_context* context;
  struct RProc *b = NULL;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "data"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  mrb_get_args(mrb, "b", &b);

  context->parser.data = context;
  context->was_header_value = TRUE;
  context->headers = mrb_hash_new(mrb, 32);

  http_parser_init(&context->parser, HTTP_REQUEST);

  context->settings.on_url = parser_settings_on_url;
  context->settings.on_header_field = parser_settings_on_header_field;
  context->settings.on_header_value = parser_settings_on_header_value;
  context->settings.on_headers_complete = parser_settings_on_headers_complete;
  context->settings.on_message_complete = parser_settings_on_message_complete;

  return mrb_nil_value();
}

static mrb_value
mrb_http_parse_response(mrb_state *mrb, mrb_value self)
{
#if 0
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
#endif
  return mrb_nil_value();
}

/*********************************************************
 * register
 *********************************************************/

void
mrb_http_init(mrb_state* mrb) {
  _class_http = mrb_define_module(mrb, "HTTP");

  _class_http_parser = mrb_define_class_under(mrb, _class_http, "Parser", mrb->object_class);
  mrb_define_method(mrb, _class_http_parser, "initialize", mrb_http_parser_init, ARGS_OPT(1));
  mrb_define_class_method(mrb, _class_http_parser, "parse", mrb_http_parser_parse, ARGS_REQ(1));
}

/* vim:set et ts=2 sts=2 sw=2 tw=0: */
