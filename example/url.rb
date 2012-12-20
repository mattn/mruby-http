#!mruby

h = HTTP::Parser.new()
puts h.parse_url("http://localhost:8080/foo?bar=baz#zzz").fragment
puts "俺の塩" == HTTP::URL::decode(HTTP::URL::encode("俺の塩"))
