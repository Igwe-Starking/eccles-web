# Works both as an ESP-IDF component and as a plain CMake library.
if(ESP_PLATFORM)
    idf_component_register(
        SRCS "src/eccles_web.c"
        INCLUDE_DIRS "src" "config"
    )
else()
    cmake_minimum_required(VERSION 3.13)
    project(EcclesWeb VERSION 1.0.0 LANGUAGES C)

    add_library(eccles_web src/eccles_web.c)
    add_library(eccles::web ALIAS eccles_web)
    target_include_directories(eccles_web PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/config)
    target_compile_features(eccles_web PUBLIC c_std_99)
    find_library(ECCLES_WEB_LIBM m)
    if(ECCLES_WEB_LIBM)
        target_link_libraries(eccles_web PUBLIC ${ECCLES_WEB_LIBM})
    endif()
endif()
