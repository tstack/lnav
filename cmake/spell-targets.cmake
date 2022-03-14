set(SPELL_COMMAND codespell CACHE STRING "Spell checker to use")

add_custom_target(
    spell-check
    COMMAND "${CMAKE_COMMAND}"
    -D "SPELL_COMMAND=${SPELL_COMMAND}"
    -P "${PROJECT_SOURCE_DIR}/cmake/spell.cmake"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Checking spelling"
    VERBATIM
)

add_custom_target(
    spell-fix
    COMMAND "${CMAKE_COMMAND}"
    -D "SPELL_COMMAND=${SPELL_COMMAND}"
    -D FIX=YES
    -P "${PROJECT_SOURCE_DIR}/cmake/spell.cmake"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Fixing spelling errors"
    VERBATIM
)
