h = HTTP::Parser.new()
s = UV::TCP.new()
s.bind(UV::ip4_addr('127.0.0.1', 8888))
s.listen(1024) do |x|
  return if x != 0
  c = s.accept()
  c.read_start do |b|
    next unless b
    r = h.parse_request(b)
    body = "hello #{r.path}"
    if !r.headers.has_key?('Connection') || r.headers['Connection'] != 'Keep-Alive'
      c.write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: #{body.size}\r\n\r\n#{body}") do |x|
        c.close() if c
        c = nil
      end
    else
      c.write("HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Length: #{body.size}\r\n\r\n#{body}")
    end
  end
end

t = UV::Timer.new
t.start(3000, 3000) {|x|
  UV::gc()
  GC.start
}

UV::run()
