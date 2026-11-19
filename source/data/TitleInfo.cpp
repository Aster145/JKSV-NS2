#include "data/TitleInfo.hpp"

#include "config/config.hpp"
#include "error.hpp"
#include "graphics/colors.hpp"
#include "graphics/gfxutil.hpp"
#include "logging/logger.hpp"
#include "stringutil.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstring>
#include <zlib.h>

//                      ---- Construction ----

data::TitleInfo::TitleInfo(uint64_t applicationID, const NacpStruct *archivedNacp) noexcept
    : m_applicationID(applicationID)
{
    const bool isSystem = applicationID & 0x8000000000000000;
    if (!isSystem)
    {
        constexpr auto source = NsApplicationControlSource_Storage;
        u64 size{};
        const auto valid = [&](Result rc) {
            return R_SUCCEEDED(rc) && size >= sizeof(m_data.nacp) && size <= sizeof(m_data);
        };
        // Request the compact icon for the default edition on Switch 2.
        Result rc = nsGetApplicationControlData2(source, applicationID, &m_data, sizeof(m_data), 1, 0, &size, nullptr);
        logger::log("Title %016" PRIX64 " compact rc=0x%08X bytes=%" PRIu64, applicationID, rc, size);
        if (!valid(rc))
        {
            std::memset(&m_data, 0, sizeof(m_data));
            size = 0;
            rc = nsGetApplicationControlData(source, applicationID, &m_data, sizeof(m_data), &size);
            logger::log("Title %016" PRIX64 " legacy rc=0x%08X bytes=%" PRIu64, applicationID, rc, size);
        }
        if (!valid(rc))
        {
            // Ask NS to consult installed storage directly after its cache-aware calls failed.
            std::memset(&m_data, 0, sizeof(m_data));
            size = 0;
            rc = nsGetApplicationControlData2(NsApplicationControlSource_StorageOnly, applicationID,
                                              &m_data, sizeof(m_data), 1, 0, &size, nullptr);
            logger::log("Title %016" PRIX64 " storage-only rc=0x%08X bytes=%" PRIu64, applicationID, rc, size);
        }
        m_hasData = valid(rc);
        if (m_hasData) { m_iconSize = size - sizeof(m_data.nacp); }
        else { std::memset(&m_data, 0, sizeof(m_data)); }
    }
    if (!m_hasData && archivedNacp)
    {
        m_data.nacp = *archivedNacp;
        m_hasData = true;
    }
    TitleInfo::load_language_entries();
    if (m_hasData) { logger::log("Title %016" PRIX64 " name=%s", applicationID, m_entry.name); }
    TitleInfo::get_create_path_safe_title();
}

//                      ---- Public functions ----

uint64_t data::TitleInfo::get_application_id() const noexcept { return m_applicationID; }

const NsApplicationControlData *data::TitleInfo::get_control_data() const noexcept { return &m_data; }

bool data::TitleInfo::has_control_data() const noexcept { return m_hasData; }

const char *data::TitleInfo::get_title() const noexcept { return m_entry.name; }

const char *data::TitleInfo::get_path_safe_title() const noexcept { return m_pathSafeTitle; }

const char *data::TitleInfo::get_publisher() const noexcept { return m_entry.author; }

uint64_t data::TitleInfo::get_save_data_owner_id() const noexcept { return m_data.nacp.save_data_owner_id; }

int64_t data::TitleInfo::get_save_data_size(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account:   return nacp.user_account_save_data_size;
        case FsSaveDataType_Bcat:      return nacp.bcat_delivery_cache_storage_size;
        case FsSaveDataType_Device:    return nacp.device_save_data_size;
        case FsSaveDataType_Temporary: return nacp.temporary_storage_size;
        case FsSaveDataType_Cache:     return nacp.cache_storage_size;
    }

    return 0;
}

int64_t data::TitleInfo::get_save_data_size_max(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account:   return std::max(nacp.user_account_save_data_size, nacp.user_account_save_data_size_max);
        case FsSaveDataType_Bcat:      return nacp.bcat_delivery_cache_storage_size;
        case FsSaveDataType_Device:    return std::max(nacp.device_save_data_size, nacp.device_save_data_size_max);
        case FsSaveDataType_Temporary: return nacp.temporary_storage_size;
        case FsSaveDataType_Cache:     return std::max(nacp.cache_storage_size, nacp.cache_storage_data_and_journal_size_max);
    }

    return 0;
}

int64_t data::TitleInfo::get_journal_size(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account:   return nacp.user_account_save_data_journal_size;
        case FsSaveDataType_Bcat:      return nacp.bcat_delivery_cache_storage_size;
        case FsSaveDataType_Device:    return nacp.device_save_data_journal_size;
        case FsSaveDataType_Temporary: return nacp.temporary_storage_size;
        case FsSaveDataType_Cache:     return nacp.cache_storage_journal_size;
    }

    return 0;
}

int64_t data::TitleInfo::get_journal_size_max(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account:
            return std::max(nacp.user_account_save_data_journal_size, nacp.user_account_save_data_journal_size_max);
        case FsSaveDataType_Bcat:      return nacp.bcat_delivery_cache_storage_size;
        case FsSaveDataType_Device:    return std::max(nacp.device_save_data_journal_size, nacp.device_save_data_journal_size_max);
        case FsSaveDataType_Temporary: return nacp.temporary_storage_size;
        case FsSaveDataType_Cache:
            return std::max(nacp.cache_storage_journal_size, nacp.cache_storage_data_and_journal_size_max);
    }

    return 0;
}

bool data::TitleInfo::has_save_data_type(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account: return nacp.user_account_save_data_size > 0 || nacp.user_account_save_data_size_max > 0;
        case FsSaveDataType_Bcat:    return nacp.bcat_delivery_cache_storage_size > 0;
        case FsSaveDataType_Device:  return nacp.device_save_data_size > 0 || nacp.device_save_data_size_max > 0;
        case FsSaveDataType_Cache:   return nacp.cache_storage_size > 0 || nacp.cache_storage_data_and_journal_size_max > 0;
    }

    return false;
}

sdl::SharedTexture data::TitleInfo::get_icon() const noexcept { return m_icon; }

void data::TitleInfo::set_path_safe_title(const char *newPathSafe) noexcept
{
    const size_t length = std::char_traits<char>::length(newPathSafe);
    if (length >= TitleInfo::SIZE_PATH_SAFE) { return; }

    std::memset(m_pathSafeTitle, 0x00, TitleInfo::SIZE_PATH_SAFE);
    std::memcpy(m_pathSafeTitle, newPathSafe, length);
}

void data::TitleInfo::load_icon()
{
    if (m_iconSize)
    {
        const std::string textureName = stringutil::get_formatted_string("%016" PRIX64, m_applicationID);
        m_icon = sdl::TextureManager::load(textureName, m_data.icon, m_iconSize);
        if (m_icon && m_icon->get()) { return; }
        logger::log("Title %016" PRIX64 " icon decode failed (%zu bytes): %s",
                    m_applicationID, m_iconSize, SDL_GetError());
    }
    const std::string text = stringutil::get_formatted_string("%04X", static_cast<unsigned>(m_applicationID & 0xFFFF));
    m_icon = gfxutil::create_generic_icon(text, 48, colors::DIALOG_DARK, colors::WHITE);
}

//                      ---- Private functions ----

void data::TitleInfo::load_language_entries() noexcept
{
    // Keep a usable name even when control data is absent or the title table cannot be decoded.
    m_entry = {};
    m_englishTitle[0] = '\0';
    std::snprintf(m_entry.name, sizeof(m_entry.name), "%016lX", m_applicationID);
    if (!m_hasData) { return; }

    using CompressedTitles = std::array<NacpLanguageEntry, 32>;
    std::unique_ptr<CompressedTitles> decodedTitles;
    const NacpLanguageEntry *entries{};
    size_t entryCount{};

    if (m_data.nacp.titles_data_format == 0)
    {
        entries = m_data.nacp.lang_data.lang;
        entryCount = 16;
    }
    else if (m_data.nacp.titles_data_format == 1)
    {
        const auto &compressed = m_data.nacp.lang_data.compressed_data;
        if (compressed.buffer_size == 0 || compressed.buffer_size > sizeof(compressed.buffer))
        {
            logger::log("Invalid compressed NACP title size for %016lX.", m_applicationID);
            return;
        }

        decodedTitles = std::make_unique<CompressedTitles>();
        z_stream stream{};
        stream.next_in = const_cast<Bytef *>(compressed.buffer);
        stream.avail_in = compressed.buffer_size;
        stream.next_out = reinterpret_cast<Bytef *>(decodedTitles->data());
        stream.avail_out = sizeof(CompressedTitles);
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        {
            logger::log("Failed to initialize NACP title decoder for %016lX.", m_applicationID);
            return;
        }

        const int result = inflate(&stream, Z_FINISH);
        const bool valid = result == Z_STREAM_END && stream.total_out == sizeof(CompressedTitles) &&
                           stream.total_in == compressed.buffer_size;
        inflateEnd(&stream);
        if (!valid)
        {
            logger::log("Invalid compressed NACP title data for %016lX.", m_applicationID);
            return;
        }
        entries = decodedTitles->data();
        entryCount = decodedTitles->size();
    }
    else
    {
        logger::log("Unsupported NACP title format %u for %016lX.", m_data.nacp.titles_data_format, m_applicationID);
        return;
    }

    // This libounce version's language helper only understands the legacy 16-entry layout.
    // Pass a normalized temporary NACP, then copy the result before that buffer goes away.
    auto languageNacp = std::make_unique<NacpStruct>();
    std::memcpy(languageNacp->lang_data.lang, entries, sizeof(languageNacp->lang_data.lang));
    NacpLanguageEntry *selected{};
    const bool entryError = error::libnx(nacpGetLanguageEntry(languageNacp.get(), &selected));
    if (!entryError && selected)
    {
        m_entry = *selected;
    }
    else if (!entryError)
    {
        // A future title may use only one of the extra entries in the 32-entry format.
        for (size_t i = 16; i < entryCount; ++i)
        {
            if (entries[i].name[0] || entries[i].author[0])
            {
                m_entry = entries[i];
                break;
            }
        }
    }
    m_entry.name[sizeof(m_entry.name) - 1] = '\0';
    m_entry.author[sizeof(m_entry.author) - 1] = '\0';
    if (m_entry.name[0] == '\0') { std::snprintf(m_entry.name, sizeof(m_entry.name), "%016lX", m_applicationID); }

    // NACP indices are 0 = US English and 1 = GB English, not SetLanguage enum values.
    auto title_length = [](const NacpLanguageEntry &entry) {
        return static_cast<size_t>(std::find(entry.name, entry.name + sizeof(entry.name), '\0') - entry.name);
    };
    const size_t enUsLength = title_length(entries[0]);
    const size_t enGbLength = title_length(entries[1]);
    const char *englishTitle = enUsLength >= enGbLength ? entries[0].name : entries[1].name;
    const size_t englishLength = std::min(std::max(enUsLength, enGbLength), sizeof(m_englishTitle) - 1);
    std::memcpy(m_englishTitle, englishTitle, englishLength);
    m_englishTitle[englishLength] = '\0';
}

void data::TitleInfo::get_create_path_safe_title() noexcept
{
    // Check if the title has a custom output path instead.
    const bool hasCustom = config::has_custom_path(m_applicationID);
    if (hasCustom)
    {
        // Read it into our local buffer.
        config::get_custom_path(m_applicationID, m_pathSafeTitle, TitleInfo::SIZE_PATH_SAFE);
        return;
    }

    // This is whether or not to use the title ID. It's used to bypass the English check.
    const bool useTitleId = config::get_by_key(config::keys::USE_TITLE_IDS);
    const bool useEnglish = config::get_by_key(config::keys::ENGLISH_SAFE_TITLES);

    // Grab the English title in case it's needed.
    const char *englishTitle{};
    const bool hasEnglish = TitleInfo::get_english_title(&englishTitle);

    // Final condition for override.
    const bool englishSafeTitle = useEnglish && hasEnglish;

    // This is our final string to use.
    const char *safeTarget = englishSafeTitle ? englishTitle : m_entry.name;

    const bool sanitized = !useTitleId && stringutil::sanitize_string_for_path(safeTarget, m_pathSafeTitle, SIZE_PATH_SAFE);
    if (useTitleId || !sanitized) { std::snprintf(m_pathSafeTitle, TitleInfo::SIZE_PATH_SAFE, "%016lX", m_applicationID); }
}

bool data::TitleInfo::get_english_title(const char **titleOut) const noexcept
{
    if (m_englishTitle[0] == '\0') { return false; }
    *titleOut = m_englishTitle;
    return true;
}
