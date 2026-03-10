# -*- coding: utf-8 -*-


from PIL import Image
import numpy as np


INPUT_IMAGE_file_name = "starry_night_1" # <== change
H_name = "STARRY_NIGHT_1_H" # <== change

# INPUT_IMAGE_file_name = "SGP_skyline_1" # <== change
# H_name = "SGP_SKYLINE_1_H" # <== change

# INPUT_IMAGE_file_name = "Jason_H_Engineering_Logo_400_300_1" # <== change
# H_name = "JASON_H_LOGO_1_H" # <== change

# INPUT_IMAGE_file_name = "SGP_map_1" # <== change
# H_name = "SGP_MAP_1_H" # <== change


# INPUT_IMAGE_file_name = "output_2bit_dither" # <== change
# H_name = "FAMILY_H" # <== change


INPUT_IMAGE = INPUT_IMAGE_file_name+".jpg"
OUTPUT_BIN = INPUT_IMAGE_file_name+"_image.bin"
OUTPUT_PREVIEW = INPUT_IMAGE_file_name+"_preview.jpg"
OUTPUT_H = INPUT_IMAGE_file_name+"_image.h"

WIDTH = 400
HEIGHT = 300
THRESHOLD = 128

# Load and resize
img = Image.open(INPUT_IMAGE)
img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)
gray = img.convert("L")

# Threshold
#bw = gray.point(lambda x: 255 if x > THRESHOLD else 0)
bw = gray.point(lambda x: 0 if x > THRESHOLD else 255)
bw.save(OUTPUT_PREVIEW, "JPEG", quality=95)

# Convert to 0/1 (1 = black)
pixels = (np.array(bw) == 0).astype(np.uint8)

# Allocate buffer
DisplayLen = WIDTH * HEIGHT // 8
buffer = np.zeros(DisplayLen, dtype=np.uint8)

H4 = HEIGHT // 4

for y in range(HEIGHT):
    inv_y = HEIGHT - 1 - y
    block_y = inv_y // 4
    local_y = inv_y % 4

    for x in range(WIDTH):
        byte_x = x // 2
        local_x = x % 2

        index = byte_x * H4 + block_y
        bit = 7 - (local_y * 2 + local_x)

        if pixels[y, x]:
            buffer[index] |= (1 << bit)

# Save binary
with open(OUTPUT_BIN, "wb") as f:
    f.write(buffer.tobytes())

print("Correct-format BIN generated:", OUTPUT_BIN)
print("Size:", len(buffer))


with open(OUTPUT_BIN, "rb") as f:
    data = f.read()

with open(OUTPUT_H, "w") as h:
    h.write("#ifndef "+H_name+"\n#define "+H_name+"\n\n")
    h.write("#include <Arduino.h>\n\n")
    h.write("const uint8_t "+INPUT_IMAGE_file_name+"_image_bin[] PROGMEM = {\n")

    for i, b in enumerate(data):
        if i % 12 == 0:
            h.write("  ")
        h.write(f"0x{b:02X}, ")
        if i % 12 == 11:
            h.write("\n")

    h.write("\n};\n\n#endif\n")

