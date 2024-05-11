file(GLOB_RECURSE SRC_COLLECTION
     "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c")

file(GLOB_RECURSE MANAGED_COMPONENTS_SOURCES 
     "${CMAKE_CURRENT_SOURCE_DIR}/managed_components/**/*.c")

idf_component_register(
    SRCS    "main.c"
            "${SRC_COLLECTION}"
            "${MANAGED_COMPONENTS_SOURCES}"
    INCLUDE_DIRS    "."
                    "include"
                    "include/ble"
                    "include/tusb"
                    "include/hid"
                    "managed_components/espressif__keyboard_button/include"
)
