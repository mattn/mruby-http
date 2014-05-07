cmap = {
 'txt'  => 'text/plain',
 'html' => 'text/html',
 'css'  => 'text/css',
 'jpg'  => 'image/jpeg',
 'png'  => 'image/png',
 'gif'  => 'image/gif',
}
h = HTTP::Parser.new()
s = UV::TCP.new()
s.bind(UV::ip4_addr('127.0.0.1', 8888))
s.listen(1024) do |x|
  return if x != 0
  c = s.accept()
  c.data = ''
  c.read_start do |b|
    next unless b
    c.data += b
    i = c.data.index("\r\n\r\n")
    next if i == nil || i < 0
    r = h.parse_request(b)
    keep_alive = r.headers.has_key?('Connection') && r.headers['Connection'] == 'Keep-Alive'
    path = r.path || '/'
    file = path + (path[-1] == '/' ? 'index.html' : '')
    size = -1
    nr = 0
    begin
      cc = keep_alive ? "keep-alive" : "close"
      size = UV::FS::stat("public/#{file}").size
      ext = file.split(".")[-1]
      ctype = cmap[ext] || 'application/octet-stream'
      header = "HTTP/1.1 200 OK\r\nConnection: #{cc}\r\nContent-Type: #{ctype}\r\nContent-Length: #{size}\r\n\r\n"
      f = UV::FS::open("public#{file}", UV::FS::O_RDONLY, UV::FS::S_IREAD)
      begin
        while nr < size
          read = f.read(8192, nr)
          if nr == 0
            c.write(header + read)
          else
            c.write(read)
          end
          nr += read.size
        end
      rescue
      end
      f.close
    rescue
      if == 0
        if size >= 0
          c.write("HTTP/1.0 500 Internal Server Error\r\n\r\nInternal Server Error") if c
        else
          c.write("HTTP/1.0 404 Not Found\r\n\r\nInternal Server Error") if c
        end if c
      end
      keep_alive = false
    end
    unless c && keep_alive
      c.close
      c = nil
    end
  end
end

t = UV::Timer.new
t.start(3000, 3000) {|x|
  UV::gc()
  GC.start
}

UV::run()
