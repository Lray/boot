cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Boot)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Ymodem download transport
    Ymodem/common.c
    Ymodem/ymodem.c

    # MCUboot bootutil
    Boot/bootutil/src/boot_area.c
    Boot/bootutil/src/boot_active_slot_flash.c
    Boot/bootutil/src/bootuitl_loader.c
    Boot/bootutil/src/bootutil_img_security_cnt.c
    Boot/bootutil/src/security_cnt.c
    Boot/bootutil/src/bootutil_img_hash.c
    Boot/bootutil/src/bootutil_sha_stm32u5_hash.c
    Boot/bootutil/src/bootutil_ecdsa_asn1.c
    Boot/bootutil/src/bootutil_ecdsa_tinycrypt.c
    Boot/bootutil/src/bootutil_ecdsa_stm32u5_pka.c
    Boot/bootutil/src/tlv.c
    Boot/bootutil/src/bootutil_find_key.c
    Boot/bootutil/src/image_ecdsa.c
    Boot/bootutil/src/image_validate.c
    Boot/bootutil/src/bootutil_misc.c
    Boot/bootutil/src/bootutil_public.c
    Boot/bootutil/src/loader.c
    Boot/bootutil/src/keys.c
    Boot/flash_map_backend/cy_flash_map.c

    # TinyCrypt crypto primitives
    Boot/ext/tinycrypt/lib/source/sha256.c
    Boot/ext/tinycrypt/lib/source/utils.c
    Boot/ext/tinycrypt/lib/source/ecc.c
    Boot/ext/tinycrypt/lib/source/ecc_dsa.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Boot
    Boot/bootutil/include
    Boot/flash_map_backend
    Boot/mcuboot_config
    Boot/sysflash
    Boot/ext/tinycrypt/lib/include
    Ymodem
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
