begin
  require 'io/console'
rescue LoadError
end

STDOUT.sync = true
STDERR.sync = true

TALK_SECONDS = 5 * 60
REFRESH_INTERVAL = 0.2

def detect_screen_size
  if $stdout.respond_to?(:winsize)
    rows, cols = $stdout.winsize
    if rows && cols && rows > 0 && cols > 0
      return [cols, rows]
    end
  end
  [80, 25]
rescue
  [80, 25]
end

SCREEN_W, SCREEN_H = detect_screen_size

def content_col
  @content_col ||= SCREEN_W >= 100 ? 3 : 2
end

def content_w
  @content_w ||= [SCREEN_W - (content_col * 2) + 1, 20].max
end

def title_col
  content_col
end

def title_text_w
  @title_text_w ||= [content_w / 3, 10].max
end

def bullet_wrap_w
  [body_text_w - 2, 10].max
end

def code_inner_w
  [content_w - 4, 8].max
end

def subtitle_text_w
  [content_w / 2, 16].max
end

def body_text_w
  @body_text_w ||= [content_w / 2, 16].max
end

def body_compact_w
  [content_w - 2, 20].max
end

def title_row
  3
end

def subtitle_row
  7
end

def body_start_row(has_subtitle)
  has_subtitle ? 10 : 8
end

def body_last_row
  SCREEN_H - 5
end

def note_row
  SCREEN_H - 3
end

Slide = Struct.new(:title, :subtitle, :bullets, :code, :notes, :layout, keyword_init: true)

def cls
  $stdout.syswrite("\x1b[2J\x1b[H")
end

def at(row, col)
  $stdout.syswrite("\x1b[#{row};#{col}H")
end

def color(code)
  $stdout.syswrite("\x1b[#{code}m")
end

def title_font(mode)
  $stdout.syswrite("\x1b[#{mode}z")
end

def fit(text, width)
  s = text.to_s
  s.length > width ? s[0, width] : s.ljust(width)
end

def center(text, width = SCREEN_W)
  s = text.to_s
  pad = width > s.length ? (width - s.length) / 2 : 0
  (' ' * pad) + s[0, width]
end

def center_double(text, width = SCREEN_W / 2)
  s = text.to_s
  pad = width > s.length ? (width - s.length) / 2 : 0
  (' ' * pad) + s[0, width]
end

def wrap_text(text, width)
  s = text.to_s
  lines = []
  line = ''

  s.each_char do |ch|
    if ch == "\n"
      lines << line
      line = ''
      next
    end

    line << ch
    if line.length >= width
      lines << line
      line = ''
    end
  end

  lines << line unless line.empty?
  lines = [''] if lines.empty?
  lines
end

def bullet_font_mode(item)
  item.to_s.length <= bullet_wrap_w ? 1 : 0
end

def bullet_width_for(mode)
  mode == 1 ? bullet_wrap_w : body_compact_w
end

def format_mmss(total_seconds)
  secs = total_seconds.to_i
  secs = 0 if secs < 0
  mins = secs / 60
  rem = secs % 60
  mins.to_s.rjust(2, '0') + ':' + rem.to_s.rjust(2, '0')
end

def read_key
  c = $stdin.sysread(1)
  return :enter if c == "\n" || c == "\r"
  return :space if c == ' '
  return :back if c == "\x7f" || c == "\b"
  return :quit if c == 'q' || c == 'Q'
  return :home if c == 'g' || c == 'G'
  return :end if c == 'e' || c == 'E'

  if c == "\x1b"
    c2 = $stdin.sysread(1) rescue nil
    return :escape unless c2 == '['
    c3 = $stdin.sysread(1) rescue nil
    return :left if c3 == 'D'
    return :right if c3 == 'C'
    return :up if c3 == 'A'
    return :down if c3 == 'B'
    return :page_up if c3 == '5' && ($stdin.sysread(1) rescue nil) == '~'
    return :page_down if c3 == '6' && ($stdin.sysread(1) rescue nil) == '~'
    return :escape
  end

  c
end

def poll_key(timeout)
  ready = IO.select([$stdin], nil, nil, timeout)
  return nil unless ready
  read_key
rescue
  nil
end

def code_excerpt(path, start_line: 1, lines: 8)
  src = File.read(path).split("\n")
  block = src[(start_line - 1), lines] || []
  block.map.with_index do |line, idx|
    num = (start_line + idx).to_s.rjust(3, ' ')
    "#{num}: #{line}"
  end
end

def parse_code_directive(line)
  m = line.match(/\A\{\{code:(.+?):(\d+):(\d+)\}\}\z/)
  return nil unless m
  code_excerpt(m[1], start_line: m[2].to_i, lines: m[3].to_i)
end

def parse_markdown(text)
  slides = []

  text.split(/^---+\s*$/).each do |chunk|
    lines = chunk.lines.map(&:chomp)
    next if lines.all? { |line| line.strip.empty? }

    slide = Slide.new(title: nil, subtitle: nil, bullets: [], code: [], notes: nil, layout: 'default')
    in_code = false
    code_lines = []

    lines.each do |line|
      stripped = line.strip
      next if stripped.empty? && !in_code

      if in_code
        if stripped.start_with?('```')
          in_code = false
          slide.code.concat(code_lines)
          code_lines = []
        else
          code_lines << line
        end
        next
      end

      if stripped.start_with?('```')
        in_code = true
        code_lines = []
      elsif stripped.start_with?('layout:')
        slide.layout = stripped.split(':', 2)[1].to_s.strip
      elsif stripped.start_with?('# ')
        slide.title = stripped[2..]
      elsif stripped.start_with?('## ')
        slide.subtitle = stripped[3..]
      elsif stripped.start_with?('- ')
        slide.bullets << stripped[2..]
      elsif stripped.start_with?('> ')
        note = stripped[2..]
        slide.notes = slide.notes ? "#{slide.notes}  #{note}" : note
      elsif (code = parse_code_directive(stripped))
        slide.code.concat(code)
      end
    end

    slide.code = nil if slide.code.empty?
    slide.bullets = nil if slide.bullets.empty?
    slides << slide if slide.title
  end

  slides
end

def fallback_slides
  [
    Slide.new(
      title: 'Ruby on Bare Metal',
      subtitle: 'Steam Deck LT Tool',
      bullets: ['slides.md not found'],
      notes: 'Right/Space next  Left/Back prev  q quit',
      layout: 'title'
    )
  ]
end

def load_slides
  data = File.read('/slides.md')
  parsed = parse_markdown(data)
  parsed.empty? ? fallback_slides : parsed
rescue
  fallback_slides
end

def render_header(slide_index, total, remaining_seconds)
  rem = total - slide_index - 1
  timer = format_mmss(remaining_seconds)
  line = "[Ruby Deck]  #{slide_index + 1}/#{total}  left:#{rem}  timer:#{timer}"
  color('36')
  at(1, 1)
  $stdout.syswrite(fit(line, SCREEN_W))
  color('0')
end

def render_big_title(row, text)
  color('1;36')
  at(row, title_col)
  title_font(2)
  $stdout.syswrite(fit(text, title_text_w))
  title_font(0)
  color('0')
end

def render_title(title, subtitle = nil)
  render_big_title(title_row, title)
  return unless subtitle
  color('1;37')
  at(subtitle_row, title_col)
  title_font(1)
  $stdout.syswrite(fit(subtitle, subtitle_text_w))
  title_font(0)
  color('0')
end

def render_title_slide(slide, index, total, remaining_seconds)
  render_header(index, total, remaining_seconds)
  render_big_title(6, slide.title)
  if slide.subtitle
    color('1;37')
    at(11, title_col)
    title_font(1)
    $stdout.syswrite(fit(slide.subtitle, subtitle_text_w))
    title_font(0)
    color('0')
  end
  if slide.notes
    render_footer(slide.notes)
  end
end

def render_bullets(items, start_row)
  row = start_row
  items.each do |item|
    mode = bullet_font_mode(item)
    width = bullet_width_for(mode)
    wrap_text(item, width).each_with_index do |line, idx|
      break if row > body_last_row
      color('37')
      at(row, content_col)
      prefix = idx == 0 ? '- ' : '  '
      title_font(mode)
      $stdout.syswrite(fit(prefix + line, width))
      title_font(0)
      color('0')
      row += 1
    end
    row += 1
  end
  row
end

def render_code(lines, start_row)
  return start_row unless lines && !lines.empty?
  width = content_w
  left = content_col
  color('36')
  at(start_row, left)
  $stdout.syswrite(fit('+' + ('-' * (width - 2)) + '+', width))
  color('0')
  row = start_row + 1
  lines.each do |line|
    break if row > body_last_row
    color('36')
    at(row, left)
    $stdout.syswrite('| ' + fit(line, code_inner_w) + ' |')
    color('0')
    row += 1
  end
  if row <= SCREEN_H - 4
    color('36')
    at(row, left)
    $stdout.syswrite(fit('+' + ('-' * (width - 2)) + '+', width))
    color('0')
    row += 1
  end
  row
end

def render_footer(text)
  return unless text
  color('36')
  at(SCREEN_H - 1, content_col)
  mode = text.to_s.length <= body_text_w ? 1 : 0
  width = mode == 1 ? body_text_w : content_w
  title_font(mode)
  $stdout.syswrite(fit(text, width))
  title_font(0)
  color('0')
end

def render_slide(slide, index, total, remaining_seconds)
  cls
  if slide.layout == 'title'
    render_title_slide(slide, index, total, remaining_seconds)
    return
  end

  render_header(index, total, remaining_seconds)
  render_title(slide.title, slide.subtitle)

  row = body_start_row(!slide.subtitle.nil?)
  row = render_bullets(slide.bullets || [], row)
  render_code(slide.code, row + 1)
  render_footer(slide.notes || 'Right/Space next  Left/Back prev')
end

SLIDES = load_slides

start_time = Time.now
index = 0
last_index = nil
last_timer = nil

loop do
  remaining = TALK_SECONDS - (Time.now - start_time)
  timer_text = format_mmss(remaining)

  if index != last_index
    render_slide(SLIDES[index], index, SLIDES.length, remaining)
    last_index = index
    last_timer = timer_text
  elsif timer_text != last_timer
    render_header(index, SLIDES.length, remaining)
    last_timer = timer_text
  end

  key = poll_key(REFRESH_INTERVAL)
  next unless key

  case key
  when :right, :down, :space, :enter, :page_down, 'n', 'N'
    index += 1 if index + 1 < SLIDES.length
  when :left, :up, :back, :page_up, 'p', 'P'
    index -= 1 if index > 0
  when :home
    index = 0
  when :end
    index = SLIDES.length - 1
  when :quit, :escape
    break
  end
end

cls
color('0')
title_font(0)
at(12, 1)
$stdout.syswrite(center('slides closed'))
at(14, 1)
$stdout.syswrite(center('power off or reboot to leave this screen'))
