#!/usr/bin/env python3
"""
Generate a simple 32x32 ICO icon for RemoteTool / Agent.

Design: a teal (#1d9e75) rounded square with a white reverse-tunnel
arrow: from right (RemoteTool) curving back to left (Agent). The arrow
symbolises the Agent -> RemoteTool reverse connection (spec §4).

Hand-rolled BMP data so we don't need PIL/Pillow. Outputs:
    apps/remote_tool/resources/app.ico
"""
import os
import struct
import zlib

# --- canvas ---------------------------------------------------------------
W, H = 32, 32
TEAL  = (0x1d, 0x9e, 0x75, 0xff)
TEAL_DARK = (0x0f, 0x6e, 0x56, 0xff)
WHITE = (0xff, 0xff, 0xff, 0xff)
TRANS = (0x00, 0x00, 0x00, 0x00)

# Build a per-pixel map. Default: rounded-square teal.
def make_pixels():
    pixels = [[TRANS] * W for _ in range(H)]
    radius = 4
    for y in range(H):
        for x in range(W):
            # Rounded corners
            in_corner = False
            for cx, cy in [(radius, radius), (W-1-radius, radius),
                           (radius, H-1-radius), (W-1-radius, H-1-radius)]:
                if (x < radius and y < radius and (cx, cy) == (radius, radius)) or \
                   (x >= W-radius and y < radius and (cx, cy) == (W-1-radius, radius)) or \
                   (x < radius and y >= H-radius and (cx, cy) == (radius, H-1-radius)) or \
                   (x >= W-radius and y >= H-radius and (cx, cy) == (W-1-radius, H-1-radius)):
                    dx, dy = x - cx, y - cy
                    if dx*dx + dy*dy > radius*radius:
                        in_corner = True
                        break
            if in_corner:
                continue
            pixels[y][x] = TEAL_DARK if (x == 0 or y == 0 or x == W-1 or y == H-1) else TEAL
    return pixels

pixels = make_pixels()

# --- draw white arrow: a simple reverse-tunnel glyph ---------------------
# The arrow points right (Agent -> RemoteTool is bidirectional; we just
# visualise the tunnel as a "double" arrow).
def fill_rect(x0, y0, x1, y1, color):
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            pixels[y][x] = color

# Horizontal arrow shaft
fill_rect(6, 14, 24, 18, WHITE)
# Arrowhead right
fill_rect(22, 11, 25, 21, WHITE)
# Arrowhead left (mirror)
fill_rect(5, 11, 8, 21, WHITE)

# --- serialize to 32bpp BMP (DIB used inside ICO) ------------------------
# BMP rows are bottom-up. BGRA pixel order.
def to_bgra_bytes():
    row_bytes = W * 4
    out = bytearray()
    for y in reversed(range(H)):
        for x in range(W):
            r, g, b, a = pixels[y][x]
            out += bytes([b, g, r, a])
        # Pad to 4-byte boundary
        while len(out) % 4 != 0:
            out += b'\x00'
    return bytes(out)

# AND mask: 1 bit per pixel, 0 = opaque, 1 = transparent. We use fully
# opaque, so AND mask is all zeros.
and_mask = b'\x00' * (H * ((W + 31) // 32) * 4)

pixel_data = to_bgra_bytes()

# BITMAPINFOHEADER
bih = struct.pack('<IIIHHIIIIII',
    40,             # biSize
    W,              # biWidth
    H * 2,          # biHeight (double for icon: XOR + AND)
    1,              # biPlanes
    32,             # biBitCount
    0,              # biCompression
    len(pixel_data) + len(and_mask),  # biSizeImage
    0, 0, 0, 0)

# ICONDIR + ICONDIRENTRY
image_offset = 6 + 16  # header + 1 entry
image_size = len(bih) + len(pixel_data) + len(and_mask)

icondir = struct.pack('<HHH', 0, 1, 1)
direntry = struct.pack('<BBBBHHII',
    W,              # width (0 = 256)
    H,              # height
    0,              # color count
    0,              # reserved
    1,              # planes
    32,             # bpp
    image_size,
    image_offset)

ico_bytes = icondir + direntry + bih + pixel_data + and_mask

out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        '..', 'apps', 'remote_tool', 'resources', 'app.ico')
os.makedirs(os.path.dirname(out_path), exist_ok=True)
with open(out_path, 'wb') as f:
    f.write(ico_bytes)

# Also copy to agent_windows resources.
agent_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          '..', 'apps', 'agent_windows', 'resources', 'app.ico')
os.makedirs(os.path.dirname(agent_path), exist_ok=True)
with open(agent_path, 'wb') as f:
    f.write(ico_bytes)

print(f"wrote {os.path.relpath(out_path, os.path.dirname(os.path.abspath(__file__)) + '/..')} ({len(ico_bytes)} bytes)")
print(f"wrote {os.path.relpath(agent_path, os.path.dirname(os.path.abspath(__file__)) + '/..')} ({len(ico_bytes)} bytes)")
