#!/usr/bin/env ruby
# Usage: ruby tools/embed_script.rb ruby/scripts/hello.rb hello_rb > kernel/generated_hello.c

path = ARGV.fetch(0)
name = ARGV.fetch(1)
content = File.binread(path)
bytes = content.bytes.map { |b| sprintf("0x%02x", b) }.join(", ")

puts "static const unsigned char #{name}[] = { #{bytes}, 0x00 };"
puts "static const unsigned long #{name}_size = #{content.bytesize};"
