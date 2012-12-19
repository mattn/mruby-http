#!mruby

h = HTTP::Parser.new()
q = "GET http://localhost:8080/foo?bar=baz#zzz HTTP/1.1\r\nHost: localhost\r\nUser-Agent: mruby\r\n\r\n"
h.parse_request(q) {|x|
  puts x.method
  puts x.schema
  puts x.host
  puts x.port
  puts x.path
  puts x.query
}
puts

q = "GET http://localhost/foo?bar=baz#zzz HTTP/1.1\r\nHost: localhost\r\nUser-Agent: mruby\r\n\r\n"
h.parse_request(q) {|x|
  puts x.method
  puts x.schema
  puts x.host
  puts x.port
  puts x.path
  puts x.query
}
puts

q = "POST http://localhost/foo?bar=baz#zzz HTTP/1.1\r\nHost: localhost\r\nUser-Agent: mruby\r\nContent-Length: 5\r\n\r\nhello"
h.parse_request(q) {|x|
  puts x.method
  puts x.schema
  puts x.host
  puts x.port
  puts x.path
  puts x.query
  puts x.body
}
puts
