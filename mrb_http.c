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
static struct RClass *_class_http_request;
static struct RClass *_class_http_response;

/*********************************************************
 * parser
 *********************************************************/

typedef struct {
  mrb_state *mrb;
  struct http_parser parser;
  struct http_parser_url handle;
  struct http_parser_settings settings;
  int was_header_value;
  mrb_value instance; /* callback */
} mrb_http_parser_context;

static void
http_parser_context_free(mrb_state *mrb, void *p)
{
  if (p) mrb_free(mrb, p);
}

static const struct mrb_data_type http_parser_context_type = {
  "mrb_http_parser_context", http_parser_context_free,
};

static void
http_request_free(mrb_state *mrb, void *p)
{
  if (p) mrb_free(mrb, p);
}

static const struct mrb_data_type http_requset_type = {
  "mrb_http_request", http_request_free,
};

static int
parser_settings_on_url(http_parser* parser, const char *at, size_t len)
{
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  if(http_parser_parse_url(at, len, FALSE, &context->handle) != 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  mrb_iv_set(mrb, context->instance, mrb_intern(mrb, "buf"), mrb_str_new(mrb, at, len));
  return 0;
}

#define CTXV_GET(ctx, name) \
  mrb_iv_get(ctx->mrb, ctx->instance, mrb_intern(ctx->mrb, name))

#define CTXV_SET(ctx, name, value) \
  mrb_iv_set(ctx->mrb, ctx->instance, mrb_intern(ctx->mrb, name), value)

static int
parser_settings_on_header_field(http_parser* parser, const char* at, size_t len)
{
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  if (context->was_header_value) {
    if (!mrb_nil_p(CTXV_GET(context, "last_header_field"))) {
      mrb_str_concat(mrb, CTXV_GET(context, "last_header_field"), CTXV_GET(context, "last_header_value"));
      CTXV_SET(context, "last_header_value", mrb_nil_value());
    }
    CTXV_SET(context, "last_header_field", mrb_str_new(mrb, at, len));
    context->was_header_value = FALSE;
  } else {
    mrb_str_concat(mrb, CTXV_GET(context, "last_header_field"), mrb_str_new(mrb, at, len));
  }
  return 0;
}

static int
parser_settings_on_header_value(http_parser* parser, const char* at, size_t len)
{
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  if(!context->was_header_value) {
    CTXV_SET(context, "last_header_value", mrb_str_new(mrb, at, len));
    context->was_header_value = TRUE;
  } else {
    mrb_str_concat(mrb, CTXV_GET(context, "last_header_value"), mrb_str_new(mrb, at, len));
  }
  return 0;
}

static int
parser_settings_on_headers_complete(http_parser* parser)
{
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  if(!mrb_nil_p(CTXV_GET(context, "last_header_field"))) {
    mrb_hash_set(mrb, CTXV_GET(context, "headers"),
        CTXV_GET(context, "last_header_field"),
        CTXV_GET(context, "last_header_value"));
  }
  return 1;
}

static int
parser_settings_on_message_complete(http_parser* parser)
{
  mrb_value proc;
  mrb_value c;
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_http_parser_context *new_context;
  mrb_state* mrb = context->mrb;

  proc = mrb_iv_get(context->mrb, context->instance, mrb_intern(context->mrb, "complete_cb"));

  c = mrb_class_new_instance(mrb, 0, NULL, _class_http_request);
  new_context = (mrb_http_parser_context*) mrb_malloc(mrb, sizeof(mrb_http_parser_context));
  memset(new_context, 0, sizeof(mrb_http_parser_context));
  new_context->mrb = mrb;
  new_context->parser = context->parser;
  new_context->handle = context->handle;
  new_context->settings = context->settings;
  new_context->was_header_value = context->was_header_value;
  new_context->instance = c;
  mrb_iv_set(mrb, c, mrb_intern(mrb, "parser_context"), mrb_obj_value(
    Data_Wrap_Struct(mrb, mrb->object_class,
    &http_parser_context_type, (void*) new_context)));
  mrb_yield(context->mrb, proc, c);

  return 0;
}

static mrb_value
mrb_http_parser_init(mrb_state *mrb, mrb_value self)
{
  mrb_http_parser_context* context = NULL;

  context = (mrb_http_parser_context*) mrb_malloc(mrb, sizeof(mrb_http_parser_context));
  memset(context, 0, sizeof(mrb_http_parser_context));
  context->mrb = mrb;
  context->instance = self;
  mrb_iv_set(mrb, self, mrb_intern(mrb, "parser_context"), mrb_obj_value(
    Data_Wrap_Struct(mrb, mrb->object_class,
    &http_parser_context_type, (void*) context)));
  return self;
}

static mrb_value
mrb_http_parser_parse(mrb_state *mrb, mrb_value self)
{
  mrb_value arg_data;
  mrb_value value_context;
  mrb_http_parser_context* context;
  struct RProc *b = NULL;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "parser_context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  mrb_get_args(mrb, "bo", &b, &arg_data);
  if (mrb_nil_p(arg_data)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  mrb_iv_set(mrb, self, mrb_intern(mrb, "complete_cb"), b ? mrb_obj_value(b) : mrb_nil_value());

  context->parser.data = context;
  context->was_header_value = TRUE;
  CTXV_SET(context, "headers", mrb_hash_new(mrb, 32));

  http_parser_init(&context->parser, HTTP_REQUEST);

  context->settings.on_url = parser_settings_on_url;
  context->settings.on_header_field = parser_settings_on_header_field;
  context->settings.on_header_value = parser_settings_on_header_value;
  context->settings.on_headers_complete = parser_settings_on_headers_complete;
  if (b) {
    context->settings.on_message_complete = parser_settings_on_message_complete;
  }

  if (RSTRING_LEN(arg_data) > 0) {
    http_parser_execute(&context->parser, &context->settings, (char*) RSTRING_PTR(arg_data), RSTRING_CAPA(arg_data));
  }

  return mrb_nil_value();
}

static mrb_value
mrb_http_parser_execute(mrb_state *mrb, mrb_value self)
{
  mrb_value arg_data;
  mrb_value value_context;
  mrb_http_parser_context* context;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "parser_context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  mrb_get_args(mrb, "o", &arg_data);
  if (mrb_nil_p(arg_data)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  http_parser_execute(&context->parser, &context->settings, (char*) RSTRING_PTR(arg_data), RSTRING_CAPA(arg_data));

  return mrb_nil_value();
}

/*********************************************************
 * request
 *********************************************************/

static mrb_value
mrb_http_request_schema(mrb_state *mrb, mrb_value self)
{
  mrb_value value_context;
  mrb_http_parser_context* context;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "parser_context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (context->handle.field_set & (1<<UF_SCHEMA)) {
    return mrb_str_substr(mrb, mrb_iv_get(mrb, context->instance, mrb_intern(mrb, "buf")), context->handle.field_data[UF_SCHEMA].off, context->handle.field_data[UF_SCHEMA].len);
  }
  return mrb_nil_value();
}


static mrb_value
mrb_http_request_host(mrb_state *mrb, mrb_value self)
{
  mrb_value value_context;
  mrb_http_parser_context* context;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "parser_context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (context->handle.field_set & (1<<UF_HOST)) {
    return mrb_str_substr(mrb, CTXV_GET(context, "buf"), context->handle.field_data[UF_HOST].off, context->handle.field_data[UF_HOST].len);
  }
  return mrb_nil_value();
}

static mrb_value
mrb_http_request_port(mrb_state *mrb, mrb_value self)
{
  mrb_value value_context;
  mrb_http_parser_context* context;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "parser_context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (context->handle.field_set & (1<<UF_PORT)) {
    return mrb_fixnum_value(context->handle.port);
  }
  mrb_value schema = mrb_http_request_schema(mrb, self);
  if (!mrb_nil_p(schema) && !strncmp("https", (char*) RSTRING_PTR(schema), RSTRING_LEN(schema))) {
    return mrb_fixnum_value(443);
  }
  return mrb_fixnum_value(80);
}

static mrb_value
mrb_http_request_path(mrb_state *mrb, mrb_value self)
{
  mrb_value value_context;
  mrb_http_parser_context* context;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "parser_context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (context->handle.field_set & (1<<UF_PATH)) {
    return mrb_str_substr(mrb, CTXV_GET(context, "buf"), context->handle.field_data[UF_PATH].off, context->handle.field_data[UF_PATH].len);
  }
  return mrb_nil_value();
}

static mrb_value
mrb_http_request_query(mrb_state *mrb, mrb_value self)
{
  mrb_value value_context;
  mrb_http_parser_context* context;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "parser_context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (context->handle.field_set & (1<<UF_QUERY)) {
    return mrb_str_substr(mrb, CTXV_GET(context, "buf"), context->handle.field_data[UF_QUERY].off, context->handle.field_data[UF_QUERY].len);
  }
  return mrb_nil_value();
}

static mrb_value
mrb_http_request_fragment(mrb_state *mrb, mrb_value self)
{
  mrb_value value_context;
  mrb_http_parser_context* context;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "parser_context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (context->handle.field_set & (1<<UF_FRAGMENT)) {
    return mrb_str_substr(mrb, CTXV_GET(context, "buf"), context->handle.field_data[UF_FRAGMENT].off, context->handle.field_data[UF_FRAGMENT].len);
  }
  return mrb_nil_value();
}

static mrb_value
mrb_http_request_headers(mrb_state *mrb, mrb_value self)
{
  mrb_value value_context;
  mrb_http_parser_context* context;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "parser_context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  return CTXV_GET(context, "headers");
}

/*********************************************************
 * response
 *********************************************************/

/*********************************************************
 * register
 *********************************************************/

void
mrb_http_init(mrb_state* mrb) {
  _class_http = mrb_define_module(mrb, "HTTP");

  _class_http_parser = mrb_define_class_under(mrb, _class_http, "Parser", mrb->object_class);
  mrb_define_method(mrb, _class_http_parser, "initialize", mrb_http_parser_init, ARGS_OPT(1));
  mrb_define_method(mrb, _class_http_parser, "parse", mrb_http_parser_parse, ARGS_OPT(2));
  mrb_define_method(mrb, _class_http_parser, "execute", mrb_http_parser_execute, ARGS_REQ(1));

  _class_http_request = mrb_define_class_under(mrb, _class_http, "Request", mrb->object_class);
  mrb_define_method(mrb, _class_http_request, "schema", mrb_http_request_schema, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "host", mrb_http_request_host, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "port", mrb_http_request_port, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "path", mrb_http_request_path, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "query", mrb_http_request_query, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "fragment", mrb_http_request_fragment, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "headers", mrb_http_request_headers, ARGS_NONE());

  _class_http_response = mrb_define_class_under(mrb, _class_http, "Response", mrb->object_class);
}

/* vim:set et ts=2 sts=2 sw=2 tw=0: */
