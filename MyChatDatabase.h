#pragma once

#include "MyChatChannel.h"
#include "sqlite3.h"

#include <string>
#include <vector>

class MyChatDatabase
{
public:
    MyChatDatabase() = default;
    ~MyChatDatabase();

    bool Open(const std::string& dbPath);
    void Close();
    void InitSchema();

    int  GetOrCreatePreset(const std::string& server, const std::string& charName);
    int  GetActivePresetId(const std::string& server, const std::string& charName);
    void SetActivePreset(const std::string& server, const std::string& charName, int presetId);
    void GetPresetList(const std::string& server, std::vector<PresetInfo>& outList);
    void SaveAsNewPreset(const std::string& server, const std::string& charName, const std::string& name, const MyChatSettings& settings);
    void CopyPreset(int sourceId, const std::string& newName);
    void RenamePreset(int presetId, const std::string& newName);
    void DeletePreset(int presetId);

    void LoadSettings(int presetId, MyChatSettings& outSettings);
    void SaveSettings(int presetId, const MyChatSettings& settings);

    void LoadGlobalSettings(const std::string& server, const std::string& charName, MyChatSettings& outSettings);
    void SaveGlobalSettings(const std::string& server, const std::string& charName, const MyChatSettings& settings);

    void LoadChannelOverrides(const std::string& server, const std::string& charName, MyChatSettings& outSettings);
    void SaveChannelOverride(const std::string& server, const std::string& charName, int channelId, int presetId);

    void SeedDefaultChannels(int presetId);

private:
    void ExecSQL(const char* sql);
    bool PrepareAndStep(const char* sql, sqlite3_stmt*& stmt);

    sqlite3* m_db = nullptr;
};
