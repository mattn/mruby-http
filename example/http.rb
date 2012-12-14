h = HTTP::Parser.new()
q = "GET http://localhost:8080/foo?bar=baz#zzz HTTP/1.1\r\nHost: localhost\r\nUser-Agent: mruby\r\n\r\n"
h.parse_request(q) {|x|
  puts x.schema
  puts x.host
  puts x.port
  puts x.path
  puts x.query
}
