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
    int  CreateBlankPreset(const std::string& server, const std::string& charName, const std::string& name);

    void LoadSettings(int presetId, MyChatSettings& outSettings);
    void SaveSettings(int presetId, const MyChatSettings& settings);

    void LoadGlobalSettings(const std::string& server, const std::string& charName, MyChatSettings& outSettings);
    void SaveGlobalSettings(const std::string& server, const std::string& charName, const MyChatSettings& settings);

    void LoadCharChannelOverrides(const std::string& server, const std::string& charName, int presetId, MyChatSettings& outSettings);
    void SaveCharChannelOverrides(const std::string& server, const std::string& charName, int presetId, const MyChatSettings& settings);

    void SeedDefaultChannels(int presetId);

private:
    void ExecSQL(const char* sql);
    bool PrepareAndStep(const char* sql, sqlite3_stmt*& stmt);
    int  GetSchemaVersion();
    void SetSchemaVersion(int version);
    void RunMigrations();

    sqlite3* m_db = nullptr;
};
