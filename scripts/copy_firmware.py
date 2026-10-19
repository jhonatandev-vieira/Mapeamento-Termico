Import("env")
import shutil, os, re

VERSION_FILE = os.path.join(env["PROJECT_DIR"], "src", "main.cpp")
version = "0.0.0"
with open(VERSION_FILE) as f:
    for line in f:
        m = re.search(r'FIRMWARE_VERSION\s+"([^"]+)"', line)
        if m:
            version = m.group(1)
            break

src  = os.path.join(env["PROJECT_BUILD_DIR"], env["PIOENV"], "firmware.bin")
dest_dir = os.path.join(env["PROJECT_DIR"], "..", "..", "backend", "public", "firmware", "ds18b20")
os.makedirs(dest_dir, exist_ok=True)
dest = os.path.join(dest_dir, f"aria_ds18b20_v{version}.bin")
shutil.copy(src, dest)
print(f"[copy_firmware] {dest}")
