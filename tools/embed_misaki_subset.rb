#!/usr/bin/env ruby
# Generates kernel/generated_misaki_font.c from Misaki BDF.
# Usage:
#   ruby tools/embed_misaki_subset.rb third_party/fonts/misaki/misaki_gothic.bdf

bdf_path = ARGV.shift or abort "usage: #{$PROGRAM_NAME} FONT.bdf"

FONT_W = 8
FONT_H = 8
FONT_ASCENT = 6

glyphs = {}

encoding = nil
bbx = nil
bitmap_lines = nil
in_bitmap = false

File.foreach(bdf_path) do |line|
  line = line.strip

  case line
  when /\AENCODING\s+(\d+)\z/
    encoding = Regexp.last_match(1).to_i
  when /\ABBX\s+(\d+)\s+(\d+)\s+(-?\d+)\s+(-?\d+)\z/
    bbx = Regexp.last_match.captures.map(&:to_i)
  when 'BITMAP'
    bitmap_lines = []
    in_bitmap = true
  when 'ENDCHAR'
    if encoding && encoding > 0x7f && bbx && bitmap_lines
      width, height, xoff, yoff = bbx
      top = FONT_ASCENT - (height + yoff)
      rows = Array.new(FONT_H, 0)

      bitmap_lines.each_with_index do |hex, row_idx|
        next if row_idx >= height
        target_y = top + row_idx
        next if target_y < 0 || target_y >= FONT_H

        bits = hex.to_i(16)
        width.times do |bit_idx|
          target_x = xoff + bit_idx
          next if target_x < 0 || target_x >= FONT_W
          source_mask = 1 << (7 - bit_idx)
          next if (bits & source_mask).zero?
          rows[target_y] |= 1 << (7 - target_x)
        end
      end

      glyphs[encoding] = rows
    end

    encoding = nil
    bbx = nil
    bitmap_lines = nil
    in_bitmap = false
  else
    bitmap_lines << line if in_bitmap && !line.empty?
  end
end

puts '#include <stdint.h>'
puts ''
puts 'struct misaki_glyph {'
puts '    uint32_t codepoint;'
puts '    uint8_t rows[8];'
puts '};'
puts ''
puts 'static const struct misaki_glyph generated_misaki_glyphs[] = {'
glyphs.sort.each do |cp, rows|
  row_bytes = rows.map { |v| format('0x%02x', v) }.join(', ')
  puts format('    { 0x%04x, { %s } },', cp, row_bytes)
end
puts '};'
puts ''
puts format('static const unsigned long generated_misaki_glyph_count = %d;', glyphs.length)
