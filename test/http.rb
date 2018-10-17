assert('HTTP::Parser.new.parse_request for GET') do
  q = <<EOF
GET http://localhost/foo?bar=baz#zzz HTTP/1.1
Host: localhost
My-Header: foo
	bar
User-Agent: mruby

EOF
  h = HTTP::Parser.new
  req = h.parse_request(q)
  
  assert_equal 'localhost', req.headers['Host']
  assert_equal 'mruby', req.headers['User-Agent']
  assert_equal "foo\tbar", req.headers['My-Header']
  assert_equal nil, req.body
end

assert('HTTP::Parser.new.parse_request for POST') do
  q = <<EOF
POST http://localhost/foo?bar=baz#zzz HTTP/1.1
Host: localhost
My-Header: foo
	bar
User-Agent: mruby

foobar
EOF
  h = HTTP::Parser.new
  req = h.parse_request(q)
  
  assert_equal 'localhost', req.headers['Host']
  assert_equal 'mruby', req.headers['User-Agent']
  assert_equal "foo\tbar", req.headers['My-Header']
  assert_equal "foobar\n", req.body
end

assert('HTTP::Parser.new.parse_response') do
  q = <<EOF
My-Header: foo
	bar
User-Agent: mruby

<b>hello</b>
EOF
  h = HTTP::Parser.new
  req = h.parse_request(q)
  
  assert_equal 'mruby', req.headers['User-Agent']
  assert_equal "foo\tbar", req.headers['My-Header']
  assert_equal "<b>hello</b>", req.body
end
