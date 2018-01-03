cmake_minimum_required(VERSION 3.7)
project(siamese)

set(CMAKE_CXX_STANDARD 11)

# Dependency: GF256 library source files
set(GF256_LIB_SRCFILES
        gf256.cpp
        gf256.h)

# Dependency: Logger library source files
set(LOGGER_LIB_SRCFILES
        Logger.cpp
        Logger.h)

# Dependency: PacketAllocator library source files
set(PKTALLOC_LIB_SRCFILES
        PacketAllocator.cpp
        PacketAllocator.h)

# Siamese library source files
set(SIAMESE_LIB_SRCFILES
        siamese.cpp
        siamese.h
        SiameseCommon.cpp
        SiameseCommon.h
        SiameseDecoder.cpp
        SiameseDecoder.h
        SiameseEncoder.cpp
        SiameseEncoder.h
        SiameseSerializers.h
        SiameseTools.cpp
        SiameseTools.h)

add_library(gf256 ${GF256_LIB_SRCFILES})
add_library(logger ${LOGGER_LIB_SRCFILES})
add_library(pktalloc ${PKTALLOC_LIB_SRCFILES})
add_library(siamese ${SIAMESE_LIB_SRCFILES})

add_executable(unit_test tests/unit_test.cpp)
target_link_libraries(unit_test gf256 logger pktalloc siamese)

set(TESTS_GENTAB_PRIMES_SRCFILES
        tests/gentab_primes.cpp)

add_executable(gentab_primes ${TESTS_GENTAB_PRIMES_SRCFILES})
target_link_libraries(gentab_primes gf256 logger pktalloc siamese)

set(TESTS_ALLOCATOR_SRCFILES
        tests/test_allocator.cpp)

add_executable(test_allocator ${TESTS_ALLOCATOR_SRCFILES})
target_link_libraries(test_allocator gf256 logger pktalloc siamese)

set(TESTS_CUSTOM_BITSET_SRCFILES
        tests/test_custom_bitset.cpp)

add_executable(test_custom_bitset ${TESTS_CUSTOM_BITSET_SRCFILES})
target_link_libraries(test_custom_bitset gf256 logger pktalloc siamese)

set(TESTS_INVERT_RATE_SRCFILES
        tests/GF256Matrix.cpp
        tests/GF256Matrix.h
        tests/test_invert_rate.cpp)

add_executable(test_invert_rate ${TESTS_INVERT_RATE_SRCFILES})
target_link_libraries(test_invert_rate gf256 logger pktalloc siamese)

set(TESTS_RECOVERY_SORT_SRCFILES
        tests/test_recovery_sort.cpp)

add_executable(test_recovery_sort ${TESTS_RECOVERY_SORT_SRCFILES})
target_link_libraries(test_recovery_sort gf256 logger pktalloc siamese)

set(TESTS_SERIALIZERS_SRCFILES
        tests/test_serializers.cpp)

add_executable(test_serializers ${TESTS_SERIALIZERS_SRCFILES})
target_link_libraries(test_serializers gf256 logger pktalloc siamese)
