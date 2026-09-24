set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Locate the STM32 GNU bundle from PATH, CUBE_BUNDLE_PATH, or the standard
# STM32Cube local installation directory. This keeps a fresh configure from
# depending on a shell-specific PATH setup.
set(_STM32_GNU_BIN_DIRS)
if(DEFINED ENV{CUBE_BUNDLE_PATH})
    file(GLOB _STM32_GNU_BIN_DIRS_FROM_ENV LIST_DIRECTORIES true
         "$ENV{CUBE_BUNDLE_PATH}/gnu-tools-for-stm32/*/bin")
    list(APPEND _STM32_GNU_BIN_DIRS ${_STM32_GNU_BIN_DIRS_FROM_ENV})
endif()
if(WIN32 AND DEFINED ENV{LOCALAPPDATA})
    file(GLOB _STM32_GNU_BIN_DIRS_FROM_LOCALAPPDATA LIST_DIRECTORIES true
         "$ENV{LOCALAPPDATA}/stm32cube/bundles/gnu-tools-for-stm32/*/bin")
    list(APPEND _STM32_GNU_BIN_DIRS ${_STM32_GNU_BIN_DIRS_FROM_LOCALAPPDATA})
endif()
list(REMOVE_DUPLICATES _STM32_GNU_BIN_DIRS)
list(SORT _STM32_GNU_BIN_DIRS ORDER DESCENDING)

find_program(ARM_NONE_EABI_GCC
    NAMES arm-none-eabi-gcc
    HINTS ${_STM32_GNU_BIN_DIRS}
)
if(NOT ARM_NONE_EABI_GCC)
    message(FATAL_ERROR
        "arm-none-eabi-gcc was not found. Add the STM32 GNU tools bin directory to PATH or set CUBE_BUNDLE_PATH.")
endif()

get_filename_component(_STM32_GNU_BIN_DIR "${ARM_NONE_EABI_GCC}" DIRECTORY)
find_program(ARM_NONE_EABI_GXX NAMES arm-none-eabi-g++ HINTS "${_STM32_GNU_BIN_DIR}")
find_program(ARM_NONE_EABI_OBJCOPY NAMES arm-none-eabi-objcopy HINTS "${_STM32_GNU_BIN_DIR}")
find_program(ARM_NONE_EABI_SIZE NAMES arm-none-eabi-size HINTS "${_STM32_GNU_BIN_DIR}")

set(CMAKE_C_COMPILER                "${ARM_NONE_EABI_GCC}")
set(CMAKE_ASM_COMPILER              "${ARM_NONE_EABI_GCC}")
set(CMAKE_CXX_COMPILER              "${ARM_NONE_EABI_GXX}")
set(CMAKE_LINKER                    "${ARM_NONE_EABI_GXX}")
set(CMAKE_OBJCOPY                   "${ARM_NONE_EABI_OBJCOPY}")
set(CMAKE_SIZE                      "${ARM_NONE_EABI_SIZE}")

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage")

# The cyclomatic-complexity parameter must be defined for the Cyclomatic complexity feature in STM32CubeIDE to work.
# However, most GCC toolchains do not support this option, which causes a compilation error; for this reason, the feature is disabled by default.
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcyclomatic-complexity")

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32F407xx_FLASH.ld\"")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --specs=nano.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")
set(TOOLCHAIN_LINK_LIBRARIES "m")
