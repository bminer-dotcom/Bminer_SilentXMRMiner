# Generate C++ header with embedded Tox config JSON as hex-encoded bytes.
# Mirrors GenerateEmbeddedConfig.cmake. Usage:
#   generate_embedded_tox_config(INPUT_FILE OUTPUT_FILE)

function(generate_embedded_tox_config INPUT_FILE OUTPUT_FILE)
    message(STATUS "Generating embedded tox config from ${INPUT_FILE}...")

    if(NOT EXISTS "${INPUT_FILE}")
        message(FATAL_ERROR "Tox config file not found: ${INPUT_FILE}")
    endif()

    get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    file(READ "${INPUT_FILE}" CONFIG_JSON HEX)

    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\1;" HEX_LIST "${CONFIG_JSON}")

    set(HEX_BYTES "")
    set(BYTE_COUNT 0)
    foreach(HEX_BYTE ${HEX_LIST})
        if(HEX_BYTE AND NOT HEX_BYTE STREQUAL ";")
            string(APPEND HEX_BYTES "0x${HEX_BYTE}, ")
            math(EXPR BYTE_COUNT "${BYTE_COUNT} + 1")
            if(BYTE_COUNT EQUAL 16)
                string(APPEND HEX_BYTES "\n        ")
                set(BYTE_COUNT 0)
            endif()
        endif()
    endforeach()

    string(REGEX REPLACE ", \n        $" "" HEX_BYTES "${HEX_BYTES}")
    string(REGEX REPLACE ", $" "" HEX_BYTES "${HEX_BYTES}")

    set(TEMPLATE_CONTENT
"#pragma once

#include <string>

// Embedded Tox config JSON (generated from tox_config.json).
inline std::string GetEmbeddedToxConfigJson() {
    static const unsigned char config_bytes[] = {
        @HEX_BYTES@
    };
    return std::string(reinterpret_cast<const char*>(config_bytes), sizeof(config_bytes));
}

static const std::string EMBEDDED_TOX_CONFIG_JSON = GetEmbeddedToxConfigJson();
")

    set(TEMP_TEMPLATE "${OUTPUT_DIR}/tox_config.h.in")
    file(WRITE "${TEMP_TEMPLATE}" "${TEMPLATE_CONTENT}")

    configure_file("${TEMP_TEMPLATE}" "${OUTPUT_FILE}" @ONLY)

    message(STATUS "Generated embedded tox config header: ${OUTPUT_FILE}")
endfunction()
