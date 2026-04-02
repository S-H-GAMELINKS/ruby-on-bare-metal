#!/usr/bin/env ruby
# Generates kernel/generated_scripts.c from ruby/scripts/*.rb
# Usage: ruby tools/embed_scripts.rb ruby/scripts/ > kernel/generated_scripts.c

dir = ARGV.fetch(0)
scripts = Dir.glob(File.join(dir, "*.rb")).sort

puts '#include "kernel.h"'
puts '#include <stddef.h>'
puts ''

# Emit each script as a C byte array
scripts.each do |path|
  name = File.basename(path, ".rb")
  c_name = "script_#{name}"
  content = File.binread(path)
  bytes = content.bytes.each_slice(16).map { |line|
    line.map { |b| sprintf("0x%02x", b) }.join(", ")
  }.join(",\n  ")

  puts "static const char #{c_name}[] = {"
  puts "  #{bytes},"
  puts "  0x00"
  puts "};"
  puts ""
end

# Emit file table
puts "const struct embedded_file generated_files[] = {"
scripts.each do |path|
  name = File.basename(path, ".rb")
  c_name = "script_#{name}"
  vfs_path = "/#{name}.rb"
  puts "    { \"#{vfs_path}\", #{c_name}, sizeof(#{c_name}) - 1 },"
end
puts "    { 0, 0, 0 }"
puts "};"
