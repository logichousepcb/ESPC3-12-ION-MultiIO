# Produces .pio/build/<env>/firmware-merged.bin (bootloader + partitions +
# boot_app0 + app) that can be flashed to a blank chip at offset 0x0.
Import("env")


def merge_bin(source, target, env):
    flat = env.Flatten(env.get("FLASH_EXTRA_IMAGES", [])) + [
        "$ESP32_APP_OFFSET",
        "$BUILD_DIR/${PROGNAME}.bin",
    ]
    # Pairs of (offset, path); quote the paths so folders with spaces work.
    images = [v if i % 2 == 0 else '"%s"' % v for i, v in enumerate(flat)]
    cmd = [
        '"$PYTHONEXE"', '"$OBJCOPY"',
        "--chip", "esp32c3", "merge_bin",
        "-o", '"$BUILD_DIR/firmware-merged.bin"',
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "4MB",
    ] + images
    env.Execute(" ".join(cmd))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
