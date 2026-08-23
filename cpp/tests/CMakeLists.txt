include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(lob_tests
  sanity_test.cpp
  order_book_test.cpp
)
target_link_libraries(lob_tests PRIVATE lob_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(lob_tests)
