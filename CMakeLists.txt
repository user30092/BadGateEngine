cmake_minimum_required(VERSION 3.22.1)
project("badgate" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Portable sources: flat GLOB (не GLOB_RECURSE).
file(GLOB PORTABLE_SOURCES CONFIGURE_DEPENDS
    "core/*.cpp"
    "world/*.cpp"
    "entity/*.cpp"
    "input/*.cpp"
    "render/*.cpp"
    "ui/*.cpp"
    "net/*.cpp"
    "engine/*.cpp"
)

# ==========================================================================
if(ANDROID)
    file(GLOB ANDROID_SOURCES CONFIGURE_DEPENDS
        "engine/android/*.cpp"
        "glue/*.c"
    )

    add_library(${PROJECT_NAME} SHARED
        ${PORTABLE_SOURCES}
        ${ANDROID_SOURCES}
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

    target_link_libraries(${PROJECT_NAME}
        android log EGL GLESv3 jnigraphics z)

    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-fno-rtti>
    )
endif()

# ==========================================================================
if(NOT ANDROID)
    file(GLOB DESKTOP_SOURCES CONFIGURE_DEPENDS "engine/desktop/*.cpp")

    add_executable(${PROJECT_NAME}
        ${PORTABLE_SOURCES}
        ${DESKTOP_SOURCES}
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

    find_package(glfw3 CONFIG REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(Threads REQUIRED)
    find_package(ZLIB REQUIRED)

    target_link_libraries(${PROJECT_NAME} PRIVATE
        glfw
        GLEW::GLEW
        Threads::Threads
        ZLIB::ZLIB
        ${CMAKE_DL_LIBS}
    )

    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/fp:fast>
            $<$<CONFIG:Release>:/GR->
            /utf-8
        )
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:-O2>
            $<$<CONFIG:Release>:-ffast-math>
            $<$<CONFIG:Release>:-fno-rtti>
        )
    endif()

    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
    endif()
endif()

# ==========================================================================
# Dedicated server — без графики, только сокеты + std::thread.
# ==========================================================================
if(NOT ANDROID)
    add_executable(badgate_server
        "server/main_server.cpp"
        "net/socket.cpp"
        "world/chunk_worker_pool.cpp"
    )

    target_include_directories(badgate_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

    find_package(Threads REQUIRED)
    target_link_libraries(badgate_server PRIVATE Threads::Threads)

    if(WIN32)
        target_link_libraries(badgate_server PRIVATE ws2_32)
    endif()

    if(MSVC)
        target_compile_options(badgate_server PRIVATE
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/fp:fast>
            $<$<CONFIG:Release>:/GR->
            /utf-8
        )
    else()
        target_compile_options(badgate_server PRIVATE
            $<$<CONFIG:Release>:-O2>
            $<$<CONFIG:Release>:-ffast-math>
            $<$<CONFIG:Release>:-fno-rtti>
        )
    endif()
endif()