# frozen_string_literal: true

module Mui
  module TerminalAdapter
    # Ruby on Bare Metal terminal adapter - uses ANSI escape sequences over serial console
    class RubyOnBareMetal < Base
      ESC = "\x1b"

      def init
        @width = 80
        @height = 25
        @cur_y = 0
        @cur_x = 0
        @buf = +""
        @has_colors = true
        @color_pairs = {}
        # Enter alternate screen
        raw_write("#{ESC}[?1049h#{ESC}[2J#{ESC}[H")
        init_colors
      end

      def close
        raw_write("#{ESC}[0m#{ESC}[?25h#{ESC}[?1049l")
      end

      def clear
        @buf = +""
        @buf << "#{ESC}[H"
      end

      def refresh
        if @buf.length > 0
          raw_write("#{ESC}[?25l")
          raw_write(@buf)
          @buf = +""
        end
        raw_write("#{ESC}[#{@cur_y + 1};#{@cur_x + 1}H#{ESC}[?25h")
      end

      def width;  @width;  end
      def height; @height; end

      def setpos(y, x)
        @cur_y = y
        @cur_x = x
        @buf << "#{ESC}[#{y + 1};#{x + 1}H"
      end

      def addstr(str)
        @buf << str
        @cur_x += str.length
      end

      def with_highlight
        @buf << "#{ESC}[7m"
        result = yield
        @buf << "#{ESC}[27m"
        result
      end

      def init_colors
        @has_colors = true
        @available_colors = 256
        @max_pairs = 256
      end

      def has_colors?; @has_colors; end
      def colors; @available_colors || 0; end
      def color_pairs; @max_pairs || 0; end

      def init_color_pair(pair_index, fg, bg)
        @color_pairs[pair_index] = [resolve_color(fg), resolve_color(bg)]
      end

      def with_color(pair_index, bold: false, underline: false)
        pair = @color_pairs[pair_index]
        if pair
          fg, bg = pair
          @buf << "#{ESC}[0m"
          @buf << "#{ESC}[38;5;#{fg}m" if fg && fg >= 0
          @buf << "#{ESC}[48;5;#{bg}m" if bg && bg >= 0
        end
        @buf << "#{ESC}[1m" if bold
        @buf << "#{ESC}[4m" if underline
        result = yield
        @buf << "#{ESC}[0m"
        result
      end

      def getch
        byte = read_byte
        return nil unless byte
        handle_input_byte(byte)
      end

      def getch_nonblock
        getch
      end

      def suspend
        raw_write("#{ESC}[?1049l")
      end

      def resume
        raw_write("#{ESC}[?1049h")
        touchwin
      end

      def touchwin; end

      private

      def resolve_color(color)
        return -1 if color.nil?
        return color if color.is_a?(Integer)
        return @color_resolver.resolve(color) if @color_resolver
        -1
      end

      def raw_write(str)
        $stdout.syswrite(str)
      end

      def read_byte
        c = $stdin.sysread(1)
        c.bytes[0]
      rescue EOFError
        nil
      end

      def handle_input_byte(byte)
        if byte == 0x1b  # ESC
          # Arrow keys and other escape sequences send ESC [ X quickly.
          # Always try to read the next byte (blocking) — on a serial
          # terminal the [ arrives immediately after ESC for sequences.
          # For bare ESC (vim normal mode), the user's ESC is followed
          # by a pause, but sysread will block until the next key anyway.
          # To distinguish: read next byte; if it's '[', it's a CSI seq.
          # If it's something else, return ESC then process that byte next.
          b2 = read_byte
          return byte unless b2   # EOF after ESC

          if b2 == 0x5b  # ESC [  → CSI sequence
            return read_csi_sequence
          elsif b2 == 0x4f  # ESC O  → SS3 sequence (some terminals)
            return read_ss3_sequence
          else
            # ESC followed by non-sequence char (e.g. ESC then a letter)
            # Push back b2 by handling it in the next call.
            # For now, store it and return ESC.
            @pending_byte = b2
            return byte
          end
        end

        # Check for pending byte from ESC handling
        if @pending_byte
          pb = @pending_byte
          @pending_byte = nil
          # The current byte was already consumed, but we need to
          # return the pending one first. Actually, this path shouldn't
          # happen since we return ESC above. Let's just handle normally.
        end

        # UTF-8 multibyte
        if byte >= 0x80
          return read_utf8_char(byte)
        end

        # Printable ASCII → String (curses gem convention)
        if byte >= 0x20 && byte <= 0x7e
          return byte.chr
        end

        # Control characters as Integer
        byte
      end

      def read_csi_sequence
        params = +""
        while true
          b = read_byte
          break unless b
          params << b.chr
          # CSI params are digits and ';', final byte is 0x40-0x7E
          if b >= 0x40 && b <= 0x7E
            break
          end
        end

        case params
        when "A"  then 259   # KEY_UP
        when "B"  then 258   # KEY_DOWN
        when "C"  then 261   # KEY_RIGHT
        when "D"  then 260   # KEY_LEFT
        when "H"  then 262   # KEY_HOME
        when "F"  then 360   # KEY_END
        when "Z"  then 353   # Shift+TAB (KEY_BTAB)
        when "1~" then 262   # KEY_HOME (alt)
        when "2~" then 331   # KEY_IC (insert)
        when "3~" then 330   # KEY_DC (delete)
        when "4~" then 360   # KEY_END (alt)
        when "5~" then 339   # KEY_PPAGE
        when "6~" then 338   # KEY_NPAGE
        when "1;5C" then 561 # Ctrl+Right (word forward)
        when "1;5D" then 546 # Ctrl+Left (word backward)
        when "1;2C" then 402 # Shift+Right
        when "1;2D" then 393 # Shift+Left
        else
          0x1b  # Unknown, return ESC
        end
      end

      def read_ss3_sequence
        b = read_byte
        return 0x1b unless b

        case b
        when 0x41 then 259   # KEY_UP
        when 0x42 then 258   # KEY_DOWN
        when 0x43 then 261   # KEY_RIGHT
        when 0x44 then 260   # KEY_LEFT
        when 0x48 then 262   # KEY_HOME
        when 0x46 then 360   # KEY_END
        else
          0x1b
        end
      end

      def read_utf8_char(first_byte)
        bytes = [first_byte]
        expected = if (first_byte & 0xE0) == 0xC0 then 2
                   elsif (first_byte & 0xF0) == 0xE0 then 3
                   elsif (first_byte & 0xF8) == 0xF0 then 4
                   else return first_byte
                   end

        (expected - 1).times do
          b = read_byte
          break unless b && (b & 0xC0) == 0x80
          bytes << b
        end

        bytes.pack("C*").force_encoding(Encoding::UTF_8)
      end
    end
  end
end
