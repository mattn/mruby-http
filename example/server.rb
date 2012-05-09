require 'HTTP'
require 'UV'

s = UV::TCP.new()
s.bind(UV::ip4_addr('127.0.0.1', 8888))
s.data = []
s.listen(50) {|x|
  return if x != 0
  c = s.accept()
  c.read_start {|b|
    h = HTTP::Parser.new()
    h.parse_request(b) {|r|
      # TODO: response object
      body = "hello #{r.path}"
      c.write("HTTP/1.1 200 OK\r\nContent-Length: #{body.size}\r\n\r\n#{body}") {|x|
        c.close
      }
    }
  }
  s.data << c
}
while 1 do
  # NOTE: must be call run_once to run GC.
  UV::run_once()
end
