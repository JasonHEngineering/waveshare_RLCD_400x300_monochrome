# -*- coding: utf-8 -*-


from PIL import Image

def convert_to_2bit_dither(input_path, output_path):
    img = Image.open(input_path).convert("L")
    
    # Convert to 4 colors using built-in quantization
    img_2bit = img.quantize(colors=4, method=Image.FASTOCTREE)
    
    # Convert back to grayscale so it's still monochrome JPG
    img_2bit = img_2bit.convert("L")
    
    img_2bit.save(output_path, "JPEG")

# Example usage
convert_to_2bit_dither("sample_1.jpg", "sample_1_2bit_dither.jpg")
