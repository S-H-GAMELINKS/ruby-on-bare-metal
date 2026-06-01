#!/usr/bin/env ruby
# Generates kernel/generated_scripts.c from ruby/scripts/*.{rb,md}
# Usage: ruby tools/embed_scripts.rb ruby/scripts/ [extra files...] > kernel/generated_scripts.c

dir = ARGV.fetch(0)
extra_files = ARGV.drop(1)
files = Dir.glob(File.join(dir, "*.{rb,md}")).sort.map { |path|
  [path, "/#{File.basename(path)}"]
}
files.concat(extra_files.map { |path| [path, "/#{path}"] })

puts '#include "kernel.h"'
puts '#include <stddef.h>'
puts ''

# Emit each script as a C byte array
files.each do |path, vfs_path|
  c_name = "script_" + vfs_path.gsub(/[^A-Za-z0-9]+/, "_")
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
files.each do |_path, vfs_path|
  c_name = "script_" + vfs_path.gsub(/[^A-Za-z0-9]+/, "_")
  puts "    { \"#{vfs_path}\", #{c_name}, sizeof(#{c_name}) - 1 },"
end
puts "    { 0, 0, 0 }"
puts "};"
