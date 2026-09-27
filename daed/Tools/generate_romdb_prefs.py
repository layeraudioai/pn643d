import sys
import os
import struct

def generate_romdb_and_prefs(rom_path, folder_name):
    if not os.path.exists(rom_path):
        print(f"Error: ROM file {rom_path} not found.")
        return

    rom_size = os.path.getsize(rom_path)

    with open(rom_path, 'rb') as f:
        header_data = f.read(0x1000)

    if len(header_data) < 0x40:
        print("Error: Invalid ROM header")
        return

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

    crc1 = struct.unpack('>I', header_data[0x10:0x14])[0]
    crc2 = struct.unpack('>I', header_data[0x14:0x18])[0]
    country_id = header_data[0x3E]

    # 1. Generate rom.db
    # Magic (8 bytes): 0x42444D5244454144 (DAEDRMDB)
    # Version (4 bytes): 4
    magic_no = 0x42444D5244454144
    version = 4
    num_files = 1
    num_details = 1

    file_name_str = f"Roms/{folder_name}.z64"
    filename_bytes = file_name_str.encode('ascii').ljust(261 + 1, b'\x00') # kMaxFilenameLen {260} + 1 = 261 + null
    rom_id_bytes = struct.pack('<IIB3x', crc1, crc2, country_id) # 12 bytes

    romdb_path = "rom.db"
    with open(romdb_path, 'wb') as db:
        db.write(struct.pack('<QI', magic_no, version))
        db.write(struct.pack('<I', num_files))
        db.write(filename_bytes[:261])
        db.write(rom_id_bytes)

        db.write(struct.pack('<I', num_details))
        db.write(rom_id_bytes)
        db.write(struct.pack('<II', rom_size, 1)) # rom_size, cic_type

    print(f"Generated {romdb_path} successfully for {folder_name} (CRC1: {crc1:08x}, CRC2: {crc2:08x}, Country: {country_id})")

    # 2. Generate preferences.ini
    prefs_path = "preferences.ini"
    prefs_content = """DisplayFramerate=0
ForceLinearFilter=yes
RumblePak=yes
BatteryWarning=yes
LargeROMBuffer=yes
GuiColor=0
StickMinDeadzone=0.200000
StickMaxDeadzone=0.900000
ViewportType=0
TVEnable=no
TVLaced=no
TVType=0

"""
    with open(prefs_path, 'w') as p:
        p.write(prefs_content)

    print(f"Generated {prefs_path} successfully.")

if __name__ == '__main__':
    if len(sys.argv) >= 3:
        generate_romdb_and_prefs(sys.argv[1], sys.argv[2])
    else:
        print("Usage: python generate_romdb_prefs.py <rom_path> <folder_name>")
