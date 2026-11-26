add_library(sivspell_lib 
    spell_corrector.cpp 
    lang_model.cpp 
    utils.cpp 
    alphabet.cpp 
    tokenizer.cpp 
    perfect_hash.cpp 
    bloom_filter.cpp
)

## hat-trie dependency
# 1. If the git submodule is checked out (third_party/hat-trie), use it.
# 2. Otherwise look for an installed tsl-hat-trie package (Nix, distro
#    packages, a parent project, ...).
set(TSL_HAT_TRIE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/hat-trie")
if(EXISTS "${TSL_HAT_TRIE_DIR}/include/tsl/htrie_hash.h")
    set(TSL_USE_SYSTEM_HAT_TRIE FALSE)
    target_include_directories(sivspell_lib PUBLIC
        "$<BUILD_INTERFACE:${TSL_HAT_TRIE_DIR}/include>")

    # and array-hash dependency!
    target_include_directories(sivspell_lib PUBLIC
        "$<BUILD_INTERFACE:${TSL_HAT_TRIE_DIR}/third_party/array-hash/include>")
    
else()
    set(TSL_USE_SYSTEM_HAT_TRIE TRUE)
    find_package(tsl-hat_trie CONFIG QUIET)
    if(NOT TARGET tsl::hat_trie)
        message(FATAL_ERROR
            "hat-trie was not found. Either run: git submodule update --init --recursive "
            "or install tsl-hat_trie and make it visible to CMake (CMAKE_PREFIX_PATH).")
    endif()
    target_link_libraries(sivspell_lib INTERFACE tsl::hat_trie)
endif()

## robin-map dependency
# 1. If the git submodule is checked out (third_party/robin-map), use it.
# 2. Otherwise look for an installed tsl-robin-map package (Nix, distro
#    packages, a parent project, ...).
set(TSL_ROBIN_MAP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/robin-map")
if(EXISTS "${TSL_ROBIN_MAP_DIR}/include/tsl/robin_map.h")
    set(TSL_USE_SYSTEM_ROBIN_MAP FALSE)
    target_include_directories(sivspell_lib PUBLIC
                               "$<BUILD_INTERFACE:${TSL_ROBIN_MAP_DIR}/include>")
else()
    set(TSL_USE_SYSTEM_ROBIN_MAP TRUE)
    find_package(tsl-robin-map CONFIG QUIET)
    if(NOT TARGET tsl::robin_map)
        message(FATAL_ERROR
            "robin-map was not found. Either run: git submodule update --init --recursive "
            "or install tsl-robin-map and make it visible to CMake (CMAKE_PREFIX_PATH).")
    endif()
    target_link_libraries(sivspell_lib INTERFACE tsl::robin_map)
endif()


find_package (Threads REQUIRED)
set(Boost_USE_MULTITHREADED ON)
find_package (Boost CONFIG REQUIRED locale)

target_link_libraries(sivspell_lib phf cityhash)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    target_link_libraries(sivspell_lib ${Boost_LIBRARIES})
endif()
