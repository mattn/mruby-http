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
  
  assert_equal 'localhost', req['Host']
  assert_equal 'mruby', req['mruby']
  assert_equal "foo\tbar", req['My-Header']
end

