# ctest driver for demo programs.
#   -DEXE=<program> [-DARGS="a;b"] [-DINPUT=<stdin file>] -DREGEX=<expected>
#   -DTIMEOUT=<seconds>
# Passes only if the program exits with status 0 within the timeout AND its
# output matches REGEX.
set(cmd ${EXE})
if(ARGS)
  list(APPEND cmd ${ARGS})
endif()
set(extra)
if(INPUT)
  set(extra INPUT_FILE ${INPUT})
endif()
execute_process(COMMAND ${cmd} ${extra}
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
                TIMEOUT ${TIMEOUT})
message("${out}")
if(err)
  message("[stderr]\n${err}")
endif()
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR "demo failed: exit status '${rc}'")
endif()
if(NOT out MATCHES "${REGEX}")
  message(FATAL_ERROR "demo output did not match '${REGEX}'")
endif()
