# Ruby on Bare Metal - autonomous dungeon-wandering demo
# Inspired by Termfront's raycasting renderer (simplified, no input/enemies)

MAP = [
  "##############",
  "#............#",
  "#...###......#",
  "#...#........#",
  "#...#..####..#",
  "#...#..#.....#",
  "#......#.....#",
  "#.######.....#",
  "#............#",
  "##############",
]
MAP_H = MAP.size
MAP_W = MAP[0].size

VIEW_W = 78
VIEW_H = 22
FOV = 60.0 * Math::PI / 180.0
HALF_FOV_TAN = Math.tan(FOV / 2.0)

def wall?(x, y)
  ix = x.floor
  iy = y.floor
  return true if iy < 0 || iy >= MAP_H || ix < 0 || ix >= MAP_W
  MAP[iy][ix] == "#"
end

$px = 2.5
$py = 2.5
$pa = 0.3

def cast_ray(ox, oy, rdx, rdy)
  mx = ox.floor
  my = oy.floor
  ddx = rdx.abs < 1e-9 ? 1e30 : (1.0 / rdx).abs
  ddy = rdy.abs < 1e-9 ? 1e30 : (1.0 / rdy).abs

  if rdx < 0
    step_x = -1
    sd_x = (ox - mx) * ddx
  else
    step_x = 1
    sd_x = (mx + 1.0 - ox) * ddx
  end
  if rdy < 0
    step_y = -1
    sd_y = (oy - my) * ddy
  else
    step_y = 1
    sd_y = (my + 1.0 - oy) * ddy
  end

  side = 0
  64.times do
    if sd_x < sd_y
      sd_x += ddx
      mx += step_x
      side = 0
    else
      sd_y += ddy
      my += step_y
      side = 1
    end
    return [1e30, 0] if my < 0 || my >= MAP_H || mx < 0 || mx >= MAP_W
    next unless MAP[my][mx] == "#"
    d = side == 0 ? (mx - ox + (1 - step_x) / 2.0) / rdx
                  : (my - oy + (1 - step_y) / 2.0) / rdy
    return [d.abs, side]
  end
  [1e30, 0]
end

def shade_char(d, side)
  # side=0 (EW) darker, side=1 (NS) lighter, for depth cue
  if d < 1.5
    side == 0 ? "#" : "%"
  elsif d < 3.0
    side == 0 ? "=" : "+"
  elsif d < 5.0
    side == 0 ? "-" : "~"
  else
    side == 0 ? ":" : "."
  end
end

def render_frame(frame)
  dx = Math.cos($pa)
  dy = Math.sin($pa)
  plane_x = -dy * HALF_FOV_TAN
  plane_y = dx * HALF_FOV_TAN

  dists = Array.new(VIEW_W)
  sides = Array.new(VIEW_W)
  VIEW_W.times do |c|
    cam = 2.0 * c / VIEW_W - 1.0
    d, s = cast_ray($px, $py, dx + plane_x * cam, dy + plane_y * cam)
    dists[c] = d
    sides[c] = s
  end

  buf = String.new(capacity: (VIEW_W + 4) * (VIEW_H + 2))
  buf << "\e[H"

  vmid = VIEW_H / 2.0
  VIEW_H.times do |r|
    VIEW_W.times do |c|
      d = dists[c]
      lh = d > 0.1 ? (VIEW_H / d) : VIEW_H.to_f
      top = (vmid - lh / 2.0).to_i
      bot = (vmid + lh / 2.0).to_i
      if r < top
        buf << " "       # ceiling
      elsif r >= bot
        buf << " "       # floor
      else
        buf << shade_char(d, sides[c])
      end
    end
    buf << "\n"
  end

  hud = "RubyOS dungeon demo  frame=#{frame}  pos=(#{'%.1f' % $px},#{'%.1f' % $py})  heading=#{'%.2f' % $pa}"
  buf << hud.ljust(VIEW_W)[0, VIEW_W]
  buf << "\n"
  print buf
end

def step_ai(frame)
  speed = 0.12
  nx = $px + Math.cos($pa) * speed
  ny = $py + Math.sin($pa) * speed
  r = 0.25
  blocked = wall?(nx + r, ny + r) || wall?(nx - r, ny - r) ||
            wall?(nx + r, ny - r) || wall?(nx - r, ny + r)
  if blocked
    # Turn to find an open direction
    $pa += (frame.odd? ? 1 : -1) * Math::PI / 2.5
  else
    $px = nx
    $py = ny
  end
  # Gentle drift so it looks like it is exploring, not marching
  $pa += 0.05 if frame % 30 == 0
  # Normalize angle to [0, 2π)
  twopi = Math::PI * 2
  $pa -= twopi while $pa >= twopi
  $pa += twopi while $pa < 0
end

def busy_sleep(sec)
  start = Time.now
  loop { break if Time.now - start >= sec }
end

print "\e[2J\e[H"
frame = 0
loop do
  render_frame(frame)
  step_ai(frame)
  frame += 1
  busy_sleep(0.08)
end
