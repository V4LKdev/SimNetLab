add_library(simd STATIC

)

target_include_directories(simd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(HWY CONFIG REQUIRED)

target_link_libraries(simd
        PUBLIC hwy
)