file(GLOB_RECURSE SRC_COLLECTION
     "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c")

idf_component_register(
    SRCS    "main.c"
            "${SRC_COLLECTION}"
    INCLUDE_DIRS    "."
                    "include"
                    "include/ble"
                    "include/btn_progress"
                    "include/descriptors"
                    "include/tusb"
                    "include/hid_custom"
                    "include/esp_now"
)
