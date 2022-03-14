cmake_minimum_required(VERSION 3.14)

macro(default name)
  if(NOT DEFINED "${name}")
    set("${name}" "${ARGN}")
  endif()
endmacro()

default(SPELL_COMMAND codespell)
default(FIX NO)

set(flag "")
if(FIX)
  set(flag -w)
endif()

execute_process(
    COMMAND "${SPELL_COMMAND}" ${flag}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE result
)

if(result EQUAL "65")
  message(FATAL_ERROR "Run again with FIX=YES to fix these errors.")
elseif(result EQUAL "64")
  message(FATAL_ERROR "Spell checker printed the usage info. Bad arguments?")
elseif(NOT result EQUAL "0")
  message(FATAL_ERROR "Spell checker returned with ${result}")
endif()
