#include <mruby.h>
#include <mruby/proc.h>
#include <mruby/data.h>
#include <compile.h>
#include <mrb_http.h>

#define _(...) #__VA_ARGS__ "\n"

int
main()
{
  int n;
  mrb_state* mrb;
  struct mrb_parser_state* st;
  char* code =
 _(
)_( require 'HTTP'
)_( d = "GET / HTTP/1.1\r\nHost: example.com\r\nLocation: /foo\r\n\r\n"
)_( puts "\n---------------\n#{d}"
)_( p HTTP::parse_http_request(d)
)_( d = "HTTP/1.1 200 OK\r\nHost: example.com\r\nLocation: /foo\r\n\r\ngoto foo"
)_( puts "\n---------------\n#{d}"
)_( p HTTP::parse_http_response(d)
);

  mrb = mrb_open();
  mrb_http_init(mrb);
  st = mrb_parse_string(mrb, code);
  n = mrb_generate_code(mrb, st->tree);
  mrb_pool_close(st->pool);
  mrb_run(mrb, mrb_proc_new(mrb, mrb->irep[n]), mrb_nil_value());
  return 0;
}
