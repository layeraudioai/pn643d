import os
import sys
import struct
import glob
import subprocess
import re
import random
import shutil
    
def get_random_rom_audio(rom_path, output_dir):
    audioout = output_dir+"/"+output_dir+".wav"
    default_audio = "Source/SysCTR/Resources/audio.wav"
    shutil.copy(default_audio, audioout)

    exe_path = "Tools/n64sfxdump.exe"
    wavs_dir = os.path.join(output_dir, "extracted_wavs")
    os.makedirs(wavs_dir, exist_ok=True)
    
    try:
        print(f"Dumping WAVs from ROM using n64sfxdump...")
        subprocess.run([exe_path, rom_path, wavs_dir], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        wav_files = glob.glob(os.path.join(wavs_dir, "*.wav"))
        if wav_files:
            chosen = random.choice(wav_files)
            print(f"Picked random banner audio from ROM ({len(wav_files)} available): {os.path.basename(chosen)}")
            return chosen
    except Exception as e:
        print(f"Error dumping WAVs with n64sfxdump: {e}")
        
    return audioout

def sanitize(name):
    return re.sub(r'[^a-zA-Z0-9]', '', name).lower()

def get_publisher(manuf_code):
    m_map = {
        ord('N'): 'Nintendo',
        ord('R'): 'Rare',
        ord('A'): 'Acclaim',
        ord('C'): 'Capcom',
        ord('E'): 'Electronic Arts',
        ord('H'): 'Hudson Soft',
        ord('K'): 'Konami',
        ord('T'): 'Tecmo',
        ord('W'): 'Midway',
        ord('S'): 'Square',
        ord('B'): 'Bandai',
        ord('L'): 'Vitus',
        ord('I'): 'Interplay',
        ord('U'): 'Ubisoft',
        ord('V'): 'SEGA',
    }
    return m_map.get(manuf_code, 'DaedalusX64 Team')

def prep_images(input_png, banner_png_out, icon_png_out):
    # Try using PIL (Pillow) first
    try:
        from PIL import Image
        img = Image.open(input_png).convert('RGBA')
        
        # 1. Banner: 256x128
        banner_img = img.resize((256, 128), Image.Resampling.LANCZOS if hasattr(Image, 'Resampling') else Image.ANTIALIAS)
        banner_img.save(banner_png_out, 'PNG')
        
        # 2. Icon: 48x48 (scaling and cropping)
        target = 48
        w, h = img.size
        scale = max(target / w, target / h)
        new_w = int(w * scale)
        new_h = int(h * scale)
        img_scaled = img.resize((new_w, new_h), Image.Resampling.LANCZOS if hasattr(Image, 'Resampling') else Image.ANTIALIAS)
        
        left = (new_w - target) // 2
        top = (new_h - target) // 2
        img_cropped = img_scaled.crop((left, top, left + target, top + target))
        img_cropped.save(icon_png_out, 'PNG')
        print("Images prepared successfully using PIL.")
        return True
    except Exception as e:
        print(f"PIL processing failed ({e}), trying ffmpeg...")

    # Fallback to ffmpeg
    try:
        # Banner 256x128
        subprocess.run([
            'ffmpeg', '-y', '-i', input_png,
            '-vf', 'scale=256:128',
            banner_png_out
        ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        # Icon 48x48 (scale + crop)
        subprocess.run([
            'ffmpeg', '-y', '-i', input_png,
            '-vf', 'scale=48:48:force_original_aspect_ratio=increase,crop=48:48',
            icon_png_out
        ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        print("Images prepared successfully using ffmpeg.")
        return True
    except Exception as e2:
        print(f"ffmpeg processing also failed ({e2}), copying original images...")
        import shutil
        shutil.copy(input_png, banner_png_out)
        shutil.copy(input_png, icon_png_out)
        return False

def process_rom(rom_path, output_dir, folder_name):
    with open(rom_path, 'rb') as f:
        header_data = f.read(0x1000)

    if len(header_data) < 0x40:
        print("Error: Invalid ROM size")
        return False

    # Check byteswap
    magic = list(header_data[:4])
    if magic != [0x80, 0x37, 0x12, 0x40]:
        r = list(header_data)
        if magic == [0x40, 0x12, 0x37, 0x80]:
            for i in range(0, len(header_data), 4):
                r[i+0] = header_data[i+3]
                r[i+1] = header_data[i+2]
                r[i+2] = header_data[i+1]
                r[i+3] = header_data[i+0]
        elif magic == [0x37, 0x80, 0x40, 0x12]:
            for i in range(0, len(header_data), 4):
                r[i+0] = header_data[i+1]
                r[i+1] = header_data[i+0]
                r[i+2] = header_data[i+3]
                r[i+3] = header_data[i+2]
        elif magic == [0x12, 0x40, 0x80, 0x37]:
            for i in range(0, len(header_data), 4):
                r[i+0] = header_data[i+2]
                r[i+1] = header_data[i+3]
                r[i+2] = header_data[i+0]
                r[i+3] = header_data[i+1]
        header_data = bytes(r)

    # Name at 0x20, 20 bytes
    name_bytes = header_data[0x20:0x34]
    rom_name = name_bytes.decode('latin1', errors='ignore').strip().rstrip('\x00')
    if not rom_name:
        rom_name = folder_name

    # Manufacturer at 0x3B
    manuf_code = header_data[0x3B] if len(header_data) > 0x3B else ord('N')
    publisher = get_publisher(manuf_code)

    print(f"ROM Name: {rom_name}, Publisher: {publisher}")

    # Find preview PNG in Data/Resources/Preview
    preview_dir = "Data/Resources/Preview"
    preview_png = None
    
    sanitized_folder = sanitize(folder_name)
    sanitized_rom = sanitize(rom_name)
    
    all_previews = glob.glob(os.path.join(preview_dir, "*.png"))
    
    for p in all_previews:
        base = os.path.splitext(os.path.basename(p))[0]
        if sanitize(base) == sanitized_folder or sanitize(base) == sanitized_rom:
            preview_png = p
            break
            
    if not preview_png:
        for p in all_previews:
            base = os.path.splitext(os.path.basename(p))[0]
            s_base = sanitize(base)
            if sanitized_folder in s_base or s_base in sanitized_folder or sanitized_rom in s_base or s_base in sanitized_rom:
                preview_png = p
                break

    if not preview_png and all_previews:
        preview_png = all_previews[0]
    if not preview_png:
        preview_png = "Data/Resources/logo.png"

    print(f"Using Preview PNG: {preview_png}")

    os.makedirs(output_dir, exist_ok=True)
    temp_banner_png = os.path.join(output_dir, sys.argv[3] + "_banner.png")
    temp_icon_png = os.path.join(output_dir, sys.argv[3] + "_icon.png")
    
    # Prepare images (banner 256x128, icon 32x32 scaled & cropped)
    prep_images(preview_png, temp_banner_png, temp_icon_png)

    banner_out = os.path.join(output_dir, sys.argv[3] + ".bnr")
    smdh_out = os.path.join(output_dir, sys.argv[3] + ".smdh")
    audio_wav = get_random_rom_audio(sys.argv[1], output_dir)
    audioout = output_dir+"/"+output_dir+".wav"
    os.replace(audio_wav,audioout)
    bannertool = "Tools/bannertool.exe"
    cmd_banner = [
        bannertool, "makebanner",
        "-i", temp_banner_png,
        "-a", audioout,
        "-o", banner_out
    ]
    print("Running:", " ".join(cmd_banner))
    subprocess.run(cmd_banner, check=True)

    cmd_smdh = [
        bannertool, "makesmdh",
        "-s", rom_name[:16],
        "-l", rom_name,
        "-p", publisher,
        "-i", temp_icon_png,
        "-o", smdh_out
    ]
    print("Running:", " ".join(cmd_smdh))
    subprocess.run(cmd_smdh, check=True)

    # Cleanup temp images
    for tmp in [temp_banner_png, temp_icon_png]:
        if os.path.exists(tmp):
            os.remove(tmp)

    return True

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print("Usage: generate_banner_smdh.py <rom_path> <output_dir> <folder_name>")
        sys.exit(1)
    process_rom(sys.argv[1], sys.argv[2], sys.argv[3])
