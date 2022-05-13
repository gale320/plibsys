file(GLOB SRC ./*.c)
file(GLOB SRCPOSIX ../os/posix/*.c)

add_library(ztk SHARED ${SRC} ${SRCPOSIX})
target_link_libraries(ztk pthread dl rt) 