def read_start(c, b)
	p 2
  h = HTTP::Parser.new()
  h.parse_request(b) {|h, r|
    # TODO: response object
    body = "hello #{r.path}"
    c.write("HTTP/1.1 200 OK\r\nContent-Length: #{body.size}\r\n\r\n#{body}") {|c, x|
      c.close
    }
  }
	p 3
end

s = UV::TCP.new()
s.bind(UV::ip4_addr('127.0.0.1', 8888))
#s.data = []
s.listen(50) {|s, x|
  return if x != 0
  c = s.accept()
  p 1
  p Kernel.methods(:read_start)
  p 2
  c.read_start Kernel.methods(:read_start)
  #s.data << c
}
while 1 do
  # NOTE: must be call run_once to run GC.
  UV::run_once()
end
