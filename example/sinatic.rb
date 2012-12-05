# {{{
module Sinatic
  @routes = { 'GET' => [], 'POST' => [] }
  def self.route(method, path, opts, &block)
    @routes[method] << [path, opts, block]
  end
  def self.do(r)
    @routes[r.method].each {|path|
      if path[0] == r.path
        param = {}
        r.body.split('&').each {|x|
          tokens = x.split('=')
          param[tokens[0]] = HTTP::URL::decode(tokens[1])
        }
        body = path[2].call(r, param)
        return [
		  "HTTP/1.0 200 OK",
		  "Content-Type: text/html; charset=utf-8",
		  "Content-Length: #{body.size}",
		  "", ""].join("\r\n") + body
      end
	}
    return "HTTP/1.0 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n"
  end
  def self.run()
    s = UV::TCP.new()
    s.bind(UV::ip4_addr('127.0.0.1', 8888))
    s.data = []
    s.listen(50) {|s, x|
      return if x != 0
      c = s.accept()
      c.read_start {|c, b|
        h = HTTP::Parser.new()
        h.parse_request(b) {|h, r|
          i = b.index("\r\n\r\n") + 4
          r.body = b.slice(i, b.size - i)
          c.write(::Sinatic.do(r)) {|c, x| c.close() }
        }
      }
      s.data << c
    }
    UV::run()
  end
end

module Kernel
  def get(path, opts={}, &block)
    ::Sinatic.route 'GET', path, opts, &block
  end
  def post(path, opts={}, &block)
    ::Sinatic.route 'POST', path, opts, &block
  end
end
# }}}

get "/foo.js" do
'$(function() {
  $("#foo").text("hello world");
})'
end

get "/" do
'
<script src="http://code.jquery.com/jquery-latest.js"></script>
<script src="/foo.js"></script>
<div id="foo"></div>
<form action="/add" method="post">
<label for="name"/>お名前</label>
<input type="text" id="name" name="name" value="">
<input type="submit">
</form>
'
end

post "/add" do |r, param|
"
<meta http-equiv=refresh content='2; URL=/'>
通報しますた「#{param['name']}」
"
end

Sinatic.run

# vim: set fdm=marker:
