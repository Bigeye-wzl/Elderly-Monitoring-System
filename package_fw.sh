#!/bin/bash
# 手动打包 RK2206 固件镜像
# 分区布局必须与 sdk_liteos/include/link.h 一致，否则烧进去引导找不到 liteos
#
# 用法: 在仓库根目录执行 ./package_fw.sh
#   也可用环境变量 ROOT_DIR 指定仓库路径, 便于打包其他目录下的构建产物:
#   ROOT_DIR=/path/to/rk2206_ohos ./package_fw.sh
set -e

BOARD=rk2206
PRODUCT=lockzhiner-rk2206
ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "$0")" && pwd)}"  # 仓库根目录
SDK_DIR=${ROOT_DIR}/device/rockchip/rk2206
OUT_DIR=${ROOT_DIR}/out/${BOARD}/${PRODUCT}
IMAGE_DIR=${OUT_DIR}/images
TOOLS_DIR=${ROOT_DIR}/device/rockchip/tools/package
LOADER_DIR=${SDK_DIR}/sdk_liteos/loader
LINK_H=${SDK_DIR}/sdk_liteos/include/link.h

# ---- 分区大小从 link.h 读取 (单位: 512B blocks) ----
# 保留 link.h 中的原始十六进制字符串原样写入 setting.ini, 与官方 mkimage.sh 行为一致
get_blocks() {
    awk -v key="$1" '$1=="#define" && $2==key {print $3; exit}' "${LINK_H}"
}
PART_SYSTEM_BLOCKS=$(get_blocks PART_SYSTEM_BLOCKS)
PART_LOADER_BLOCKS=$(get_blocks PART_LOADER_BLOCKS)
PART_LITEOS_BLOCKS=$(get_blocks PART_LITEOS_BLOCKS)
PART_ROOTFS_BLOCKS=$(get_blocks PART_ROOTFS_BLOCKS)
PART_USERFS_BLOCKS=$(get_blocks PART_USERFS_BLOCKS)

if [ -z "${PART_SYSTEM_BLOCKS}" ] || [ -z "${PART_LOADER_BLOCKS}" ] || [ -z "${PART_LITEOS_BLOCKS}" ] \
   || [ -z "${PART_ROOTFS_BLOCKS}" ] || [ -z "${PART_USERFS_BLOCKS}" ]; then
    echo "错误: 无法从 ${LINK_H} 解析分区大小"
    exit 1
fi
echo "part blocks: sys=${PART_SYSTEM_BLOCKS} loader=${PART_LOADER_BLOCKS} liteos=${PART_LITEOS_BLOCKS} rootfs=${PART_ROOTFS_BLOCKS} userfs=${PART_USERFS_BLOCKS}"

# 分区偏移 (十进制 blocks)
PART_SYSTEM_OFFSET=0
PART_LOADER_OFFSET=$((PART_SYSTEM_OFFSET + PART_SYSTEM_BLOCKS))
PART_LITEOS_OFFSET=$((PART_LOADER_OFFSET + PART_LOADER_BLOCKS))
PART_ROOTFS_OFFSET=$((PART_LITEOS_OFFSET + PART_LITEOS_BLOCKS))
PART_USERFS_OFFSET=$((PART_ROOTFS_OFFSET + PART_ROOTFS_BLOCKS))

LOADER1_BIN=${BOARD}_psram.bin
LOADER2_BIN=${BOARD}_loader.bin
LITEOS_BIN=liteos.bin
ROOTFS_BIN=rootfs_vfat.img
USERFS_BIN=userfs_vfag.img

LOADER_IMAGE=${LOADER_DIR}/${BOARD}_db_loader.bin
SETTING_INI=${OUT_DIR}/setting.ini
CONFIG_JSON=${OUT_DIR}/config.json

make_config_json() {
    echo "{"
    echo "  \"MAGIC\": \"RESC\","
    echo "  \"CHIP\": \"${BOARD}\","
    echo "  \"MODEL\": \"${PRODUCT}\","
    echo "  \"DESC\": \"${BOARD} OpenHarmony\","
    echo "  \"VERSION\": \"2.00.0000\","
    echo "  \"DIGEST\": \"JSHASH\""
    echo "}"
}

make_setting_ini() {
    echo "[System]"
    echo "FwVersion=1.0"
    echo "Gpt_Enable="
    echo "Backup_Partition_Enable="
    echo "Nano="
    echo "Loader_Encrypt=0"
    echo "Chip="
    echo "Model="
    echo "[UserPart1]:"
    echo "Name=IDBlock"
    echo "Type=0x2"
    echo "PartOffset=${PART_LOADER_OFFSET}"
    echo "PartSize=${PART_LOADER_BLOCKS}"
    echo "Flag="
    echo "File=${LOADER_DIR}/${LOADER1_BIN},${LOADER_DIR}/${LOADER2_BIN}"
    echo "[UserPart2]"
    echo "Name=liteos"
    echo "Type=0x8"
    echo "PartOffset=${PART_LITEOS_OFFSET}"
    echo "PartSize=${PART_LITEOS_BLOCKS}"
    echo "Flag="
    echo "File=${OUT_DIR}/${LITEOS_BIN}"
    # rootfs/userfs 为可选分区 (Flag=1): 镜像文件不存在时 firmware_merger 自动跳过
    echo "[UserPart3]"
    echo "Name=rootfs"
    echo "Type="
    echo "PartOffset=${PART_ROOTFS_OFFSET}"
    echo "PartSize=${PART_ROOTFS_BLOCKS}"
    echo "Flag=1"
    echo "File=${OUT_DIR}/${ROOTFS_BIN}"
    echo "[UserPart4]"
    echo "Name=userfs"
    echo "Type="
    echo "PartOffset=${PART_USERFS_OFFSET}"
    echo "PartSize=${PART_USERFS_BLOCKS}"
    echo "Flag=1"
    echo "File=${OUT_DIR}/${USERFS_BIN}"
}

echo "=== 打包 RK2206 固件 ==="
if [ ! -d "${OUT_DIR}" ] || [ ! -f "${OUT_DIR}/${LITEOS_BIN}" ]; then
    echo "错误: ${OUT_DIR}/${LITEOS_BIN} 不存在, 请先执行 hb build 编译"
    exit 1
fi

mkdir -p "${IMAGE_DIR}"
make_config_json > "${CONFIG_JSON}"
make_setting_ini > "${SETTING_INI}"

arch_text=$(arch)
if [ "${arch_text}" = "x86_64" ]; then
    echo "arch is x86_64 and packet for x86_64"
    "${TOOLS_DIR}/resource_header_tool" pack --json "${CONFIG_JSON}" "${OUT_DIR}/${LITEOS_BIN}"
    "${TOOLS_DIR}/firmware_merger" -p "${SETTING_INI}" "${IMAGE_DIR}"
elif [ "${arch_text}" = "aarch64" ]; then
    echo "arch is aarch64 and packet for aarch64"
    python3 "${TOOLS_DIR}/resource_header_tool_aarch64.py" pack --json "${CONFIG_JSON}" "${OUT_DIR}/${LITEOS_BIN}"
    "${TOOLS_DIR}/firmware_merger_aarch64" -p "${SETTING_INI}" "${IMAGE_DIR}"
else
    echo "package_fw.sh => ${arch_text} is out of the range!"
    exit 1
fi

cp "${LOADER_IMAGE}" "${IMAGE_DIR}/"

echo "=== 打包完成 ==="
ls -la "${IMAGE_DIR}"
