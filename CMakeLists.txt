find_package(Threads REQUIRED)

add_library(asyncpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher.cpp
)
target_link_libraries(asyncpp PRIVATE Threads::Threads)
target_include_directories(asyncpp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(BUILD_TEST)
    enable_testing()
    include(GoogleTest)
    if(HUNTER_ENABLED)
        hunter_add_package(GTest)
    endif()
    find_package(GTest REQUIRED)

    add_executable(asyncpp-test
        ${CMAKE_CURRENT_SOURCE_DIR}/test/defer.cpp
    )
    target_link_libraries(asyncpp-test PRIVATE asyncpp GTest::GTest GTest::Main Threads::Threads)
    gtest_add_tests(TARGET asyncpp-test)
endif()