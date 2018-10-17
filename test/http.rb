assert('HTTP::Parser.new.parse_request for GET') do
  q = "GET http://localhost/foo?bar=baz#zzz HTTP/1.1\r\nHost: localhost\r\nMy-Header: foo\r\n\tbar\r\nUser-Agent: mruby\r\n\r\n"
  h = HTTP::Parser.new
  req = h.parse_request(q)
  
  assert_equal 'localhost', req.headers['Host']
  assert_equal 'mruby', req.headers['User-Agent']
  assert_equal "foo\tbar", req.headers['My-Header']
  assert_equal nil, req.body
end

assert('HTTP::Parser.new.parse_request for POST') do
  q = "POST http://localhost/foo?bar=baz#zzz HTTP/1.1\r\nHost: localhost\r\nMy-Header: foo\r\n\tbar\r\nUser-Agent: mruby\r\nContent-Length: 7\r\n\r\nfoobar\n"
  h = HTTP::Parser.new
  req = h.parse_request(q)
  
  assert_equal 'localhost', req.headers['Host']
  assert_equal 'mruby', req.headers['User-Agent']
  assert_equal "foo\tbar", req.headers['My-Header']
  assert_equal "foobar\n", req.body
end

assert('HTTP::Parser.new.parse_response') do
  q = "HTTP/1.1 200 OK\r\nHost: localhost\r\nMy-Header: foo\r\n\tbar\r\nUser-Agent: mruby\r\nContent-Length: 13\r\n\r\n<b>hello</b>\n"
  h = HTTP::Parser.new
  res = h.parse_response(q)
  
  assert_equal 200, res.status_code
  assert_equal 'OK', res.message
  assert_equal 'mruby', res.headers['User-Agent']
  assert_equal "foo\tbar", res.headers['My-Header']
  assert_equal "<b>hello</b>\n", res.body
end
