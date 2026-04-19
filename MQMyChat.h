#pragma once

#include "MyChatChannel.h"

#include <mq/imgui/ConsoleWidget.h>
#include <Blech/Blech.h>

#include <chrono>
#include <unordered_map>

class MyChatDatabase;
struct lua_State;

struct ClaimedLine {
    std::chrono::steady_clock::time_point expireTime;
};

struct BlechEventData {
    int channelId = 0;
    int eventIndex = 0;
};

class MyChatEngine
{
public:
    void Initialize();
    void Shutdown();

    void LoadCharacterSettings();
    void SaveCharacterSettings();
    void UnloadCharacterSettings();

    void ProcessIncomingChat(const char* line, int color);
    void ProcessWriteChat(const char* line, int color);

    void SendToChannel(const std::string& channelName, const std::string& message);
    void CreateChannel(const std::string& name, int channelId = -1);
    int  GetNextChannelId() const;

    void RegisterBlechEvents();
    void UnregisterBlechEvents();

    void SortChannels();
    void PopulateDefaultChannels();
    void ApplyFontSizes();
    void SyncFontSizes();

    void CleanExpiredClaims();

    std::string SubstituteTokens(const std::string& pattern, const std::string& line) const;
    bool PatternFind(const std::string& text, const std::string& pattern) const;
    bool MatchFilter(const std::string& line, const ChatFilter& filter) const;

    MyChatSettings                       settings;
    std::shared_ptr<mq::imgui::ConsoleWidget> mainConsole;

    struct SortedChannel {
        int         id = 0;
        std::string name;
        int         tabOrder = 0;
    };
    std::vector<SortedChannel>           sortedChannels;

    std::unique_ptr<MyChatDatabase>      database;

    int                                  activePresetId = -1;
    std::string                          activePresetName;
    std::vector<PresetInfo>              presetList;

    std::string                          charName;
    std::string                          serverName;
    std::string                          dbPath;

    bool                                 showMainWindow = true;
    bool                                 showConfigGUI = false;
    bool                                 showEditGUI = false;
    int                                  editChannelId = 0;

    bool                                 enableSpam = false;

private:
    std::unique_ptr<Blech>               m_blech;
    std::unordered_map<unsigned int, BlechEventData> m_blechEventMap;

    std::unordered_map<std::string, ClaimedLine> m_claimedLines;

    static void CALLBACK BlechCallback(unsigned int id, void* pData, PBLECHVALUE pValues);
    static unsigned int CALLBACK BlechVarCallback(char* varName, char* value, size_t valueLen);

    void RouteToChannel(int channelId, int eventIndex, const char* line, int color);

    const char* m_currentLine = nullptr;
    int m_currentColor = 0;
    lua_State* m_luaState = nullptr;
};

extern MyChatEngine* g_chatEngine;
