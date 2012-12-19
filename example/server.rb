h = HTTP::Parser.new()
s = UV::TCP.new()
s.bind(UV::ip4_addr('127.0.0.1', 8888))
s.listen(1024) {|x|
  return if x != 0
  c = s.accept()
  c.read_start {|b|
    return unless b
    r = h.parse_request(b)
    body = "hello #{r.path}"
    c.write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: #{body.size}\r\n\r\n#{body}") {|x|
      c.close() if c
      c = nil
    }
  }
}

t = UV::Timer.new
t.start(3000, 3000) {|x|
  UV::gc()
  GC.start
}

UV::run()
