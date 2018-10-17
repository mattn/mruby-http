assert('HTTP::Parser.new.parse_request') do
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
  assert_equal 'mruby', req.headers['mruby']
  assert_equal "foo\tbar", req.headers['My-Header']
end

