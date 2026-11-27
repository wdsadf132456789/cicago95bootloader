cmake_minimum_required(VERSION 3.20)
project(chicago95 NONE)

option(BUILD_KERNEL "Compile the kernel binary before assembling image" OFF)

find_program(GCC gcc REQUIRED)
find_program(LD ld REQUIRED)
find_program(NASM nasm REQUIRED)
find_program(PYTHON3 python3 REQUIRED)

set(BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(OBJ_DIR "${CMAKE_BINARY_DIR}/obj")
set(MODULE_GEN_DIR "${CMAKE_BINARY_DIR}/gen/modules")
set(MODULE_COUNT 3)
set(IMG_OUT "${CMAKE_BINARY_DIR}/chicago95.bin")
set(SRC "${CMAKE_CURRENT_SOURCE_DIR}")

set(CFLAGS_32 -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -nostdinc -I${SRC}/include -c -O2 -Wall)
set(CFLAGS_64 -m64 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -nostdinc -I${SRC}/include -I${SRC}/stage3 -c -O2 -Wall)

add_custom_command(
    OUTPUT "${BIN_DIR}/stage1.bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_DIR}"
    COMMAND ${NASM} -f bin -Wno-label-redef-late "${SRC}/stage1/boot.asm" -o "${BIN_DIR}/stage1.bin"
    DEPENDS "${SRC}/stage1/boot.asm"
)

set(CORE_STAGE2_C
    stage2/main.c
    stage2/security/firewall/gen2_packet_filter/fw_gen2_packet_filter.c
    stage2/security/firewall/gen2_stateful/fw_gen2_stateful.c
    stage2/security/firewall/gen2_app_layer/fw_gen2_app_layer.c
    stage2/security/firewall/gen2_adaptive/fw_gen2_adaptive.c
    stage2/security/dns_encrypt/doh/dns_doh.c
    stage2/security/dns_encrypt/dot/dns_dot.c
    stage2/security/dns_encrypt/dnscrypt/dns_dnscrypt.c
    stage2/security/wifi_encrypt/wpa2_aes/wpa2_aes.c
    stage2/security/wifi_encrypt/wpa3_sae/wpa3_sae.c
    stage2/security/mac_encrypt/mac_random/mac_random.c
    stage2/security/mac_encrypt/mac_clone/mac_clone.c
    stage2/security/mac_encrypt/mac_mask/mac_mask.c
    stage2/security/mac_encrypt/mac_rot/mac_rot.c
    stage2/security/mac_encrypt/mac_oui/mac_oui.c
    stage2/security/anti_ip/anti_ip.c
    stage2/security/disk_encrypt/uuid_random/uuid_random.c
    stage2/security/disk_encrypt/serial_mask/serial_mask.c
    stage2/security/disk_encrypt/gpt_header/gpt_header.c
    stage2/security/disk_encrypt/mbr_scramble/mbr_scramble.c
    stage2/security/disk_encrypt/luks_camouflage/luks_camouflage.c
    stage2/security/disk_encrypt/partname_encrypt/partname_encrypt.c
    stage2/security/disk_encrypt/label_encrypt/label_encrypt.c
    stage2/security/disk_encrypt/smart_obfuscate/smart_obfuscate.c
    stage2/security/disk_encrypt/inquiry_scramble/inquiry_scramble.c
    stage2/security/disk_encrypt/fingerprint_rotator/fingerprint_rotator.c
    stage2/security/disk_privacy/uuid_encrypt/uuid_encrypt.c
    stage2/security/disk_privacy/uuid_rot/uuid_rot.c
    stage2/security/disk_privacy/uuid_clone/uuid_clone.c
    stage2/security/disk_privacy/disk_serial_mask/disk_serial_mask.c
    stage2/security/disk_privacy/disk_model_spoof/disk_model_spoof.c
    stage2/security/disk_privacy/disk_label_enc/disk_label_enc.c
    stage2/security/disk_privacy/disk_mbr_gpt_mask/disk_mbr_gpt_mask.c
    stage2/security/disk_privacy/disk_lba_scramble/disk_lba_scramble.c
    stage2/security/disk_privacy/disk_size_obfs/disk_size_obfs.c
    stage2/security/disk_privacy/disk_fs_hide/disk_fs_hide.c
    stage2/fs/brainfs_fat.c
    stage2/fs/brainfs_core.c
    stage2/fs/brainvfs.c
    stage2/fs/encfs_mount.c
    stage2/security/netguard/netguard.c
    stage2/security/common/aes/aes256.c
    stage2/security/common/sha256/sha256.c
    stage2/security/common/sha512/sha512_stub.c
    stage2/security/common/chacha20/chacha20.c
    stage2/security/common/poly1305/poly1305_stub.c
    stage2/security/common/rfc4/rfc4.c
    stage2/security/common/hkdf/hkdf.c
    stage2/security/common/crypto/crypto.c
    stage2/security/common/panic/panic.c
    stage2/security/common/pgp/pgp.c
    stage2/security/common/pgp/pgp_mpi.c
    stage2/security/john/john_core.c
    stage2/security/john/john_hash.c
    stage2/security/john/john_brute.c
    stage2/security/john/john_wordlist.c
    stage2/security/john/john_analyzer.c
    stage2/security/john/john_table.c
    stage2/security/tor/core/tor_core.c
    stage2/security/tor/socks5/tor_socks5.c
    stage2/security/tor/directory/tor_directory.c
    stage2/security/tor/hidden_service/tor_hs.c
    stage2/security/tor/tor_bootstrapper.c
    stage2/vga/vga.c
    stage2/memory/pmm.c
    stage2/memory/vmm.c
    stage2/menu/menu.c
    stage2/tape/ata.c
    stage2/tape/floppy.c
    stage2/boot/ring0_init.c
    stage2/boot/pre_init.c
    stage2/shell/shell.c
    stage2/shell/fish_shell.c
    stage2/shell/gnu_tools.c
    stage2/shell/awk.c
    stage2/shell/nano.c
    stage2/boot/fs_menu.c
    stage2/drivers/mouse.c
    stage2/gui/gui.c
    lib/real/real.c
    lib/protected/protected.c
    lib/long/long.c
    lib/divmod64.c
    lib/mem.c)

set(DEFAULT_OPT_C
    stage2/drivers/nic_e1000.c stage2/drivers/wifi/wifi_core.c stage2/drivers/wifi/wifi_autodetect.c
    stage2/drivers/wifi/intel/wifi_intel.c stage2/drivers/wifi/atheros/wifi_atheros.c
    stage2/drivers/wifi/broadcom/wifi_broadcom.c stage2/drivers/wifi/realtek/wifi_realtek.c
    stage2/drivers/wifi/mediatek/wifi_mediatek.c stage2/drivers/wifi/marvell/wifi_marvell.c stage2/drivers/wifi/prism5/wifi_prism5.c)

set(STAGE2_C ${CORE_STAGE2_C} ${DEFAULT_OPT_C})
set(_scan_bin "${CMAKE_BINARY_DIR}/scan_driver")
execute_process(
    COMMAND ${GCC} -O2 -Wall "${SRC}/tools/scan_driver.c" -o "${_scan_bin}"
    RESULT_VARIABLE _scan_rc
)
if(_scan_rc EQUAL 0)
    execute_process(
        COMMAND "${_scan_bin}"
        OUTPUT_VARIABLE _scan_out
        ERROR_QUIET
        RESULT_VARIABLE _scan_rc
    )
    if(_scan_rc EQUAL 0)
        string(REGEX MATCHALL "[a-zA-Z0-9_./]+[.]c" _scan_srcs "${_scan_out}")
        set(_scan_opt)
        foreach(_s ${_scan_srcs})
            if(_s IN_LIST DEFAULT_OPT_C)
                list(APPEND _scan_opt ${_s})
            endif()
        endforeach()
        if(_scan_opt)
            set(STAGE2_C ${CORE_STAGE2_C} ${_scan_opt})
        endif()
    endif()
endif()

set(STAGE2_OBJS)
macro(cc32 SRC_FILE)
    set(_obj "${OBJ_DIR}/${SRC_FILE}.o")
    get_filename_component(_odir "${_obj}" DIRECTORY)
    add_custom_command(
        OUTPUT "${_obj}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_odir}"
        COMMAND ${GCC} ${CFLAGS_32} "${SRC}/${SRC_FILE}" -o "${_obj}"
        DEPENDS "${SRC}/${SRC_FILE}"
    )
    list(APPEND STAGE2_OBJS "${_obj}")
endmacro()
foreach(_s ${STAGE2_C})
    cc32("${_s}")
endforeach()

set(STAGE2_ENTRY_OBJ "${OBJ_DIR}/stage2/entry.asm.o")
add_custom_command(
    OUTPUT "${STAGE2_ENTRY_OBJ}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OBJ_DIR}/stage2"
    COMMAND ${NASM} -f elf32 "${SRC}/stage2/entry.asm" -o "${STAGE2_ENTRY_OBJ}"
    DEPENDS "${SRC}/stage2/entry.asm"
)
list(PREPEND STAGE2_OBJS "${STAGE2_ENTRY_OBJ}")

add_custom_command(
    OUTPUT
        "${BIN_DIR}/stage2.bin"
        "${BIN_DIR}/stage2.elf"
    COMMAND
        ${CMAKE_COMMAND} -E make_directory "${BIN_DIR}"
    COMMAND
        ${LD} -m elf_i386 -Ttext 0x0600 --oformat binary -e stage2_entry
        ${STAGE2_OBJS}
        -o "${BIN_DIR}/stage2.bin"
    COMMAND
        ${LD} -m elf_i386 -Ttext 0x0600 -e stage2_entry
        ${STAGE2_OBJS}
        -o "${BIN_DIR}/stage2.elf"
    DEPENDS ${STAGE2_OBJS}
)

file(GLOB STAGE3_C CONFIGURE_DEPENDS "${SRC}/stage3/*.c")
set(STAGE3_OBJS)
foreach(_s ${STAGE3_C})
    get_filename_component(_name "${_s}" NAME)
    set(_obj "${OBJ_DIR}/stage3/${_name}.o")
    add_custom_command(
        OUTPUT "${_obj}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OBJ_DIR}/stage3"
        COMMAND ${GCC} ${CFLAGS_64} "${_s}" -o "${_obj}"
        DEPENDS "${_s}"
    )
    list(APPEND STAGE3_OBJS "${_obj}")
endforeach()
add_custom_command(
    OUTPUT "${BIN_DIR}/stage3.bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_DIR}"
    COMMAND ${LD} -Ttext 0x100000 --oformat binary -e stage3_entry ${STAGE3_OBJS} -o "${BIN_DIR}/stage3.bin"
    DEPENDS ${STAGE3_OBJS}
)

set(EXTRA_BINS)
foreach(_row IN ITEMS "4;0x2000;stage4_entry" "5;0x30000;stage5_entry" "6;0x40000;stage6_entry" "7;0x50000;stage7_entry" "8;0x60000;stage8_entry")
    list(GET _row 0 _n)
    list(GET _row 1 _addr)
    list(GET _row 2 _entry)
    set(_obj "${OBJ_DIR}/stage${_n}/main.o")
    add_custom_command(
        OUTPUT "${_obj}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OBJ_DIR}/stage${_n}"
        COMMAND ${GCC} ${CFLAGS_64} "${SRC}/stage${_n}/main.c" -o "${_obj}"
        DEPENDS "${SRC}/stage${_n}/main.c"
    )
    add_custom_command(
        OUTPUT "${BIN_DIR}/stage${_n}.bin"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_DIR}"
        COMMAND ${LD} -Ttext ${_addr} --oformat binary -e ${_entry} "${_obj}" -o "${BIN_DIR}/stage${_n}.bin"
        DEPENDS "${_obj}"
    )
    list(APPEND EXTRA_BINS "${BIN_DIR}/stage${_n}.bin")
endforeach()

set(_gen_stamp "${CMAKE_BINARY_DIR}/.stages_9_100.stamp")
add_custom_command(
    OUTPUT "${_gen_stamp}"
    COMMAND ${PYTHON3} "${SRC}/tools/gen_stages.py"
    COMMAND ${CMAKE_COMMAND} -E touch "${_gen_stamp}"
    DEPENDS "${SRC}/tools/gen_stages.py"
    WORKING_DIRECTORY "${SRC}"
)

foreach(_n RANGE 9 100)
    math(EXPR _addr "0x30000 + (${_n} - 5) * 0x10000")
    string(TOUPPER "${_addr}" _hex)
    set(_obj "${OBJ_DIR}/stage${_n}/main.o")
    add_custom_command(
        OUTPUT "${_obj}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OBJ_DIR}/stage${_n}"
        COMMAND ${GCC} ${CFLAGS_64} "${SRC}/stage${_n}/main.c" -o "${_obj}"
        DEPENDS "${_gen_stamp}"
    )
    add_custom_command(
        OUTPUT "${BIN_DIR}/stage${_n}.bin"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_DIR}"
        COMMAND ${LD} -Ttext ${_addr} --oformat binary -e stage${_n}_entry "${_obj}" -o "${BIN_DIR}/stage${_n}.bin"
        DEPENDS "${_obj}"
    )
    list(APPEND EXTRA_BINS "${BIN_DIR}/stage${_n}.bin")
endforeach()

set(MODULE_DIR_BIN "${MODULE_GEN_DIR}/module_dir.bin")
set(MODULE_PAYLOADS)
math(EXPR _module_last "${MODULE_COUNT} - 1")
foreach(_i RANGE ${_module_last})
    list(APPEND MODULE_PAYLOADS "${MODULE_GEN_DIR}/module_${_i}.bin")
endforeach()

add_custom_command(
    OUTPUT "${MODULE_DIR_BIN}" ${MODULE_PAYLOADS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MODULE_GEN_DIR}"
    COMMAND ${PYTHON3} "${SRC}/tools/gen_modules.py" --output-dir "${MODULE_GEN_DIR}"
    DEPENDS "${SRC}/tools/gen_modules.py" "${SRC}/include/boot/bootinfo.h"
)

set(IMG_DEPS "${BIN_DIR}/stage1.bin" "${BIN_DIR}/stage2.bin" "${BIN_DIR}/stage3.bin" ${EXTRA_BINS} "${MODULE_DIR_BIN}" ${MODULE_PAYLOADS})
set(KERNEL_BIN "${SRC}/kernel/build/kernel.bin")
set(KERNEL_ARG "-DKERNEL_BIN=${KERNEL_BIN}")
if(BUILD_KERNEL)
    add_custom_command(
        OUTPUT "${KERNEL_BIN}"
        COMMAND sh "${SRC}/tools/prompt_kernel.sh" "${SRC}/kernel"
        DEPENDS "${SRC}/tools/prompt_kernel.sh"
    )
    list(APPEND IMG_DEPS "${KERNEL_BIN}")
endif()

add_custom_command(
    OUTPUT "${IMG_OUT}"
    COMMAND ${CMAKE_COMMAND} -DIMG_OUT="${IMG_OUT}" -DSTAGE_BIN_DIR="${BIN_DIR}" ${KERNEL_ARG}
            -DMODULE_DIR_BIN="${MODULE_DIR_BIN}" -DMODULE_PAYLOAD_DIR="${MODULE_GEN_DIR}" -DMODULE_COUNT=${MODULE_COUNT}
            -P "${SRC}/tools/make_image.cmake"
    DEPENDS ${IMG_DEPS}
)
add_custom_target(chicago95 ALL DEPENDS "${IMG_OUT}")
