set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.71.0 COMPONENTS system thread unit_test_framework REQUIRED)

IF(Boost_FOUND)
    INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
    LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
ENDIF(Boost_FOUND)

add_executable(
    MultiQueueAsyncProcessorTests
    mocks.h
    helpers.h
    test_params.h
    tests.cpp)

TARGET_LINK_LIBRARIES(
    MultiQueueAsyncProcessorTests
    ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    ${Boost_THREAD_LIBRARY})
