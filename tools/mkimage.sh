#!/bin/bash
# Builds the bootable disk image, with the layout the PRD asks for:
#
#   sector 0        MBR: GRUB's first stage and the partition table
#   sectors 1-2047  GRUB's second stage
#   partition 1     kernel image (ext2)
#   partition 2     swap
#   partition 3     user data (ext2), mounted as /
#
# Usage: mkimage.sh <kernel.elf> <grub.cfg> <rootfs folder> <output image>

set -e

KERNEL=$1
GRUB_CFG=$2
ROOTFS=$3
IMAGE=$4

SECTOR=512
FIRST_PARTITION=2048
KERNEL_SECTORS=$((16 * 2048))       # 16 MB
SWAP_SECTORS=$((32 * 2048))         # 32 MB
DATA_SECTORS=$((64 * 2048))         # 64 MB

SWAP_START=$((FIRST_PARTITION + KERNEL_SECTORS))
DATA_START=$((SWAP_START + SWAP_SECTORS))
TOTAL_SECTORS=$((DATA_START + DATA_SECTORS))

WORK=$(dirname "$IMAGE")/image-parts
GRUB_DIR=/usr/lib/grub/i386-pc

# Plain ext2 with only the file-type feature, so the kernel's driver can read it.
make_ext2() {
    local image=$1 sectors=$2 source=$3

    rm -f "$image"
    /usr/sbin/mke2fs -q -t ext2 -b 4096 -I 128 -m 0 -O none,filetype \
        -d "$source" "$image" "$((sectors / 8))"
}

rm -rf "$WORK"
mkdir -p "$WORK/boot-files/boot/grub"

cp "$KERNEL" "$WORK/boot-files/boot/kernel.elf"
cp "$GRUB_CFG" "$WORK/boot-files/boot/grub/grub.cfg"

make_ext2 "$WORK/kernel.ext2" "$KERNEL_SECTORS" "$WORK/boot-files"
make_ext2 "$WORK/data.ext2" "$DATA_SECTORS" "$ROOTFS"

rm -f "$IMAGE"
truncate -s "$((TOTAL_SECTORS * SECTOR))" "$IMAGE"

/usr/sbin/sfdisk --quiet --no-tell-kernel "$IMAGE" <<EOF
label: dos
start=$FIRST_PARTITION, size=$KERNEL_SECTORS, type=83, bootable
start=$SWAP_START, size=$SWAP_SECTORS, type=82
start=$DATA_START, size=$DATA_SECTORS, type=83
EOF

# mke2fs copies owners from this PC. On the OS, files belong to root, and each
# user owns their home folder.
(
    cd "$ROOTFS"
    find . -mindepth 1 | sed 's|^\.||' | while read -r path; do
        echo "sif \"$path\" uid 0"
        echo "sif \"$path\" gid 0"
    done
    echo 'sif /home/student uid 1000'
    echo 'sif /home/student gid 1000'
) > "$WORK/owners.cmd"
/usr/sbin/debugfs -w -f "$WORK/owners.cmd" "$WORK/data.ext2" > /dev/null 2>&1

dd if="$WORK/kernel.ext2" of="$IMAGE" bs=$SECTOR seek=$FIRST_PARTITION conv=notrunc status=none
dd if="$WORK/data.ext2" of="$IMAGE" bs=$SECTOR seek=$DATA_START conv=notrunc status=none

# Install GRUB by hand, because grub-install would need a real disk and root.
grub-mkimage -O i386-pc -p "(hd0,msdos1)/boot/grub" -o "$WORK/core.img" \
    biosdisk part_msdos ext2 normal multiboot configfile

dd if="$GRUB_DIR/boot.img" of="$IMAGE" bs=440 count=1 conv=notrunc status=none
dd if="$WORK/core.img" of="$IMAGE" bs=$SECTOR seek=1 conv=notrunc status=none
