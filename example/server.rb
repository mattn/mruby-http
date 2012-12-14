s = UV::TCP.new()
s.bind(UV::ip4_addr('127.0.0.1', 8888))
s.data = []
s.listen(200) {|x|
  return if x != 0
  c = s.accept()
  c.read_start {|b|
    h = HTTP::Parser.new()
    h.parse_request(b) {|r|
      body = "hello #{r.path}"
      c.write("HTTP/1.1 200 OK\r\nContent-Length: #{body.size}\r\n\r\n#{body}") {|c, x|
        c.close()
      }
    }
  }
}

UV::run()
