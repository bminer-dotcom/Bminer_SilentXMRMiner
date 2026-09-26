# Generate C++ header with embedded config JSON as hex-encoded bytes
# Usage: generate_embedded_config(INPUT_FILE OUTPUT_FILE)

function(generate_embedded_config INPUT_FILE OUTPUT_FILE)
    message(STATUS "Generating embedded config from ${INPUT_FILE}...")
    
    # Check if input file exists
    if(NOT EXISTS "${INPUT_FILE}")
        message(FATAL_ERROR "Embedded config file not found: ${INPUT_FILE}")
    endif()
    
    # Create output directory if it doesn't exist
    get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    
    # Read the JSON file as raw hex to avoid encoding issues
    file(READ "${INPUT_FILE}" CONFIG_JSON HEX)
    
    # Split hex string into pairs
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\1;" HEX_LIST "${CONFIG_JSON}")
    
    # Build the escaped string literal (each byte as \xNN) so the bminer DVM_STR
    # protector can encrypt the body in place. Every escape is followed by a backslash
    # (never a hex digit), so the greedy \x parser stays unambiguous.
    set(HEX_LITERAL "")
    foreach(HEX_BYTE ${HEX_LIST})
        if(HEX_BYTE AND NOT HEX_BYTE STREQUAL ";")
            string(APPEND HEX_LITERAL "\\x${HEX_BYTE}")
        endif()
    endforeach()
    
    # Create template with placeholder for the literal
    set(TEMPLATE_CONTENT 
"#pragma once

// Embedded configuration JSON (generated from embedded_config.json).
// Stored as an escaped string literal so the DVM_STR protector can encrypt the body in
// place (no plaintext residue in the binary). Decrypted at runtime via
// DVM_STR(EMBEDDED_CONFIG_LITERAL) in ConfigManager::LoadEmbeddedConfig().
#define EMBEDDED_CONFIG_LITERAL \"@HEX_LITERAL@\"
")
    
    # Write template file for configure_file
    set(TEMP_TEMPLATE "${OUTPUT_DIR}/config.h.in")
    file(WRITE "${TEMP_TEMPLATE}" "${TEMPLATE_CONTENT}")
    
    # Use configure_file to substitute variables
    configure_file("${TEMP_TEMPLATE}" "${OUTPUT_FILE}" @ONLY)
    
    message(STATUS "Generated embedded config header with DVM_STR literal: ${OUTPUT_FILE}")
endfunction()

