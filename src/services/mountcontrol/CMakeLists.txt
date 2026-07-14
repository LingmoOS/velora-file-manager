cmake_minimum_required(VERSION 3.10)

project(lingmo-filemanager-mountcontrol)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Include service configuration
include(${CMAKE_CURRENT_SOURCE_DIR}/dependencies.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../DFMServiceCommon.cmake)

FILE(GLOB FILEOPERATIONS_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.xml"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.policy"
    )

add_library(${PROJECT_NAME}
    SHARED
    ${FILEOPERATIONS_FILES}
)

set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ../../)

# Apply service configuration using shared dependencies
dfm_setup_mountcontrol_dependencies(${PROJECT_NAME} "mountcontrol")

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR}/lingmo-service-manager/)
install(FILES ${PROJECT_NAME}.json DESTINATION share/lingmo-service-manager/system/)
install(FILES org.lingmo.filemanager.mountcontrol.conf DESTINATION share/dbus-1/system.d/)

set(PolicyDir "${CMAKE_INSTALL_PREFIX}/share/polkit-1/actions")
install(FILES org.lingmo.filemanager.mountcontrol.policy DESTINATION ${PolicyDir})

INSTALL_DBUS_SERVICE(org.lingmo.Filemanager.MountControl root)

# Install systemd drop-in configuration for security hardening
# This adds security restrictions without modifying the shared service template
install(FILES
    systemd/security-hardening.conf
    DESTINATION
    lib/systemd/system/deepin-service-plugin@org.lingmo.Filemanager.MountControl.service.d/
)
