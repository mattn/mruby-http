#include <mruby.h>
#include <mruby/proc.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <http_parser.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HEADER_NAME_LEN 1024
#define MAX_HEADERS         128

/*********************************************************
 * parser
 *********************************************************/

typedef struct {
  mrb_state *mrb;
  int type;
  struct http_parser parser;
  struct http_parser_url handle;
  struct http_parser_settings settings;
  int was_header_value;
  mrb_value instance;
} mrb_http_parser_context;

static void
http_parser_context_free(mrb_state *mrb, void *p)
{
  free(p);
}

static const struct mrb_data_type
http_parser_context_type = {
  "mrb_http_parser_context", http_parser_context_free,
};

static void
http_url_free(mrb_state *mrb, void *p)
{
  free(p);
}

static const struct mrb_data_type http_url_type = {
  "mrb_http_url", http_url_free,
};

static int
parser_settings_on_url(http_parser* parser, const char *at, size_t len)
{
  int ai;
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  if(http_parser_parse_url(at, len, FALSE, &context->handle) != 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  ai = mrb_gc_arena_save(mrb);
  mrb_iv_set(mrb, context->instance, mrb_intern(mrb, "buf"), mrb_str_new(mrb, at, len));
  mrb_gc_arena_restore(mrb, ai);
  return 0;
}

#define OBJECT_GET(mrb, instance, name) \
  mrb_iv_get(mrb, instance, mrb_intern(mrb, name))

#define OBJECT_SET(mrb, instance, name, value) \
  mrb_iv_set(mrb, instance, mrb_intern(mrb, name), value)

#define OBJECT_REMOVE(mrb, instance, name) \
  mrb_iv_remove(mrb, instance, mrb_intern(mrb, name))

static int
parser_settings_on_header_field(http_parser* parser, const char* at, size_t len)
{
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  int ai = mrb_gc_arena_save(mrb);
  if (context->was_header_value) {
    if (!mrb_nil_p(OBJECT_GET(mrb, context->instance, "last_header_field"))) {
      mrb_str_concat(mrb, OBJECT_GET(mrb, context->instance, "last_header_field"), OBJECT_GET(mrb, context->instance, "last_header_value"));
      OBJECT_SET(mrb, context->instance, "last_header_value", mrb_nil_value());
    }
    OBJECT_SET(mrb, context->instance, "last_header_field", mrb_str_new(mrb, at, len));
    context->was_header_value = FALSE;
  } else {
    mrb_str_concat(mrb, OBJECT_GET(mrb, context->instance, "last_header_field"), mrb_str_new(mrb, at, len));
  }
  mrb_gc_arena_restore(mrb, ai);
  return 0;
}

static int
parser_settings_on_header_value(http_parser* parser, const char* at, size_t len)
{
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  int ai = mrb_gc_arena_save(mrb);
  if(!context->was_header_value) {
    OBJECT_SET(mrb, context->instance, "last_header_value", mrb_str_new(mrb, at, len));
    context->was_header_value = TRUE;
    mrb_hash_set(mrb, OBJECT_GET(mrb, context->instance, "headers"),
        OBJECT_GET(mrb, context->instance, "last_header_field"),
        OBJECT_GET(mrb, context->instance, "last_header_value"));
  } else {
    mrb_str_concat(mrb, OBJECT_GET(mrb, context->instance, "last_header_value"), mrb_str_new(mrb, at, len));
  }
  mrb_gc_arena_restore(mrb, ai);
  return 0;
}

static int
parser_settings_on_headers_complete(http_parser* parser)
{
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  int ai = mrb_gc_arena_save(mrb);
  if(!mrb_nil_p(OBJECT_GET(mrb, context->instance, "last_header_field"))) {
    mrb_hash_set(mrb, OBJECT_GET(mrb, context->instance, "headers"),
        OBJECT_GET(mrb, context->instance, "last_header_field"),
        OBJECT_GET(mrb, context->instance, "last_header_value"));
  }
  mrb_gc_arena_restore(mrb, ai);
  return 0;
}

static int
parser_settings_on_body(http_parser *parser, const char *p, size_t len)
{
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;

  int ai = mrb_gc_arena_save(mrb);
  OBJECT_SET(mrb, context->instance, "body", mrb_str_new(mrb, p, len));
  mrb_gc_arena_restore(mrb, ai);
  return 0;
}

static int
parser_settings_on_message_complete(http_parser* parser)
{
  mrb_http_parser_context *context = (mrb_http_parser_context*) parser->data;
  mrb_state* mrb = context->mrb;
  mrb_value c = context->instance;

  if (context->handle.field_set & (1<<UF_SCHEMA)) {
    OBJECT_SET(mrb, c, "schema", mrb_str_substr(mrb, OBJECT_GET(mrb, c, "buf"), context->handle.field_data[UF_SCHEMA].off, context->handle.field_data[UF_SCHEMA].len));
  }
  if (context->handle.field_set & (1<<UF_HOST)) {
    OBJECT_SET(mrb, c, "host", mrb_str_substr(mrb, OBJECT_GET(mrb, c, "buf"), context->handle.field_data[UF_HOST].off, context->handle.field_data[UF_HOST].len));
  }
  if (context->handle.field_set & (1<<UF_HOST)) {
    OBJECT_SET(mrb, c, "host", mrb_str_substr(mrb, OBJECT_GET(mrb, c, "buf"), context->handle.field_data[UF_HOST].off, context->handle.field_data[UF_HOST].len));
  }
  if (context->handle.field_set & (1<<UF_PORT)) {
    OBJECT_SET(mrb, c, "port", mrb_fixnum_value(context->handle.port));
  } else {
    if (context->handle.field_set & (1<<UF_SCHEMA)) {
      mrb_value schema = mrb_str_substr(mrb, OBJECT_GET(mrb, c, "buf"), context->handle.field_data[UF_SCHEMA].off, context->handle.field_data[UF_SCHEMA].len);
      if (!mrb_nil_p(schema) && !strcmp("https", (char*) RSTRING_PTR(schema))) {
        OBJECT_SET(mrb, c, "port", mrb_fixnum_value(443));
      }
    }
  }
  if (context->handle.field_set & (1<<UF_PATH)) {
    OBJECT_SET(mrb, c, "path", mrb_str_substr(mrb, OBJECT_GET(mrb, c, "buf"), context->handle.field_data[UF_PATH].off, context->handle.field_data[UF_PATH].len));
  }
  if (context->handle.field_set & (1<<UF_QUERY)) {
    OBJECT_SET(mrb, c, "query", mrb_str_substr(mrb, OBJECT_GET(mrb, c, "buf"), context->handle.field_data[UF_QUERY].off, context->handle.field_data[UF_QUERY].len));
  }
  OBJECT_SET(mrb, c, "method", mrb_str_new_cstr(mrb, http_method_str(context->parser.method)));
  OBJECT_SET(mrb, c, "status_code", mrb_fixnum_value(context->parser.status_code));
  OBJECT_SET(mrb, c, "content_length", mrb_fixnum_value(context->parser.content_length));
  OBJECT_REMOVE(mrb, c, "last_header_field");
  OBJECT_REMOVE(mrb, c, "last_header_value");
  OBJECT_REMOVE(mrb, c, "buf");

  return 0;
}

static mrb_value
mrb_http_parser_init(mrb_state *mrb, mrb_value self)
{
  mrb_http_parser_context* context = NULL;

  context = (mrb_http_parser_context*) malloc(sizeof(mrb_http_parser_context));
  memset(context, 0, sizeof(mrb_http_parser_context));
  context->mrb = mrb;
  context->instance = mrb_nil_value();
  mrb_iv_set(mrb, self, mrb_intern(mrb, "context"), mrb_obj_value(
    Data_Wrap_Struct(mrb, mrb->object_class,
    &http_parser_context_type, (void*) context)));
  return self;
}

static mrb_value
_http_parser_parse(mrb_state *mrb, mrb_value self, int type)
{
  mrb_value arg_data = mrb_nil_value();
  mrb_value value_context;
  mrb_http_parser_context* context;
  mrb_value b = mrb_nil_value();
  struct RClass* _class_http;
  struct RClass* clazz;
  char* data;
  size_t len;
  char* eol;
  size_t done;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  mrb_get_args(mrb, "|&o", &b, &arg_data);
  if (mrb_nil_p(arg_data)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  context->parser.data = context;

  _class_http = mrb_class_get(mrb, "HTTP");
  if (type == HTTP_REQUEST) {
    clazz = mrb_class_ptr(mrb_const_get(mrb, mrb_obj_value(_class_http), mrb_intern(mrb, "Request")));
    context->instance = mrb_obj_new(mrb, clazz, 0, NULL);
  } else {
    clazz = mrb_class_ptr(mrb_const_get(mrb, mrb_obj_value(_class_http), mrb_intern(mrb, "Response")));
    context->instance = mrb_obj_new(mrb, clazz, 0, NULL);
  }
  context->was_header_value = TRUE;

  http_parser_init(&context->parser, type);

  context->type = type;
  context->settings.on_url = parser_settings_on_url;
  context->settings.on_header_field = parser_settings_on_header_field;
  context->settings.on_header_value = parser_settings_on_header_value;
  context->settings.on_headers_complete = parser_settings_on_headers_complete;
  context->settings.on_body = parser_settings_on_body;
  context->settings.on_message_complete = parser_settings_on_message_complete;

  data = RSTRING_PTR(arg_data);
  len = RSTRING_LEN(arg_data);

  eol = strpbrk(data, "\r\n");
  if (eol) {
  }

RETRY:
  if (len > 10 && (!strncmp(data+9, "200 Connection established\r\n", 28) ||
      !strncmp(data+9, "100 Continue\r\n", 14) || *(data+9) == '3')) {
    char* next = strstr(data, "\r\n\r\n");
    if (next) {
      len -= (next + 4 - data);
      data = next + 4;
      goto RETRY;
    }
  }

  done = http_parser_execute(&context->parser, &context->settings, data, len);
  if (done < len) {
    OBJECT_SET(mrb, context->instance, "body", mrb_str_new(mrb, data + done, len - done));
  }

  if (!mrb_nil_p(b)) {
    mrb_value args[1];
    args[0] = context->instance;
    mrb_yield_argv(mrb, b, 1, args);
    return mrb_nil_value();
  }
  return context->instance;
}

static mrb_value
mrb_http_parser_parse_request(mrb_state *mrb, mrb_value self)
{
  return _http_parser_parse(mrb, self, HTTP_REQUEST);
}

static mrb_value
mrb_http_parser_parse_response(mrb_state *mrb, mrb_value self)
{
  return _http_parser_parse(mrb, self, HTTP_RESPONSE);
}

static mrb_value
mrb_http_parser_execute(mrb_state *mrb, mrb_value self)
{
  mrb_value arg_data;
  mrb_value value_context;
  mrb_http_parser_context* context;
  size_t done;

  value_context = mrb_iv_get(mrb, self, mrb_intern(mrb, "context"));
  Data_Get_Struct(mrb, value_context, &http_parser_context_type, context);
  if (!context) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  mrb_get_args(mrb, "o", &arg_data);
  if (mrb_nil_p(arg_data)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  done = http_parser_execute(&context->parser, &context->settings, (char*) RSTRING_PTR(arg_data), RSTRING_LEN(arg_data));

  return mrb_fixnum_value(done);
}

static mrb_value
mrb_http_parser_parse_url(mrb_state *mrb, mrb_value self)
{
  mrb_value c;
  mrb_value arg_data;
  struct http_parser_url handle = {0};
  struct RClass* _class_http, *_class_http_url;

  mrb_get_args(mrb, "S", &arg_data);

  if (http_parser_parse_url(RSTRING_PTR(arg_data), RSTRING_LEN(arg_data), FALSE, &handle)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid URL");
  }

  _class_http = mrb_class_get(mrb, "HTTP");
  _class_http_url = mrb_class_ptr(mrb_const_get(mrb, mrb_obj_value(_class_http), mrb_intern(mrb, "URL")));

  c = mrb_obj_new(mrb, _class_http_url, 0, NULL);

  if (handle.field_set & (1<<UF_SCHEMA)) {
    OBJECT_SET(mrb, c, "schema", mrb_str_substr(mrb, arg_data, handle.field_data[UF_SCHEMA].off, handle.field_data[UF_SCHEMA].len));
  }
  if (handle.field_set & (1<<UF_HOST)) {
    OBJECT_SET(mrb, c, "host", mrb_str_substr(mrb, arg_data, handle.field_data[UF_HOST].off, handle.field_data[UF_HOST].len));
  }
  if (handle.field_set & (1<<UF_HOST)) {
    OBJECT_SET(mrb, c, "host", mrb_str_substr(mrb, arg_data, handle.field_data[UF_HOST].off, handle.field_data[UF_HOST].len));
  }
  if (handle.field_set & (1<<UF_PORT)) {
    OBJECT_SET(mrb, c, "port", mrb_fixnum_value(handle.port));
  } else {
    if (handle.field_set & (1<<UF_SCHEMA)) {
      mrb_value schema = mrb_str_substr(mrb, arg_data, handle.field_data[UF_SCHEMA].off, handle.field_data[UF_SCHEMA].len);
      if (!mrb_nil_p(schema) && !strcmp("https", (char*) RSTRING_PTR(schema))) {
        OBJECT_SET(mrb, c, "port", mrb_fixnum_value(443));
      }
    }
  }
  if (handle.field_set & (1<<UF_PATH)) {
    OBJECT_SET(mrb, c, "path", mrb_str_substr(mrb, arg_data, handle.field_data[UF_PATH].off, handle.field_data[UF_PATH].len));
  }
  if (handle.field_set & (1<<UF_QUERY)) {
    OBJECT_SET(mrb, c, "query", mrb_str_substr(mrb, arg_data, handle.field_data[UF_QUERY].off, handle.field_data[UF_QUERY].len));
  }
  if (handle.field_set & (1<<UF_FRAGMENT)) {
    OBJECT_SET(mrb, c, "fragment", mrb_str_substr(mrb, arg_data, handle.field_data[UF_FRAGMENT].off, handle.field_data[UF_FRAGMENT].len));
  }

  return c;
}

/*********************************************************
 * object
 *********************************************************/

static mrb_value
mrb_http_object_initialize(mrb_state *mrb, mrb_value self)
{
  OBJECT_SET(mrb, self, "headers", mrb_hash_new(mrb));
  OBJECT_SET(mrb, self, "body", mrb_nil_value());
  OBJECT_SET(mrb, self, "method", mrb_str_new_cstr(mrb, "GET"));
  return self;
}

static mrb_value
mrb_http_object_status_code_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "status_code");
}

static mrb_value
mrb_http_object_message_get(mrb_state *mrb, mrb_value self)
{
  const char* message = NULL;
  switch(mrb_fixnum(OBJECT_GET(mrb, self, "status_code"))) {
    case 100: message = "Continue"; break;
    case 101: message = "Switching Protocols"; break;
    case 200: message = "OK"; break;
    case 201: message = "Created"; break;
    case 202: message = "Accepted"; break;
    case 203: message = "Non-Authoritative Information"; break;
    case 204: message = "No Content"; break;
    case 205: message = "Reset Content"; break;
    case 206: message = "Partial Content"; break;
    case 300: message = "Multiple Choices"; break;
    case 301: message = "Moved Permanently"; break;
    case 302: message = "Found"; break;
    case 303: message = "See Other"; break;
    case 304: message = "Not Modified"; break;
    case 305: message = "Use Proxy"; break;
              //case 306: message = "(reserved)"; break;
    case 307: message = "Temporary Redirect"; break;
    case 400: message = "Bad Request"; break;
    case 401: message = "Unauthorized"; break;
    case 402: message = "Payment Required"; break;
    case 403: message = "Forbidden"; break;
    case 404: message = "Not Found"; break;
    case 405: message = "Method Not Allowed"; break;
    case 406: message = "Not Acceptable"; break;
    case 407: message = "Proxy Authentication Required"; break;
    case 408: message = "Request Timeout"; break;
    case 409: message = "Conflict"; break;
    case 410: message = "Gone"; break;
    case 411: message = "Length Required"; break;
    case 412: message = "Precondition Failed"; break;
    case 413: message = "Request Entity Too Large"; break;
    case 414: message = "Request-URI Too Long"; break;
    case 415: message = "Unsupported Media Type"; break;
    case 416: message = "Requested Range Not Satisfiable"; break;
    case 417: message = "Expectation Failed"; break;
    case 500: message = "Internal Server Error"; break;
    case 501: message = "Not Implemented"; break;
    case 502: message = "Bad Gateway"; break;
    case 503: message = "Service Unavailable"; break;
    case 504: message = "Gateway Timeout"; break;
    case 505: message = "HTTP Version Not Supported"; break;
    default: mrb_raise(mrb, E_RUNTIME_ERROR, "Not supported status code.");
  }
  return mrb_str_new_cstr(mrb, message);
}

static mrb_value
mrb_http_object_content_length_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "content_length");
}

static mrb_value
mrb_http_object_schema_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "schema");
}

static mrb_value
mrb_http_object_host_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "host");
}

static mrb_value
mrb_http_object_port_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "port");
}

static mrb_value
mrb_http_object_path_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "path");
}

static mrb_value
mrb_http_object_query_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "query");
}

static mrb_value
mrb_http_object_fragment_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "fragment");
}

static mrb_value
mrb_http_object_headers_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "headers");
}

static mrb_value
mrb_http_object_headers_set_item(mrb_state *mrb, mrb_value self)
{
  mrb_value key, value;
  mrb_get_args(mrb, "SS", &key, &value);
  mrb_hash_set(mrb, OBJECT_GET(mrb, self, "headers"), key, value);
  return mrb_nil_value();
}

static mrb_value
mrb_http_object_method_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "method");
}

static mrb_value
mrb_http_object_method_set(mrb_state *mrb, mrb_value self)
{
  mrb_value arg;
  mrb_get_args(mrb, "S", &arg);
  OBJECT_SET(mrb, self, "method", arg);
  return mrb_nil_value();
}

static mrb_value
mrb_http_object_body_get(mrb_state *mrb, mrb_value self)
{
  return OBJECT_GET(mrb, self, "body");
}

static mrb_value
mrb_http_object_body_set(mrb_state *mrb, mrb_value self)
{
  mrb_value arg;
  mrb_get_args(mrb, "S", &arg);
  OBJECT_SET(mrb, self, "body", arg);
  return mrb_nil_value();
}

/*********************************************************
 * response
 *********************************************************/

/*********************************************************
 * url
 *********************************************************/

static char
from_hex(char ch) {
  return ('0' <= ch && ch <= '9') ? (ch - '0') :
    ((('A' <= ch && ch <= 'Z') ? (ch + 32) : ch) - 'a' + 10);
}

static char
to_hex(char code) {
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

static mrb_value
mrb_http_url_encode(mrb_state *mrb, mrb_value self) {
  mrb_value arg;
  char* str;
  char *pstr, *buf, *pbuf;

  mrb_get_args(mrb, "S", &arg);
  str = RSTRING_PTR(arg);

  pstr = str;
  buf = malloc(strlen(str) * 3 + 1);
  pbuf = buf;
  while (*pstr) {
    char c = *pstr;
    if ((('a' <= c && c <= 'z') ||
         ('A' <= c && c <= 'Z') ||
         ('0' <= c && c <= '9')) ||
        c == '-' || c == '_' || c == '.' || c == '~') 
      *pbuf++ = *pstr;
    else if (c == ' ') 
      *pbuf++ = '+';
    else 
      *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
    pstr++;
  }
  *pbuf = '\0';
  arg = mrb_str_new(mrb, buf, pbuf - buf);
  free(buf);
  return arg;
}

static mrb_value
mrb_http_url_decode(mrb_state *mrb, mrb_value self) {
  mrb_value arg;
  char* str;
  char *pstr, *buf, *pbuf;

  mrb_get_args(mrb, "S", &arg);
  str = RSTRING_PTR(arg);

  pstr = str;
  buf = malloc(RSTRING_LEN(arg) + 1);
  pbuf = buf;
  while (*pstr) {
    if (*pstr == '%') {
      if (pstr[1] && pstr[2]) {
        *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
      }
    } else if (*pstr == '+') { 
      *pbuf++ = ' ';
    } else {
      *pbuf++ = *pstr;
    }
    pstr++;
  }
  *pbuf = '\0';
  arg = mrb_str_new(mrb, buf, pbuf - buf);
  free(buf);
  return arg;
}

/*********************************************************
 * register
 *********************************************************/

void
mrb_mruby_http_gem_init(mrb_state* mrb) {

  struct RClass* _class_http;
  struct RClass* _class_http_parser;
  struct RClass* _class_http_request;
  struct RClass* _class_http_response;
  struct RClass *_class_http_url;
  int ai = mrb_gc_arena_save(mrb);

  _class_http = mrb_define_module(mrb, "HTTP");
  _class_http_parser = mrb_define_class_under(mrb, _class_http, "Parser", mrb->object_class);
  mrb_define_method(mrb, _class_http_parser, "initialize", mrb_http_parser_init, ARGS_OPT(1));
  mrb_define_method(mrb, _class_http_parser, "parse_request", mrb_http_parser_parse_request, ARGS_OPT(2));
  mrb_define_method(mrb, _class_http_parser, "parse_response", mrb_http_parser_parse_response, ARGS_OPT(2));
  mrb_define_method(mrb, _class_http_parser, "parse_url", mrb_http_parser_parse_url, ARGS_REQ(1));
  mrb_define_method(mrb, _class_http_parser, "execute", mrb_http_parser_execute, ARGS_REQ(1));
  mrb_gc_arena_restore(mrb, ai);

  _class_http_request = mrb_define_class_under(mrb, _class_http, "Request", mrb->object_class);
  mrb_define_method(mrb, _class_http_request, "initialize", mrb_http_object_initialize, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "schema", mrb_http_object_schema_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "host", mrb_http_object_host_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "port", mrb_http_object_port_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "path", mrb_http_object_path_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "query", mrb_http_object_query_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "headers", mrb_http_object_headers_get, ARGS_NONE());
  //mrb_define_method(mrb, _class_http_request, "headers[]=", mrb_http_object_headers_set_item, ARGS_REQ(2));
  mrb_define_method(mrb, _class_http_request, "method", mrb_http_object_method_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "method=", mrb_http_object_method_set, ARGS_REQ(1));
  mrb_define_method(mrb, _class_http_request, "body", mrb_http_object_body_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_request, "body=", mrb_http_object_body_set, ARGS_REQ(1));
  mrb_gc_arena_restore(mrb, ai);

  _class_http_response = mrb_define_class_under(mrb, _class_http, "Response", mrb->object_class);
  mrb_define_method(mrb, _class_http_response, "initialize", mrb_http_object_initialize, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "status_code", mrb_http_object_status_code_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "message", mrb_http_object_message_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "content_length", mrb_http_object_content_length_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "schema", mrb_http_object_schema_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "host", mrb_http_object_host_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "port", mrb_http_object_port_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "path", mrb_http_object_path_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "query", mrb_http_object_query_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "headers", mrb_http_object_headers_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "method", mrb_http_object_method_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "body", mrb_http_object_body_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_response, "body=", mrb_http_object_body_set, ARGS_REQ(1));
  mrb_gc_arena_restore(mrb, ai);

  _class_http_url = mrb_define_class_under(mrb, _class_http, "URL", mrb->object_class);
  mrb_define_method(mrb, _class_http_url, "schema", mrb_http_object_schema_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_url, "host", mrb_http_object_host_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_url, "port", mrb_http_object_port_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_url, "path", mrb_http_object_path_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_url, "query", mrb_http_object_query_get, ARGS_NONE());
  mrb_define_method(mrb, _class_http_url, "fragment", mrb_http_object_fragment_get, ARGS_NONE());
  //mrb_define_method(mrb, _class_http_url, "to_url", mrb_http_url_to_url, ARGS_NONE());
  mrb_define_class_method(mrb, _class_http_url, "encode", mrb_http_url_encode, ARGS_REQ(1));
  mrb_define_class_method(mrb, _class_http_url, "decode", mrb_http_url_decode, ARGS_REQ(1));
  mrb_gc_arena_restore(mrb, ai);
}

void
mrb_mruby_http_gem_final(mrb_state* mrb) {
}

/* vim:set et ts=2 sts=2 sw=2 tw=0: */
