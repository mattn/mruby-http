require 'HTTP'
require 'UV'

s = UV::TCP.new()
s.bind(UV::ip4_addr('127.0.0.1', 8888))
s.listen(5) {|x|
  return if x != 0
  c = s.accept()
  c.read_start {|b|
    c.data = HTTP::Parser.new()
    c.data.parse(b) {|x|
      # TODO: response object
      c.write("HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nhello\n") {|r|
        c.close()
		c = nil
      }
    }
  }
}
UV::run()
