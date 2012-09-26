m = UV::Mutex.new()
s = UV::TCP.new()
s.bind(UV::ip4_addr('127.0.0.1', 8888))
s.listen(200) {|s, x|
  return if x != 0
  m.lock()
  c = s.accept()
  m.unlock()
  h = HTTP::Parser.new()
  c.read_start {|c, b|
    h.parse_request(b) {|h, r|
      #body = "hello #{r.path}"
      body = "hello"
      c.write("HTTP/1.1 200 OK\r\nContent-Length: #{body.size}\r\n\r\n#{body}") {|c, x|
        c.close()
      }
    }
    h = nil
  }
}

while 1
  UV::run_once()
end
