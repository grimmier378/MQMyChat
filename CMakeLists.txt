include("../../src/Plugin.cmake")

find_package(unofficial-sqlite3 CONFIG REQUIRED)

set(MQMyChat_SOURCES
    MQMyChat.cpp
    MyChatDatabase.cpp
    MyChatRenderer.cpp
    MyChatTLO.cpp
)

set(MQMyChat_HEADERS
    MQMyChat.h
    MyChatChannel.h
    MyChatDatabase.h
    MyChatRenderer.h
    MyChatTLO.h
    Theme.h
)

add_library(MQMyChat SHARED
    ${MQMyChat_SOURCES}
    ${MQMyChat_HEADERS}
    MQMyChat.rc
    resource.h
)

set_target_properties(MQMyChat PROPERTIES FOLDER "plugins")

target_Plugin_props(MQMyChat)

target_link_libraries(MQMyChat PRIVATE
    unofficial::sqlite3::sqlite3
)
