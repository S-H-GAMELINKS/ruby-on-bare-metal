STDOUT.sync = true
STDERR.sync = true

# ==== VFS-based require system ====
$LOADED_FEATURES_VFS = {}

# Override require/require_relative for VFS
module Kernel
  alias_method :original_require, :require
  alias_method :original_require_relative, :require_relative

# ==== Encoding constants (not auto-created without encdb) ====
Encoding.list.each do |enc|
  name = enc.name.gsub('-', '_').gsub('.', '_')
  begin
    Encoding.const_set(name, enc) unless Encoding.const_defined?(name, false)
  rescue
  end
end

  # Known stubs for unavailable modules
  STUB_MODULES = %w[curses bundler/inline bundler open3 tempfile clipboard].freeze

  def require(name)
    return true if STUB_MODULES.include?(name)

    # Try VFS paths, then embedded filesystem
    paths = [
      "/lib/#{name}.rb",
      "/lib/#{name}",
      name.end_with?('.rb') ? name : "#{name}.rb",
    ]
    paths.each do |path|
      next if $LOADED_FEATURES_VFS[path]
      # Try VFS (in-memory) first
      data = VFS.read(path) rescue nil
      # Then try File.read (accesses C embedded files via syscall)
      data ||= (File.read(path) rescue nil)
      if data
        $LOADED_FEATURES_VFS[path] = true
        eval(data, TOPLEVEL_BINDING, path, 1)
        return true
      end
    end
    # Fall back to original require
    begin
      original_require(name)
    rescue LoadError
      false
    end
  end

  def require_relative(name)
    # Determine caller's directory
    caller_file = caller_locations(1, 1)[0]&.path || '(eval)'
    dir = File.dirname(caller_file)
    full = "#{dir}/#{name}"
    full = full + '.rb' unless full.end_with?('.rb')

    # Normalize path (resolve ..)
    parts = full.split('/')
    normalized = []
    parts.each { |p|
      if p == '..'
        normalized.pop
      elsif p != '.' && p != ''
        normalized.push(p)
      end
    }
    full = '/' + normalized.join('/')

    return true if $LOADED_FEATURES_VFS[full]
    data = VFS.read(full) rescue nil
    data ||= (File.read(full) rescue nil)
    if data
      $LOADED_FEATURES_VFS[full] = true
      eval(data, TOPLEVEL_BINDING, full, 1)
      return true
    end
    original_require_relative(name)
  end
end

# ==== File class VFS integration ====
class << File
  alias_method :original_exist?, :exist?
  alias_method :original_read, :read
  alias_method :original_write, :write
  alias_method :original_readlines, :readlines

  def exist?(path)
    return true if VFS.exist?(path)
    begin
      original_exist?(path)
    rescue
      false
    end
  end

  def read(path, *args)
    data = VFS.read(path)
    return data if data
    original_read(path, *args)
  end

  def write(path, data, *args)
    VFS.write(path, data.to_s)
    data.to_s.length
  end

  def readlines(path, *args)
    data = VFS.read(path)
    return data.split("\n").map { |l| l + "\n" } if data
    original_readlines(path, *args)
  end

  def directory?(path)
    VFS.list(path).length > 0 rescue false
  end

  def expand_path(path, base = '/')
    return path if path.start_with?('/')
    "#{base}/#{path}".gsub('//', '/')
  end

  def dirname(path)
    parts = path.split('/')
    parts.length > 1 ? parts[0..-2].join('/') : '/'
  end

  def basename(path, ext = nil)
    name = path.split('/').last || path
    name = name.sub(/#{Regexp.escape(ext)}$/, '') if ext
    name
  end
end

# Stub IO.select for Ruby on Bare Metal serial
module IOSelectPatch
  def self.select(read_arr, write_arr = nil, err_arr = nil, timeout = nil)
    # Simple implementation for serial console
    if read_arr&.include?($stdin)
      if timeout == 0
        # Non-blocking: check if data is available
        # On Ruby on Bare Metal, serial_data_ready is checked via syscall
        return [[$stdin], [], []]
      else
        # Blocking: always return readable
        return [[$stdin], [], []]
      end
    end
    [read_arr || [], write_arr || [], err_arr || []]
  end
end

# Stub modules that mui might try to use
module Open3
  def self.capture3(*args)
    ['', '', nil]
  end
end

# Curses key code constants (used by mui key handlers)
module Curses
  KEY_UP        = 259
  KEY_DOWN      = 258
  KEY_LEFT      = 260
  KEY_RIGHT     = 261
  KEY_HOME      = 262
  KEY_END       = 360
  KEY_DC        = 330
  KEY_BACKSPACE = 263
  KEY_ENTER     = 10
  KEY_NPAGE     = 338
  KEY_PPAGE     = 339
  KEY_IC        = 331
  KEY_BTAB      = 353
  KEY_RESIZE    = 410
  A_BOLD        = 0x200000
  A_UNDERLINE   = 0x20000
  A_REVERSE     = 0x40000
end

# Tempfile stub
class Tempfile
  def initialize(name)
    @path = "/tmp/_tempfile_#{name}"
    @data = +""
  end
  def path; @path; end
  def write(s); @data << s; end
  def close; end
  def unlink; end
  def rewind; end
  def read; @data; end
end

# ==== Utility: line reader (raw mode, avoids Primitive-based IO) ====
def read_line(prompt = '')
  $stdout.syswrite(prompt)
  buf = ''
  cursor = 0
  saved_input = ''
  history_index = $history.length

  redraw = -> {
    $stdout.syswrite("\r\x1b[K" + prompt + buf)
    # Move cursor back if not at end
    if cursor < buf.length
      $stdout.syswrite("\x1b[#{buf.length - cursor}D")
    end
  }

  while true
    c = $stdin.sysread(1)
    if c == "\n" || c == "\r"
      $stdout.syswrite("\n")
      return buf

    elsif c == "\x1b" # ESC - start of escape sequence
      c2 = $stdin.sysread(1)
      if c2 == '['
        c3 = $stdin.sysread(1)
        if c3 == 'A' # Up arrow
          if $history.length > 0 && history_index > 0
            saved_input = buf if history_index == $history.length
            history_index -= 1
            buf = $history[history_index].dup
            cursor = buf.length
            redraw.call
          end
        elsif c3 == 'B' # Down arrow
          if history_index < $history.length
            history_index += 1
            if history_index == $history.length
              buf = saved_input.dup
            else
              buf = $history[history_index].dup
            end
            cursor = buf.length
            redraw.call
          end
        elsif c3 == 'C' # Right arrow
          if cursor < buf.length
            cursor += 1
            $stdout.syswrite("\x1b[C")
          end
        elsif c3 == 'D' # Left arrow
          if cursor > 0
            cursor -= 1
            $stdout.syswrite("\x1b[D")
          end
        else # Unknown CSI sequence
          while c3 && c3.ord >= 0x20 && c3.ord <= 0x3F
            c3 = $stdin.sysread(1)
          end
        end
      end

    elsif c == "\x7f" || c == "\b"
      if cursor > 0
        buf = buf[0...cursor - 1] + buf[cursor..-1]
        cursor -= 1
        redraw.call
      end
    elsif c == "\x03" # Ctrl-C
      $stdout.syswrite("^C\n")
      return ''
    elsif c == "\x04" # Ctrl-D
      raise EOFError
    elsif c.ord >= 32
      buf = buf[0...cursor] + c + buf[cursor..-1]
      cursor += 1
      if cursor == buf.length
        $stdout.syswrite(c)
      else
        redraw.call
      end
    end
  end
end

# ==== Writable Virtual File System ====
$vfs = {}

module VFS
  def self.write(path, content)
    $vfs[path] = content.to_s
  end

  def self.read(path)
    $vfs[path]
  end

  def self.delete(path)
    $vfs.delete(path)
  end

  def self.exist?(path)
    $vfs.key?(path)
  end

  def self.list(dir)
    prefix = dir == '/' ? '/' : dir + '/'
    $vfs.keys.select { |k| k.start_with?(prefix) }.map { |k|
      rest = k[prefix.length..]
      rest.include?('/') ? rest.split('/')[0] + '/' : rest
    }.uniq.sort
  end

  def self.glob(pattern)
    re_str = pattern.gsub('.', '\\.').gsub('*', '.*').gsub('?', '.')
    re = Regexp.new('^' + re_str + '$')
    $vfs.keys.select { |k| k.match?(re) }.sort
  end

  def self.size(path)
    d = $vfs[path]
    d ? d.length : nil
  end
end

# ==== /proc: dynamic virtual files ====
def update_proc_files
  t = (Process.clock_gettime(Process::CLOCK_MONOTONIC) rescue 0).to_i
  VFS.write('/proc/version', "Ruby on Bare Metal v0.1\nRuby #{RUBY_VERSION} [#{RUBY_PLATFORM}]\n")
  VFS.write('/proc/uptime', "#{t / 60}m#{t % 60}s (#{t}s total)\n")

  objs = ObjectSpace.count_objects rescue {}
  total = objs[:TOTAL] || 0
  free = objs[:FREE] || 0
  VFS.write('/proc/meminfo',
    "objects_total: #{total}\n" \
    "objects_free:  #{free}\n" \
    "objects_live:  #{total - free}\n" \
    "vfs_files:     #{$vfs.keys.length}\n" \
    "vfs_bytes:     #{$vfs.values.map { |v| v.length }.sum}\n")
end

# ==== History ====
$history = []

# ==== Improved exception display ====
def format_exception(e)
  # Extremely defensive: avoid any Primitive-based methods
  parts = []
  begin
    parts.push(e.class.to_s)
  rescue
    parts.push('Exception')
  end
  # e.message may use Primitive internally, so wrap carefully
  begin
    m = e.message
    parts[0] = parts[0] + ': ' + m.to_s if m
  rescue
    # message not available
  end
  result = [parts[0]]
  begin
    bt = e.backtrace
    if bt.is_a?(Array)
      i = 0
      while i < bt.length && i < 5
        result.push('  from ' + bt[i].to_s)
        i = i + 1
      end
      result.push("  ... #{bt.length - 5} more") if bt.length > 5
    end
  rescue
    # backtrace not available
  end
  result
end

# ==== Commands ====
$commands = {}

$commands['help'] = -> {
  puts "Ruby on Bare Metal Shell Commands:"
  puts "  help          - show this help"
  puts "  ls [dir]      - list files"
  puts "  cat <file>    - show file contents"
  puts "  write <file>  - write to file (reads until '.' on a line)"
  puts "  rm <file>     - delete file"
  puts "  run <file>    - execute Ruby script"
  puts "  mui [file]    - vim-like editor"
  puts "  mem           - memory info"
  puts "  uptime        - system uptime"
  puts "  version       - version info"
  puts "  history       - command history"
  puts "  clear         - clear screen"
  puts "  exit          - halt system"
  puts ""
  puts "Or type any Ruby expression to evaluate it."
}

$commands['version'] = -> {
  puts "Ruby on Bare Metal v0.1"
  puts "Ruby #{RUBY_VERSION} [#{RUBY_PLATFORM}]"
}

$commands['uptime'] = -> {
  update_proc_files
  $stdout.syswrite(VFS.read('/proc/uptime'))
}

$commands['mem'] = -> {
  update_proc_files
  $stdout.syswrite(VFS.read('/proc/meminfo'))
}

$commands['clear'] = -> {
  $stdout.syswrite("\x1b[2J\x1b[H")
}

$commands['history'] = -> {
  $history.each_with_index { |h, i| puts "  #{i + 1}: #{h}" }
}

$commands['ls'] = ->(dir = '/') {
  entries = VFS.list(dir)
  if entries.empty?
    puts "(empty)"
  else
    entries.each { |e| puts "  #{e}" }
  end
}

$commands['cat'] = ->(path = nil) {
  unless path
    puts "usage: cat <path>"
    return
  end
  data = VFS.read(path)
  if data
    $stdout.syswrite(data)
    $stdout.syswrite("\n") unless data.end_with?("\n")
  else
    puts "cat: #{path}: not found"
  end
}

$commands['write'] = ->(path = nil) {
  unless path
    puts "usage: write <path>"
    return
  end
  puts "Enter text (type '.' on a line by itself to finish):"
  lines = []
  while true
    $stdout.syswrite('  ')
    l = read_line
    break if l == '.'
    lines.push(l)
  end
  VFS.write(path, lines.join("\n") + "\n")
  puts "wrote #{path} (#{VFS.size(path)} bytes)"
}

$commands['rm'] = ->(path = nil) {
  unless path
    puts "usage: rm <path>"
    return
  end
  if VFS.exist?(path)
    VFS.delete(path)
    puts "removed #{path}"
  else
    puts "rm: #{path}: not found"
  end
}

$commands['mui'] = ->(path = nil) {
  # Try to load mui editor
  begin
    unless defined?(Mui::Editor)
      require 'mui'
      require 'mui/terminal_adapter/ruby_on_bare_metal'
    end
    adapter = Mui::TerminalAdapter::RubyOnBareMetal.new
    editor = Mui::Editor.new(path, adapter: adapter, load_config: false)
    editor.run
  rescue Exception => e
    puts "mui editor failed: #{e.class}: #{e.message rescue '?'}"
    puts "Falling back to line editor..."
    # Fallback: simple line editor
    existing = VFS.read(path)
    if existing
      puts "Current content of #{path}:"
      existing.split("\n").each_with_index { |l, i| puts "  #{i + 1}: #{l}" }
    end
    puts "Enter new content (type '.' to finish):"
    lines = []
    while true
      $stdout.syswrite('  ')
      l = read_line
      break if l == '.'
      lines.push(l)
    end
    VFS.write(path, lines.join("\n") + "\n")
    puts "saved #{path} (#{VFS.size(path)} bytes)"
  end
}

$commands['run'] = ->(path = nil) {
  unless path
    puts "usage: run <path>"
    return
  end
  data = VFS.read(path)
  if data
    eval(data, TOPLEVEL_BINDING, path)
  else
    puts "run: #{path}: not found"
  end
}

# ==== Register built-in files ====
VFS.write('/etc/motd', "Welcome to Ruby on Bare Metal!\nA tiny system where everything is Ruby.\n")
VFS.write('/bin/hello.rb', "puts 'Hello from Ruby on Bare Metal!'\n")
VFS.write('/bin/calc.rb',
  "puts 'Calculator (type expression, empty line to quit)'\n" \
  "while true\n" \
  "  $stdout.syswrite('calc> ')\n" \
  "  l = read_line\n" \
  "  break if l == ''\n" \
  "  begin\n" \
  "    puts '= ' + eval(l).to_s\n" \
  "  rescue Exception\n" \
  "    puts 'error'\n" \
  "  end\n" \
  "end\n")
VFS.write('/bin/sysinfo.rb',
  "update_proc_files\n" \
  "puts 'System Information'\n" \
  "puts '=' * 30\n" \
  "['/proc/version', '/proc/uptime', '/proc/meminfo'].each { |f|\n" \
  "  puts f + ':'\n" \
  "  $stdout.syswrite(VFS.read(f))\n" \
  "}\n")
VFS.write('/bin/files.rb',
  "puts 'All files in VFS:'\n" \
  "$vfs.each { |path, data|\n" \
  "  puts '  ' + path + ' (' + data.length.to_s + ' bytes)'\n" \
  "}\n")
VFS.write('/bin/ruby_test.rb',
  "puts 'Ruby Feature Test'\n" \
  "puts '=' * 30\n" \
  "tests = [\n" \
  "  ['Integer arithmetic', -> { (1 + 2 * 3) == 7 }],\n" \
  "  ['String methods', -> { 'hello'.upcase == 'HELLO' }],\n" \
  "  ['Array operations', -> { [3,1,2].sort == [1,2,3] }],\n" \
  "  ['Hash access', -> { {a: 1}[:a] == 1 }],\n" \
  "  ['Block/each', -> { r = []; [1,2].each{|x| r.push(x*2)}; r == [2,4] }],\n" \
  "  ['Method def', -> { eval('def _t; 42; end; _t == 42') }],\n" \
  "  ['Exception', -> { begin; 1/0; rescue ZeroDivisionError; true; end }],\n" \
  "  ['Regexp', -> { 'hello' =~ /ell/ }],\n" \
  "  ['Range', -> { (1..5).to_a == [1,2,3,4,5] }],\n" \
  "  ['Symbol', -> { :foo.to_s == 'foo' }],\n" \
  "]\n" \
  "pass = 0\n" \
  "fail = 0\n" \
  "tests.each { |name, test|\n" \
  "  begin\n" \
  "    if test.call\n" \
  "      puts '  PASS: ' + name\n" \
  "      pass = pass + 1\n" \
  "    else\n" \
  "      puts '  FAIL: ' + name\n" \
  "      fail = fail + 1\n" \
  "    end\n" \
  "  rescue Exception\n" \
  "    puts '  ERROR: ' + name\n" \
  "    fail = fail + 1\n" \
  "  end\n" \
  "}\n" \
  "puts ''\n" \
  "puts pass.to_s + ' passed, ' + fail.to_s + ' failed'\n")

update_proc_files

# ==== Boot message ====
$stdout.syswrite(VFS.read('/etc/motd'))
puts "Ruby on Bare Metal v0.1 - Ruby #{RUBY_VERSION} [#{RUBY_PLATFORM}]"
puts "Type 'help' for commands, or enter Ruby expressions."
puts ""

# ==== Main shell loop ====
running = true
while running
  begin
    line = read_line('ruby-on-bare-metal> ')
  rescue EOFError
    running = false
    next
  end

  next if line == ''
  $history.push(line)

  if line == 'exit' || line == 'halt' || line == 'shutdown'
    running = false
    next
  end

  # Parse command
  parts = line.split(' ', 2)
  cmd = parts[0]
  arg = parts[1]

  if $commands.key?(cmd)
    begin
      if arg
        $commands[cmd].call(arg)
      else
        $commands[cmd].call
      end
    rescue Exception => e
      begin
        lines = format_exception(e)
        lines.each { |l| $stdout.syswrite(l + "\n") }
      rescue
        $stdout.syswrite("(error displaying exception)\n")
      end
    end
  else
    # Evaluate as Ruby expression
    begin
      result = eval(line, TOPLEVEL_BINDING, '(eval)', 1)
      $stdout.syswrite("=> ")
      $stdout.syswrite(result.inspect)
      $stdout.syswrite("\n")
    rescue SyntaxError
      $stdout.syswrite("SyntaxError\n")
    rescue Exception => e
      begin
        lines = format_exception(e)
        lines.each { |l| $stdout.syswrite(l + "\n") }
      rescue
        $stdout.syswrite("(error)\n")
      end
    end
  end
end

puts "\nhalting."
