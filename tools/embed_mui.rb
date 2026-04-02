#!/usr/bin/env ruby
# Generates kernel/generated_mui.c from mui lib/ sources
# Usage: ruby tools/embed_mui.rb third_party/mui/lib/ > kernel/generated_mui.c

dir = ARGV.fetch(0)
files = Dir.glob(File.join(dir, "**/*.rb")).sort

# curses.rb is replaced with a stub for Ruby on Bare Metal

puts '/* Auto-generated: mui editor source files */'
puts ''

files.each_with_index do |path, idx|
  rel = path.sub(dir, "").sub(/^\//, "")
  c_name = "mui_file_#{idx}"
  content = File.binread(path)

  # Remove 'require "curses"' lines
  content = content.gsub(/^require\s+["']curses["']\s*$/, "# curses removed for Ruby on Bare Metal")

  bytes = content.bytes.each_slice(16).map { |line|
    line.map { |b| sprintf("0x%02x", b) }.join(", ")
  }.join(",\n  ")

  puts "static const char #{c_name}[] = {"
  puts "  #{bytes},"
  puts "  0x00"
  puts "};"
  puts ""
end

# File table
puts "struct mui_file { const char *path; const char *data; unsigned long size; };"
puts "const struct mui_file mui_files[] = {"
files.each_with_index do |path, idx|
  rel = path.sub(dir, "").sub(/^\//, "")
  vfs_path = "/lib/#{rel}"
  c_name = "mui_file_#{idx}"
  puts "  { \"#{vfs_path}\", #{c_name}, sizeof(#{c_name}) - 1 },"
end
puts "  { 0, 0, 0 }"
puts "};"
