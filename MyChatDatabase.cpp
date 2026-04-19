#include "MyChatDatabase.h"

#include <mq/Plugin.h>

MyChatDatabase::~MyChatDatabase()
{
    Close();
}

bool MyChatDatabase::Open(const std::string& dbPath)
{
    if (m_db)
        Close();

    int rc = sqlite3_open_v2(dbPath.c_str(), &m_db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_WAL, nullptr);
    if (rc != SQLITE_OK)
    {
        WriteChatf("\ar[MyChat] Failed to open database: %s", sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    ExecSQL("PRAGMA journal_mode=WAL");
    ExecSQL("PRAGMA foreign_keys=ON");
    ExecSQL("PRAGMA busy_timeout=3000");

    return true;
}

void MyChatDatabase::Close()
{
    if (m_db)
    {
        ExecSQL("PRAGMA wal_checkpoint");
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

void MyChatDatabase::ExecSQL(const char* sql)
{
    if (!m_db)
        return;

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        WriteChatf("\ar[MyChat] SQL error: %s", errMsg ? errMsg : "unknown");
        if (errMsg)
            sqlite3_free(errMsg);
    }
}

bool MyChatDatabase::PrepareAndStep(const char* sql, sqlite3_stmt*& stmt)
{
    if (!m_db)
        return false;

    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        WriteChatf("\ar[MyChat] Prepare failed: %s", sqlite3_errmsg(m_db));
        stmt = nullptr;
        return false;
    }
    return true;
}

void MyChatDatabase::InitSchema()
{
    ExecSQL(
        "CREATE TABLE IF NOT EXISTS global_settings ("
        "char_name TEXT NOT NULL, server TEXT NOT NULL DEFAULT '', "
        "key TEXT NOT NULL, value TEXT NOT NULL, "
        "PRIMARY KEY (char_name, server, key))"
    );

    ExecSQL(
        "CREATE TABLE IF NOT EXISTS presets ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, preset_name TEXT NOT NULL, "
        "server TEXT NOT NULL DEFAULT '', description TEXT DEFAULT '', "
        "created_by TEXT NOT NULL, "
        "created_at TEXT NOT NULL DEFAULT (datetime('now')), "
        "updated_at TEXT NOT NULL DEFAULT (datetime('now')), "
        "UNIQUE(preset_name, server))"
    );

    ExecSQL(
        "CREATE TABLE IF NOT EXISTS channels ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, preset_id INTEGER NOT NULL, "
        "channel_id INTEGER NOT NULL, name TEXT NOT NULL DEFAULT 'New', "
        "enabled INTEGER NOT NULL DEFAULT 0, echo TEXT NOT NULL DEFAULT '/say', "
        "main_enable INTEGER NOT NULL DEFAULT 1, enable_links INTEGER NOT NULL DEFAULT 0, "
        "pop_out INTEGER NOT NULL DEFAULT 0, locked INTEGER NOT NULL DEFAULT 0, "
        "scale REAL NOT NULL DEFAULT 1.0, main_font_size INTEGER NOT NULL DEFAULT 16, "
        "tab_order INTEGER NOT NULL DEFAULT 0, "
        "UNIQUE(preset_id, channel_id), "
        "FOREIGN KEY (preset_id) REFERENCES presets(id) ON DELETE CASCADE)"
    );

    ExecSQL(
        "CREATE TABLE IF NOT EXISTS events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, channel_row_id INTEGER NOT NULL, "
        "event_index INTEGER NOT NULL, event_string TEXT NOT NULL DEFAULT 'new', "
        "enabled INTEGER NOT NULL DEFAULT 1, "
        "UNIQUE(channel_row_id, event_index), "
        "FOREIGN KEY (channel_row_id) REFERENCES channels(id) ON DELETE CASCADE)"
    );

    ExecSQL(
        "CREATE TABLE IF NOT EXISTS filters ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, event_row_id INTEGER NOT NULL, "
        "filter_index INTEGER NOT NULL, filter_string TEXT NOT NULL DEFAULT '', "
        "color_r REAL NOT NULL DEFAULT 1.0, color_g REAL NOT NULL DEFAULT 1.0, "
        "color_b REAL NOT NULL DEFAULT 1.0, color_a REAL NOT NULL DEFAULT 1.0, "
        "enabled INTEGER NOT NULL DEFAULT 1, hidden INTEGER NOT NULL DEFAULT 0, "
        "UNIQUE(event_row_id, filter_index), "
        "FOREIGN KEY (event_row_id) REFERENCES events(id) ON DELETE CASCADE)"
    );

    ExecSQL(
        "CREATE TABLE IF NOT EXISTS char_active_preset ("
        "char_name TEXT NOT NULL, server TEXT NOT NULL DEFAULT '', "
        "preset_id INTEGER NOT NULL, "
        "PRIMARY KEY (char_name, server), "
        "FOREIGN KEY (preset_id) REFERENCES presets(id) ON DELETE CASCADE)"
    );

    ExecSQL(
        "CREATE TABLE IF NOT EXISTS char_channel_overrides ("
        "char_name TEXT NOT NULL, server TEXT NOT NULL DEFAULT '', "
        "channel_id INTEGER NOT NULL, preset_id INTEGER NOT NULL, "
        "PRIMARY KEY (char_name, server, channel_id), "
        "FOREIGN KEY (preset_id) REFERENCES presets(id) ON DELETE CASCADE)"
    );

    ExecSQL("CREATE INDEX IF NOT EXISTS idx_channels_preset ON channels(preset_id)");
    ExecSQL("CREATE INDEX IF NOT EXISTS idx_events_channel ON events(channel_row_id)");
    ExecSQL("CREATE INDEX IF NOT EXISTS idx_filters_event ON filters(event_row_id)");
}

int MyChatDatabase::GetActivePresetId(const std::string& server, const std::string& charName)
{
    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep("SELECT preset_id FROM char_active_preset WHERE char_name=? AND server=?", stmt))
        return -1;

    sqlite3_bind_text(stmt, 1, charName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, server.c_str(), -1, SQLITE_TRANSIENT);

    int presetId = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        presetId = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return presetId;
}

void MyChatDatabase::SetActivePreset(const std::string& server, const std::string& charName, int presetId)
{
    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep(
        "INSERT OR REPLACE INTO char_active_preset (char_name, server, preset_id) VALUES (?, ?, ?)", stmt))
        return;

    sqlite3_bind_text(stmt, 1, charName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, server.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, presetId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int MyChatDatabase::GetOrCreatePreset(const std::string& server, const std::string& charName)
{
    int presetId = GetActivePresetId(server, charName);
    if (presetId > 0)
        return presetId;

    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep(
        "INSERT OR IGNORE INTO presets (preset_name, server, created_by) VALUES ('Default', ?, ?)", stmt))
        return -1;

    sqlite3_bind_text(stmt, 1, server.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, charName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    stmt = nullptr;
    if (!PrepareAndStep("SELECT id FROM presets WHERE preset_name='Default' AND server=?", stmt))
        return -1;

    sqlite3_bind_text(stmt, 1, server.c_str(), -1, SQLITE_TRANSIENT);

    presetId = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        presetId = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);

    if (presetId > 0)
        SetActivePreset(server, charName, presetId);

    return presetId;
}

void MyChatDatabase::GetPresetList(const std::string& server, std::vector<PresetInfo>& outList)
{
    outList.clear();

    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep("SELECT id, preset_name, server, created_by FROM presets WHERE server=?", stmt))
        return;

    sqlite3_bind_text(stmt, 1, server.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        PresetInfo info;
        info.id = sqlite3_column_int(stmt, 0);
        info.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.server = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.createdBy = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        outList.push_back(std::move(info));
    }

    sqlite3_finalize(stmt);
}

void MyChatDatabase::SaveAsNewPreset(const std::string& server, const std::string& charName,
    const std::string& name, const MyChatSettings& settings)
{
    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep(
        "INSERT INTO presets (preset_name, server, created_by) VALUES (?, ?, ?)", stmt))
        return;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, server.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, charName.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        WriteChatf("\ar[MyChat] Failed to create preset '%s': %s", name.c_str(), sqlite3_errmsg(m_db));
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_finalize(stmt);

    int newId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
    SaveSettings(newId, settings);
}

void MyChatDatabase::CopyPreset(int sourceId, const std::string& newName)
{
    MyChatSettings settings;
    LoadSettings(sourceId, settings);

    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep(
        "INSERT INTO presets (preset_name, server, created_by) "
        "SELECT ?, server, created_by FROM presets WHERE id=?", stmt))
        return;

    sqlite3_bind_text(stmt, 1, newName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, sourceId);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        WriteChatf("\ar[MyChat] Failed to copy preset: %s", sqlite3_errmsg(m_db));
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_finalize(stmt);

    int newId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
    SaveSettings(newId, settings);
}

void MyChatDatabase::RenamePreset(int presetId, const std::string& newName)
{
    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep("UPDATE presets SET preset_name=?, updated_at=datetime('now') WHERE id=?", stmt))
        return;

    sqlite3_bind_text(stmt, 1, newName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, presetId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void MyChatDatabase::DeletePreset(int presetId)
{
    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep("DELETE FROM presets WHERE id=?", stmt))
        return;

    sqlite3_bind_int(stmt, 1, presetId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void MyChatDatabase::LoadSettings(int presetId, MyChatSettings& outSettings)
{
    outSettings.channels.clear();

    sqlite3_stmt* chanStmt = nullptr;
    if (!PrepareAndStep(
        "SELECT id, channel_id, name, enabled, echo, main_enable, enable_links, "
        "pop_out, locked, scale, main_font_size, tab_order "
        "FROM channels WHERE preset_id=?", chanStmt))
        return;

    sqlite3_bind_int(chanStmt, 1, presetId);

    while (sqlite3_step(chanStmt) == SQLITE_ROW)
    {
        int rowId = sqlite3_column_int(chanStmt, 0);
        int channelId = sqlite3_column_int(chanStmt, 1);

        ChatChannel chan;
        chan.channelId = channelId;
        chan.name = reinterpret_cast<const char*>(sqlite3_column_text(chanStmt, 2));
        chan.enabled = sqlite3_column_int(chanStmt, 3) != 0;
        chan.echo = reinterpret_cast<const char*>(sqlite3_column_text(chanStmt, 4));
        chan.mainEnable = sqlite3_column_int(chanStmt, 5) != 0;
        chan.enableLinks = sqlite3_column_int(chanStmt, 6) != 0;
        chan.popOut = sqlite3_column_int(chanStmt, 7) != 0;
        chan.locked = sqlite3_column_int(chanStmt, 8) != 0;
        chan.scale = static_cast<float>(sqlite3_column_double(chanStmt, 9));
        chan.fontSize = sqlite3_column_int(chanStmt, 10);
        chan.tabOrder = sqlite3_column_int(chanStmt, 11);

        sqlite3_stmt* evtStmt = nullptr;
        if (PrepareAndStep(
            "SELECT id, event_index, event_string, enabled FROM events WHERE channel_row_id=?", evtStmt))
        {
            sqlite3_bind_int(evtStmt, 1, rowId);

            while (sqlite3_step(evtStmt) == SQLITE_ROW)
            {
                int evtRowId = sqlite3_column_int(evtStmt, 0);

                ChatEvent evt;
                evt.eventIndex = sqlite3_column_int(evtStmt, 1);
                evt.eventString = reinterpret_cast<const char*>(sqlite3_column_text(evtStmt, 2));
                evt.enabled = sqlite3_column_int(evtStmt, 3) != 0;

                sqlite3_stmt* filtStmt = nullptr;
                if (PrepareAndStep(
                    "SELECT filter_index, filter_string, color_r, color_g, color_b, color_a, "
                    "enabled, hidden FROM filters WHERE event_row_id=?", filtStmt))
                {
                    sqlite3_bind_int(filtStmt, 1, evtRowId);

                    while (sqlite3_step(filtStmt) == SQLITE_ROW)
                    {
                        ChatFilter filt;
                        filt.filterIndex = sqlite3_column_int(filtStmt, 0);
                        filt.filterString = reinterpret_cast<const char*>(sqlite3_column_text(filtStmt, 1));

                        float r = static_cast<float>(sqlite3_column_double(filtStmt, 2));
                        float g = static_cast<float>(sqlite3_column_double(filtStmt, 3));
                        float b = static_cast<float>(sqlite3_column_double(filtStmt, 4));
                        float a = static_cast<float>(sqlite3_column_double(filtStmt, 5));
                        filt.color = MQColor(
                            static_cast<uint8_t>(r * 255.0f),
                            static_cast<uint8_t>(g * 255.0f),
                            static_cast<uint8_t>(b * 255.0f),
                            static_cast<uint8_t>(a * 255.0f)
                        );

                        filt.enabled = sqlite3_column_int(filtStmt, 6) != 0;
                        filt.hidden = sqlite3_column_int(filtStmt, 7) != 0;

                        evt.filters.push_back(std::move(filt));
                    }
                    sqlite3_finalize(filtStmt);
                }

                chan.events.push_back(std::move(evt));
            }
            sqlite3_finalize(evtStmt);
        }

        outSettings.channels[channelId] = std::move(chan);
    }

    sqlite3_finalize(chanStmt);
}

void MyChatDatabase::SaveSettings(int presetId, const MyChatSettings& settings)
{
    ExecSQL("BEGIN TRANSACTION");

    sqlite3_stmt* stmt = nullptr;
    if (PrepareAndStep("DELETE FROM channels WHERE preset_id=?", stmt))
    {
        sqlite3_bind_int(stmt, 1, presetId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    for (const auto& [channelId, chan] : settings.channels)
    {
        stmt = nullptr;
        if (!PrepareAndStep(
            "INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, "
            "enable_links, pop_out, locked, scale, main_font_size, tab_order) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", stmt))
            continue;

        sqlite3_bind_int(stmt, 1, presetId);
        sqlite3_bind_int(stmt, 2, channelId);
        sqlite3_bind_text(stmt, 3, chan.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, chan.enabled ? 1 : 0);
        sqlite3_bind_text(stmt, 5, chan.echo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, chan.mainEnable ? 1 : 0);
        sqlite3_bind_int(stmt, 7, chan.enableLinks ? 1 : 0);
        sqlite3_bind_int(stmt, 8, chan.popOut ? 1 : 0);
        sqlite3_bind_int(stmt, 9, chan.locked ? 1 : 0);
        sqlite3_bind_double(stmt, 10, static_cast<double>(chan.scale));
        sqlite3_bind_int(stmt, 11, chan.fontSize);
        sqlite3_bind_int(stmt, 12, chan.tabOrder);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            WriteChatf("\ar[MyChat] Failed to insert channel %d: %s", channelId, sqlite3_errmsg(m_db));
            sqlite3_finalize(stmt);
            continue;
        }
        sqlite3_finalize(stmt);

        int chanRowId = static_cast<int>(sqlite3_last_insert_rowid(m_db));

        for (const auto& evt : chan.events)
        {
            stmt = nullptr;
            if (!PrepareAndStep(
                "INSERT INTO events (channel_row_id, event_index, event_string, enabled) "
                "VALUES (?, ?, ?, ?)", stmt))
                continue;

            sqlite3_bind_int(stmt, 1, chanRowId);
            sqlite3_bind_int(stmt, 2, evt.eventIndex);
            sqlite3_bind_text(stmt, 3, evt.eventString.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, evt.enabled ? 1 : 0);

            if (sqlite3_step(stmt) != SQLITE_DONE)
            {
                sqlite3_finalize(stmt);
                continue;
            }
            sqlite3_finalize(stmt);

            int evtRowId = static_cast<int>(sqlite3_last_insert_rowid(m_db));

            for (const auto& filt : evt.filters)
            {
                stmt = nullptr;
                if (!PrepareAndStep(
                    "INSERT INTO filters (event_row_id, filter_index, filter_string, "
                    "color_r, color_g, color_b, color_a, enabled, hidden) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", stmt))
                    continue;

                sqlite3_bind_int(stmt, 1, evtRowId);
                sqlite3_bind_int(stmt, 2, filt.filterIndex);
                sqlite3_bind_text(stmt, 3, filt.filterString.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 4, static_cast<double>(filt.color.Red) / 255.0);
                sqlite3_bind_double(stmt, 5, static_cast<double>(filt.color.Green) / 255.0);
                sqlite3_bind_double(stmt, 6, static_cast<double>(filt.color.Blue) / 255.0);
                sqlite3_bind_double(stmt, 7, static_cast<double>(filt.color.Alpha) / 255.0);
                sqlite3_bind_int(stmt, 8, filt.enabled ? 1 : 0);
                sqlite3_bind_int(stmt, 9, filt.hidden ? 1 : 0);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    stmt = nullptr;
    if (PrepareAndStep("UPDATE presets SET updated_at=datetime('now') WHERE id=?", stmt))
    {
        sqlite3_bind_int(stmt, 1, presetId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    ExecSQL("COMMIT");
}

void MyChatDatabase::LoadGlobalSettings(const std::string& server, const std::string& charName,
    MyChatSettings& outSettings)
{
    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep("SELECT key, value FROM global_settings WHERE char_name=? AND server=?", stmt))
        return;

    sqlite3_bind_text(stmt, 1, charName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, server.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

        if (key == "locked")
            outSettings.windowLocked = (value == "1" || value == "true");
        else if (key == "timeStamps")
            outSettings.timeStamps = (value == "1" || value == "true");
        else if (key == "Scale")
            outSettings.scale = std::stof(value);
        else if (key == "themeIdx")
            outSettings.themeIdx = std::stoi(value);
        else if (key == "doLinks")
            outSettings.doLinks = (value == "1" || value == "true");
        else if (key == "mainEcho")
            outSettings.mainEcho = value;
        else if (key == "MainFontSize")
            outSettings.mainFontSize = std::stoi(value);
        else if (key == "LogCommands")
            outSettings.logCommands = (value == "1" || value == "true");
        else if (key == "keyFocus")
            outSettings.keyFocus = (value == "1" || value == "true");
        else if (key == "keyName")
            outSettings.keyName = value;
        else if (key == "localEcho")
            outSettings.localEcho = (value == "1" || value == "true");
        else if (key == "autoScroll")
            outSettings.autoScroll = (value == "1" || value == "true");
    }

    sqlite3_finalize(stmt);
}

void MyChatDatabase::SaveGlobalSettings(const std::string& server, const std::string& charName,
    const MyChatSettings& settings)
{
    auto saveKV = [&](const char* key, const std::string& value) {
        sqlite3_stmt* stmt = nullptr;
        if (!PrepareAndStep(
            "INSERT OR REPLACE INTO global_settings (char_name, server, key, value) VALUES (?, ?, ?, ?)", stmt))
            return;
        sqlite3_bind_text(stmt, 1, charName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, server.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, key, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    };

    ExecSQL("BEGIN TRANSACTION");

    saveKV("locked", settings.windowLocked ? "1" : "0");
    saveKV("timeStamps", settings.timeStamps ? "1" : "0");
    saveKV("Scale", fmt::format("{:.2f}", settings.scale));
    saveKV("themeIdx", fmt::format("{}", settings.themeIdx));
    saveKV("doLinks", settings.doLinks ? "1" : "0");
    saveKV("mainEcho", settings.mainEcho);
    saveKV("MainFontSize", fmt::format("{}", settings.mainFontSize));
    saveKV("LogCommands", settings.logCommands ? "1" : "0");
    saveKV("keyFocus", settings.keyFocus ? "1" : "0");
    saveKV("keyName", settings.keyName);
    saveKV("localEcho", settings.localEcho ? "1" : "0");
    saveKV("autoScroll", settings.autoScroll ? "1" : "0");

    ExecSQL("COMMIT");
}

void MyChatDatabase::LoadChannelOverrides(const std::string& server, const std::string& charName,
    MyChatSettings& outSettings)
{
    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep(
        "SELECT channel_id, preset_id FROM char_channel_overrides WHERE char_name=? AND server=?", stmt))
        return;

    sqlite3_bind_text(stmt, 1, charName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, server.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int channelId = sqlite3_column_int(stmt, 0);
        int overridePresetId = sqlite3_column_int(stmt, 1);

        MyChatSettings overrideSettings;
        LoadSettings(overridePresetId, overrideSettings);

        auto it = overrideSettings.channels.find(channelId);
        if (it != overrideSettings.channels.end())
            outSettings.channels[channelId] = std::move(it->second);
    }

    sqlite3_finalize(stmt);
}

void MyChatDatabase::SaveChannelOverride(const std::string& server, const std::string& charName,
    int channelId, int presetId)
{
    sqlite3_stmt* stmt = nullptr;
    if (!PrepareAndStep(
        "INSERT OR REPLACE INTO char_channel_overrides (char_name, server, channel_id, preset_id) "
        "VALUES (?, ?, ?, ?)", stmt))
        return;

    sqlite3_bind_text(stmt, 1, charName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, server.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, channelId);
    sqlite3_bind_int(stmt, 4, presetId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}
void MyChatDatabase::SeedDefaultChannels(int presetId)
{
    if (!m_db) return;
    ExecSQL("BEGIN TRANSACTION");
    auto run = [&](const std::string& sql) { ExecSQL(sql.c_str()); };

    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 0, 'Consider', 1, '/say', 1, 0, 0, 0, 1.0, 16, 2)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 0), 1, '#*#scowls at you, ready to attack --#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 0 AND e.event_index = 1), 0, '', 0.032515, 0.371745, 0.776744, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 0), 2, '#*#glares at you threateningly --#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 0 AND e.event_index = 2), 0, '', 0.189171, 0.841860, 0.066566, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 0), 3, '#*#regards you indifferently --#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 0 AND e.event_index = 3), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 0), 4, '#*#regards you as an ally --#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 0 AND e.event_index = 4), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 0), 5, '#*#looks upon you warmly --#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 0 AND e.event_index = 5), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 0), 6, '#*#glowers at you dubiously --#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 0 AND e.event_index = 6), 0, '', 0.962791, 0.040303, 0.040303, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 0), 7, '#*#kindly considers you --#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 0 AND e.event_index = 7), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 0), 8, '#*#looks your way apprehensively --#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 0 AND e.event_index = 8), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 0), 9, '#*#judges you amiably --#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 0 AND e.event_index = 9), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 2, 'Combat', 1, '/say', 0, 0, 0, 0, 1.0, 16, 3)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 1, '#*# Heals #*# for #*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 1), 0, '', 0.000000, 0.872038, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 2, '#*#crush#*#point#*# of damage#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 2), 0, '', 0.000000, 0.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 2), 1, '^You', 0.971654, 0.542441, 0.995261, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 2), 2, '^N3', 0.772152, 0.221546, 0.221546, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 2), 3, '^GP1', 0.105272, 0.423470, 0.924051, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 2), 4, '^PT3', 0.318861, 0.854829, 0.862559, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 3, '#*# healed #*# for #*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 3), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 3), 1, 'GP1', 0.000000, 0.672986, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 4, '#*# kick#*#point#*# of damage#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 4), 0, '', 1.000000, 0.000000, 0.000000, 0.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 4), 1, '^You', 0.972549, 0.541176, 0.996078, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 4), 2, '^N3', 0.897674, 0.342369, 0.342369, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 4), 3, '^GP1', 0.721291, 0.436433, 0.869198, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 4), 4, '^PT3', 0.317647, 0.854902, 0.862745, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 5, '#*# bite#*#point#*# of damage#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 5), 0, '', 1.000000, 0.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 5), 1, '^N3', 1.000000, 0.388626, 0.388626, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 6, '#*#non-melee#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 6), 0, '', 0.978903, 0.680678, 0.218911, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 6), 1, '^GP1', 0.974684, 0.404379, 0.069914, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 6), 2, '^You', 0.784349, 0.853081, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 6), 3, '^N3', 1.000000, 0.142180, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 6), 4, '^PT3', 0.317647, 0.854902, 0.862745, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 7, '#*# bash#*#point#*# of damage#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 7), 0, '', 0.075949, 1.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 7), 1, '^You', 0.972549, 0.541176, 0.996078, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 7), 2, '^GP1', 0.283147, 0.383385, 0.905213, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 7), 3, '^N3', 0.890995, 0.430718, 0.430718, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 7), 4, '^PT3', 0.317647, 0.854902, 0.862745, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 8, '#*# hits#*#point#*# of damage#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 8), 0, '', 0.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 8), 1, '^GP1', 0.867299, 0.720707, 0.147975, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 8), 2, '^N3', 0.933649, 0.327441, 0.327441, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 8), 3, '^PT3', 0.317647, 0.854902, 0.862745, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 9, '#*#You hit #*# for #*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 9), 0, '', 0.972549, 0.541176, 0.996078, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 10, '#*#pierce#*#point#*# of damage#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 10), 0, '', 0.000000, 0.000000, 0.000000, 0.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 10), 1, '^You', 0.972549, 0.541176, 0.996078, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 10), 2, '^N3', 0.957346, 0.313066, 0.313066, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 10), 3, '^GP1', 0.000000, 0.303318, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 10), 4, '^PT3', 0.317647, 0.854902, 0.862745, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 11, '#*#backstabs #*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 11), 0, '', 1.000000, 0.973822, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 11), 1, '^N3', 1.000000, 0.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 11), 2, '^GP1', 1.000000, 0.995261, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 11), 3, '^You', 0.691943, 1.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 11), 4, '^PT3', 0.949741, 0.971564, 0.050650, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 12, '#*# but miss#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 12), 0, '', 0.983715, 0.881517, 1.000000, 0.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 12), 1, '^GP1', 0.654028, 0.654028, 0.654028, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 12), 2, '^PT3', 0.696682, 0.670268, 0.670268, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 12), 3, '^You', 0.654028, 0.638530, 0.638530, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 12), 4, '^N3', 0.511848, 0.504571, 0.504571, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 13, '#*#slash#*#point#*# of damage#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 13), 0, '', 1.000000, 1.000000, 0.000000, 0.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 13), 1, '^You', 0.972549, 0.541176, 0.996078, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 13), 2, '^GP1', 0.198846, 0.584398, 0.924051, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 13), 3, '^N3', 0.981043, 0.306866, 0.306866, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 13), 4, '^PT3', 0.317647, 0.854902, 0.862745, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 2), 14, '#*#trike through#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 2 AND e.event_index = 14), 0, '', 0.000000, 1.000000, 0.483721, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 3, 'Crits', 1, '/say', 0, 0, 0, 0, 1.0, 16, 4)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 3), 1, '#*#ASSASSINATE#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 1), 0, '', 0.000000, 1.000000, 0.900474, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 3), 2, '#*#Finishing Blow#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 2), 0, '', 0.935107, 0.430380, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 2), 1, '^TK1', 0.126582, 1.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 2), 2, '^GP1', 0.000000, 0.553447, 0.729858, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 3), 3, '#*#crippling blow#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 3), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 3), 1, '^GP1', 0.981043, 0.770405, 0.418454, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 3), 2, '^M3', 0.490799, 0.962085, 0.082074, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 3), 3, '^PT3', 0.981043, 0.954688, 0.553289, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 3), 4, '#*#xceptional#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 4), 0, '', 0.000000, 1.000000, 0.835443, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 4), 1, '^You', 0.025316, 1.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 4), 2, '^GP1', 0.000000, 1.000000, 0.701422, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 4), 3, '^PT3', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 3), 5, '#*#critical hit#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 5), 0, '', 1.000000, 0.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 5), 1, '^GP1', 1.000000, 0.909953, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 5), 2, '^M1', 1.000000, 0.633751, 0.123223, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 5), 3, '^PT3', 0.971564, 0.858741, 0.538735, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 3), 6, '#*#critical blast#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 6), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 6), 1, '^You', 0.624836, 0.293614, 0.815166, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 6), 2, '^GP1', 1.000000, 0.488152, 0.983019, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 3 AND e.event_index = 6), 3, '^PT3', 0.865986, 0.593989, 0.971564, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 4, 'Exp AA pts', 1, '/say', 1, 0, 0, 0, 1.0, 16, 12)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 4), 1, '#*#You have gained #*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 4 AND e.event_index = 1), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 4 AND e.event_index = 1), 1, 'experience', 1.000000, 0.962025, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 4 AND e.event_index = 1), 2, 'ability', 0.925581, 0.420832, 0.133456, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 4), 2, '#*# gained #*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 4 AND e.event_index = 2), 0, '', 1.000000, 0.000000, 0.000000, 0.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 4 AND e.event_index = 2), 1, 'M3', 0.094787, 1.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 4 AND e.event_index = 2), 2, 'GP1', 0.000000, 0.531646, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 5, 'Faction', 1, '/say', 1, 0, 0, 0, 1.0, 16, 6)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 5), 1, '#*#Your faction standing with#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 5 AND e.event_index = 1), 0, '', 0.857913, 0.890295, 0.037565, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 5 AND e.event_index = 1), 1, '[-]%d+%.?$', 0.915612, 0.358753, 0.169987, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 5 AND e.event_index = 1), 2, '%d+%.?$', 0.350213, 0.907173, 0.107177, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 6, 'Guild', 0, '/gu', 1, 0, 0, 0, 1.0, 16, 11)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 6), 1, '#*# guild,#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 6 AND e.event_index = 1), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 6 AND e.event_index = 1), 1, '^You', 0.476041, 0.721519, 0.167441, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 6 AND e.event_index = 1), 2, 'tells the guild,', 0.160034, 0.966245, 0.093771, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 7, 'Loot', 1, '/say', 1, 1, 0, 0, 1.0, 16, 5)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 7), 1, '#*#MQ2LinkDB#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 1), 0, '', 0.842932, 0.829993, 0.348647, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 1), 1, 'links in database', 0.875820, 0.879581, 0.161180, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 1), 2, 'Scanning incoming chat for item links', 0.351982, 0.958115, 0.220718, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 1), 3, 'Syntax', 0.921466, 0.638315, 0.352183, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 1), 4, 'Not scanning incoming chat', 0.916230, 0.302212, 0.302212, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 1), 5, 'Will NOT scan incoming chat for item links', 0.947644, 0.307612, 0.307612, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 1), 6, 'Fetching Items', 0.788005, 0.942408, 0.256572, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 7), 2, '#*#LootLink#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 2), 0, '', 1.000000, 0.976744, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 7), 3, '#*#LinksDB#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 3), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 7), 4, '#*# looted a #*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 7 AND e.event_index = 4), 0, '', 0.000000, 0.735533, 0.930233, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 11, 'OOC', 1, '/ooc', 1, 1, 0, 0, 1.0, 16, 7)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 11), 1, '#*#SERVER MESSAGE#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 11 AND e.event_index = 1), 0, '', 0.981460, 0.986046, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 11), 2, '#*# out of character, ''#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 11 AND e.event_index = 2), 0, '', 0.202532, 0.800000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 11 AND e.event_index = 2), 1, '^You', 0.781991, 0.781991, 0.781991, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 11 AND e.event_index = 2), 2, '^Eris', 1.000000, 0.177215, 0.656305, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 11 AND e.event_index = 2), 3, 'out of character', 0.101266, 1.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 11 AND e.event_index = 2), 4, '^Dhaymion', 0.000000, 0.952880, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 12, 'Raid', 0, '/rsay', 1, 0, 0, 0, 1.0, 16, 12)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 12), 1, '#*#tell#*# raid, ''#*#''#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 12 AND e.event_index = 1), 0, '', 0.106736, 0.346850, 0.682464, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 12 AND e.event_index = 1), 1, '^You', 0.592417, 0.592417, 0.592417, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 12 AND e.event_index = 1), 2, 'tells the raid,', 0.126030, 0.410423, 0.857820, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 13, 'Say', 1, '/say', 1, 1, 0, 0, 1.0, 16, 15)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 13), 1, '#*#say, ''#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 13 AND e.event_index = 1), 0, '', 1.000000, 0.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 13 AND e.event_index = 1), 1, '^You', 0.649789, 0.649789, 0.649789, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 13), 2, '#*# says, ''#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 13 AND e.event_index = 2), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 13 AND e.event_index = 2), 1, '^P3', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 14, 'Shout', 1, '/shout', 1, 1, 0, 0, 1.0, 16, 8)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 14), 1, '#*#shout#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 14 AND e.event_index = 1), 0, '', 1.000000, 0.054852, 0.054852, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 14 AND e.event_index = 1), 1, '^You', 0.093147, 0.919831, 0.574508, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 14 AND e.event_index = 1), 2, 'P3', 1.000000, 0.000000, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 15, 'Tells', 1, '/tell', 1, 1, 0, 0, 1.0, 16, 9)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 15), 1, '#*#sent #*# a tell that said:#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 15 AND e.event_index = 1), 0, '', 0.708327, 0.464135, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 15), 2, '#*#you have not received any tells#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 15 AND e.event_index = 2), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 15), 3, '#*#tells you, ''#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 15 AND e.event_index = 3), 0, '', 0.000000, 0.000000, 0.852321, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 15 AND e.event_index = 3), 1, 'NO2PT1', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 15 AND e.event_index = 3), 2, '^P3', 0.602499, 0.224786, 0.902954, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 15), 4, '#*#You told #1#,#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 15 AND e.event_index = 4), 0, '', 0.500000, 0.500000, 0.500000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 9000, 'Spam', 0, '/say', 1, 0, 0, 0, 1.0, 16, 18)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 9000), 1, '#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9000 AND e.event_index = 1), 0, '', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO channels (preset_id, channel_id, name, enabled, echo, main_enable, enable_links, pop_out, locked, scale, main_font_size, tab_order) VALUES ({}, 9100, 'NPC', 1, '/say', 0, 1, 0, 0, 1.0, 16, 1)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 9100), 1, '#*# says#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 1), 0, '', 0.734884, 0.547510, 0.249519, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 1), 1, 'N3', 0.846512, 0.556509, 0.263797, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 9100), 2, '#*#You have received an invaluable piece of information#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 2), 0, '', 0.864986, 0.948837, 0.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 9100), 3, '#*# whispers#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 3), 0, '', 0.786431, 0.958140, 0.619448, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 3), 1, 'N3', 0.796558, 0.874419, 0.532785, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 9100), 4, '#*#Your Adventurer Stone glows#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 4), 0, '', 0.468138, 0.962791, 0.125387, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 9100), 5, '#*# shouts#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 5), 0, '', 0.832558, 0.294300, 0.294300, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 5), 1, 'N3', 0.893023, 0.336441, 0.336441, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 9100), 6, '#*# tells you#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 6), 0, '', 0.647592, 0.321536, 0.776744, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 6), 1, '^N3', 0.817133, 0.502326, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 6), 2, '^PT1', 1.000000, 1.000000, 1.000000, 1.000000, 1, 0)", presetId));
    run(fmt::format("INSERT INTO events (channel_row_id, event_index, event_string, enabled) VALUES ((SELECT id FROM channels WHERE preset_id = {} AND channel_id = 9100), 7, '#*#You have additional information to uncover#*#', 1)", presetId));
    run(fmt::format("INSERT INTO filters (event_row_id, filter_index, filter_string, color_r, color_g, color_b, color_a, enabled, hidden) VALUES ((SELECT e.id FROM events e JOIN channels c ON e.channel_row_id = c.id WHERE c.preset_id = {} AND c.channel_id = 9100 AND e.event_index = 7), 0, '', 1.000000, 0.723804, 0.004651, 1.000000, 1, 0)", presetId));

    ExecSQL("COMMIT");
    WriteChatf("g[MyChat] Default channels seeded successfully.");
}
