
enable_testing()

# Minimal test executable that links the project core library. Avoid external test frameworks to keep
# bootstrapping simple and robust on dev machines.
add_executable(unit_tests test_point_store.cpp)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(unit_tests PRIVATE scout_core_lib)

add_test(NAME UnitTests COMMAND unit_tests)
