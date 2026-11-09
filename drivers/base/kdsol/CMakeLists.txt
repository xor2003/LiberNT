
spec2def(kdsol.dll kdsol.spec)

list(APPEND SOURCE
    kdsol.c
    ${REACTOS_SOURCE_DIR}/drivers/base/kdcom/kddll.c
    ${REACTOS_SOURCE_DIR}/drivers/base/kdcom/kdserial.c
    ${REACTOS_SOURCE_DIR}/drivers/base/kdcom/kddll.h)

add_library(kdsol MODULE
    ${SOURCE}
    kdsol.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kdsol.def)
add_pch(kdsol ${REACTOS_SOURCE_DIR}/drivers/base/kdcom/kddll.h SOURCE)

set_module_type(kdsol kerneldll ENTRYPOINT 0)
set_subsystem(kdsol native)
add_importlibs(kdsol ntoskrnl hal)
target_link_libraries(kdsol cportlib strtol)
add_dependencies(kdsol psdk bugcodes)
add_cd_file(TARGET kdsol DESTINATION reactos/system32 NO_CAB FOR all)
