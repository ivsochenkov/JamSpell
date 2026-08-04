
add_executable(sivspell_web_server main.cpp)
if(WIN32)
  target_link_libraries(sivspell_web_server wsock32 ws2_32 sivspell_lib ${CMAKE_THREAD_LIBS_INIT})
else()
  target_link_libraries(sivspell_web_server sivspell_lib ${CMAKE_THREAD_LIBS_INIT})
endif()

