

target_link_libraries(sivspell_lib phf cityhash)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    target_link_libraries(sivspell_lib ${Boost_LIBRARIES})
endif()
