
find_program(GENTOKENS_PYTHON_EXECUTABLE NAMES python3 python REQUIRED NO_CMAKE_FIND_ROOT_PATH)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/liber_tokens.h
    COMMAND "${GENTOKENS_PYTHON_EXECUTABLE}" -I "${REACTOS_SOURCE_DIR}/sdk/tools/gentokens/gentokens.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tokens.json" "${CMAKE_CURRENT_BINARY_DIR}/liber_tokens.h"
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tokens.json ${REACTOS_SOURCE_DIR}/sdk/tools/gentokens/gentokens.py)

add_custom_target(liber_tokens DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/liber_tokens.h)
