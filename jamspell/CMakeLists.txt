add_library(sivspell_lib 
spell_corrector.cpp 
lang_model.cpp 
utils.cpp 
alphabet.cpp 
tokenizer.cpp 
perfect_hash.cpp 
bloom_filter.cpp
)

target_link_libraries(sivspell_lib phf cityhash)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    target_link_libraries(sivspell_lib ${Boost_LIBRARIES})
endif()
