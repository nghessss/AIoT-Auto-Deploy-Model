#!/bin/bash
set -e

DOCKER="docker run --rm -v $PWD:/project -w /project -u $UID -e HOME=/tmp espressif/idf"

echo "🧹 Cleaning..."
$DOCKER idf.py fullclean

echo "🛠️  Building..."
$DOCKER idf.py build

echo "📦 Merging binary manually..."
$DOCKER python -m esptool \
  --chip esp32s3 merge_bin \
  -o aiot-bin.bin \
  -f raw \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 16MB \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x10000 build/AIoT.bin

echo "✅ Done! Final binary: aiot-bin.bin"