cmake_minimum_required(VERSION 3.20)

project(qrack_device)

if (NOT MSVC)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if (APPLE)
        set(QRACK_DIR "/usr/local/lib/qrack")
    else (APPLE)
        set(QRACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qrack/build")
    endif (APPLE)

    find_library(QRACK_LIB
        NAMES qrack libqrack
        PATHS "${QRACK_DIR}"
        NO_DEFAULT_PATH)

    if (NOT QRACK_LIB)
        message(FATAL_ERROR "Could not find libqrack in ${QRACK_DIR}")
    endif (NOT QRACK_LIB)

    message(STATUS "Found Qrack library: ${QRACK_LIB}")

    option(ENABLE_OPENCL "Use OpenCL optimizations" ON)
    find_package(OpenCL)
    if ((NOT OpenCL_FOUND) OR (APPLE AND NOT ((${CMAKE_SYSTEM_PROCESSOR} MATCHES "^amd") OR (${CMAKE_SYSTEM_PROCESSOR} MATCHES "^x86_64"))))
        set (ENABLE_OPENCL OFF)
    endif ((NOT OpenCL_FOUND) OR (APPLE AND NOT ((${CMAKE_SYSTEM_PROCESSOR} MATCHES "^amd") OR (${CMAKE_SYSTEM_PROCESSOR} MATCHES "^x86_64"))))

    message(STATUS "Use OpenCL: ${ENABLE_OPENCL}")

    add_library(qrack_device SHARED pennylane_qrack/qrack_device.cpp)
    if (APPLE)
        target_link_directories(qrack_device PUBLIC ${CMAKE_SOURCE_DIR}/catalyst/runtime/include /usr/local/lib/qrack)
        target_include_directories(qrack_device PUBLIC ${CMAKE_SOURCE_DIR}/catalyst/runtime/include /usr/local/include)
    else (APPLE)
        target_link_directories(qrack_device PUBLIC ${CMAKE_SOURCE_DIR}/catalyst/runtime/include ${CMAKE_SOURCE_DIR}/qrack/build)
        target_include_directories(qrack_device PUBLIC ${CMAKE_SOURCE_DIR}/catalyst/runtime/include ${CMAKE_SOURCE_DIR}/_qrack_include)
    endif (APPLE)

    if (ENABLE_OPENCL)
        if (APPLE)
            target_include_directories(qrack_device PUBLIC ${CMAKE_SOURCE_DIR}/qrack/build/_deps/opencl-headers-src/ ${CMAKE_SOURCE_DIR}/qrack/build/_deps/opencl-clhpp-src/include/)
            target_link_libraries(qrack_device PUBLIC ${QRACK_LIB} "-framework OpenCL")
        else (APPLE)
            target_link_libraries(qrack_device PUBLIC ${QRACK_LIB} OpenCL)
        endif (APPLE)
    else (ENABLE_OPENCL)
        target_link_libraries(qrack_device PUBLIC ${QRACK_LIB})
    endif (ENABLE_OPENCL)

    set_property(TARGET qrack_device PROPERTY POSITION_INDEPENDENT_CODE ON)
    install(TARGETS qrack_device LIBRARY DESTINATION pennylane_qrack)
endif (NOT MSVC)
