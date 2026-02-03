##
# @file CMakeLists.txt
# @brief Game Boy Emulator App for Tuya T5 Pocket
#/

# APP_PATH
set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})

# APP_NAME
get_filename_component(APP_NAME ${APP_PATH} NAME)

# APP_SRCS - collect main app sources
aux_source_directory(${APP_PATH}/src APP_SRCS)
# Remove display subdirectory (it's just a placeholder)
list(REMOVE_ITEM APP_SRCS ${APP_PATH}/src/display/CMakeLists.txt)
# Remove display subdirectory (handled separately)
list(REMOVE_ITEM APP_SRCS ${APP_PATH}/src/display)

# Font sources (Terminus TTF Bold fonts)
aux_source_directory(${APP_PATH}/src/fonts FONT_SRCS)

# GNUBoy core sources
aux_source_directory(${APP_PATH}/gnuboy/src GNUBOY_SRCS)
# Remove files we don't need to save space
list(REMOVE_ITEM GNUBOY_SRCS 
    ${APP_PATH}/gnuboy/src/debug.c
    ${APP_PATH}/gnuboy/src/main.c      # SDL2 main, we use tuya_main.c
    ${APP_PATH}/gnuboy/src/rccmds.c    # RC system - not needed for minimal
    ${APP_PATH}/gnuboy/src/rckeys.c    # RC system - not needed for minimal
    ${APP_PATH}/gnuboy/src/rcvars.c    # RC system - not needed for minimal
    ${APP_PATH}/gnuboy/src/rcfile.c    # RC system - not needed for minimal
    ${APP_PATH}/gnuboy/src/exports.c   # RC system - not needed for minimal
    ${APP_PATH}/gnuboy/src/split.c     # RC system - not needed for minimal
    ${APP_PATH}/gnuboy/src/path.c      # RC system - not needed for minimal
)

# GNUBoy compression libraries (required by loader.c)
aux_source_directory(${APP_PATH}/gnuboy/lib/gz GZ_SRCS)
aux_source_directory(${APP_PATH}/gnuboy/lib/xz XZ_SRCS)

# System-specific files (nix implementations for embedded)
aux_source_directory(${APP_PATH}/gnuboy/sys/nix SYS_SRCS)
# Remove SDL2-specific files
list(REMOVE_ITEM SYS_SRCS 
    ${APP_PATH}/gnuboy/sys/nix/nix.c
    ${APP_PATH}/gnuboy/sys/nix/io_pipe.c
    ${APP_PATH}/gnuboy/sys/nix/io_network.c
)
# Add Tuya-specific system implementations
list(APPEND SYS_SRCS ${APP_PATH}/gnuboy/sys/nix/nix_tuya.c)

# Combine all sources
list(APPEND APP_SRCS ${GNUBOY_SRCS} ${GZ_SRCS} ${XZ_SRCS} ${SYS_SRCS} ${FONT_SRCS})

set(APP_INC 
    ${APP_PATH}/include
    ${APP_PATH}/gnuboy/include
    ${APP_PATH}/gnuboy
    ${APP_PATH}/gnuboy/sys/nix
)

########################################
# Target Configure
########################################
add_library(${EXAMPLE_LIB})

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRCS}
    )

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_INC}
    )

target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        "-DLV_LVGL_H_INCLUDE_SIMPLE"
        "-DIS_LITTLE_ENDIAN"
        "-DIS_LINUX"
        "-DRG_TARGET_TUYA_T5_POCKET"
        "-std=c99"
    )
