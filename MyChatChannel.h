#pragma once

#include <mq/Plugin.h>

namespace mq::imgui { class ConsoleWidget; }

static constexpr int DEFAULT_MAX_BUFFER_LINES = 5000;

struct ChatFilter {
    int         filterIndex = 0;
    std::string filterString;
    MQColor     color = MQColor(255, 255, 255, 255);
    bool        enabled = true;
    bool        hidden = false;
};

struct ChatEvent {
    int                      eventIndex = 0;
    std::string              eventString;
    bool                     enabled = true;
    std::vector<ChatFilter>  filters;

    unsigned int             blechId = 0;
};

struct ChatChannel {
    int                      channelId = 0;
    std::string              name;
    bool                     enabled = true;
    std::string              echo = "/say";
    bool                     mainEnable = true;
    bool                     enableLinks = false;
    bool                     popOut = false;
    bool                     locked = false;
    float                    scale = 1.0f;
    int                      fontSize = 16;
    int                      tabOrder = 0;
    std::vector<ChatEvent>   events;

    std::shared_ptr<mq::imgui::ConsoleWidget> console;
    std::vector<std::string> commandHistory;
    int                      historyPos = -1;
    char                     inputBuffer[2048] = {};
};

struct PresetInfo {
    int         id = 0;
    std::string name;
    std::string server;
    std::string createdBy;
};

struct MyChatSettings {
    bool        windowLocked = false;
    bool        timeStamps = true;
    float       scale = 1.0f;
    int         themeIdx = 10;
    bool        doLinks = false;
    std::string mainEcho = "/say";
    int         mainFontSize = 16;
    bool        logCommands = false;
    bool        keyFocus = false;
    std::string keyName = "RightShift";
    bool        localEcho = true;
    bool        autoScroll = true;
    int         maxBufferLines = DEFAULT_MAX_BUFFER_LINES;

    std::map<int, ChatChannel> channels;
};

static constexpr int CHANNEL_SPAM = 9000;
static constexpr int CHANNEL_RESERVED_START = 9000;
static constexpr float CLAIMED_TTL = 0.5f;
static constexpr int MAX_COMMAND_HISTORY = 100;
static constexpr int MAX_CHANNEL_HISTORY = 10;
static constexpr float TAB_ORDER_SAVE_INTERVAL = 3.0f;
