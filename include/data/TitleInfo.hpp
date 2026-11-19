#pragma once
#include "data/DataCommon.hpp"
#include "sdl.hpp"

#include <cstdint>
#include <memory>
#include <switch.h>
#include <vector>

namespace data
{
    class TitleInfo;

    /// @brief Vector of pointers to titleinfo instances.
    using TitleInfoList = std::vector<data::TitleInfo *>;

    /// @brief Class that holds data related to titles loaded from the system.
    class TitleInfo final : public data::DataCommon
    {
        public:
            /// @brief Reads artwork from the console. An archived NACP supplies only missing save metadata.
            explicit TitleInfo(uint64_t applicationID, const NacpStruct *archivedNacp = nullptr) noexcept;

            // None of this nonesense around these parts.
            TitleInfo(const TitleInfo &)            = delete;
            TitleInfo &operator=(const TitleInfo &) = delete;

            /// @brief Returns the application ID of the title.
            /// @return Title's application ID.
            uint64_t get_application_id() const noexcept;

            /// @brief Returns a pointer to the control data for the title.
            /// @return Pointer to control data.
            const NsApplicationControlData *get_control_data() const noexcept;

            /// @brief Returns whether or not the title has control data.
            /// @return Whether or not the title has control data.
            bool has_control_data() const noexcept;

            /// @brief Returns the title of the title?
            /// @return Title directly from the NACP.
            const char *get_title() const noexcept;

            /// @brief Returns the path safe version of the title for file system usage.
            /// @return Path safe version of the title.
            const char *get_path_safe_title() const noexcept;

            /// @brief Returns the publisher of the title.
            /// @return Publisher string from NACP.
            const char *get_publisher() const noexcept;

            /// @brief Returns the owner ID of the save data.
            uint64_t get_save_data_owner_id() const noexcept;

            /// @brief Returns the save data container's base size.
            /// @param saveType Type of save data to return.
            /// @return Size of baseline save data if applicable. If not, 0.
            int64_t get_save_data_size(uint8_t saveType) const noexcept;

            /// @brief Returns the maximum size of the save data container.
            /// @param saveType Type of save data to return.
            /// @return Maximum size of the save container if applicable. If not, 0.
            int64_t get_save_data_size_max(uint8_t saveType) const noexcept;

            /// @brief Returns the journaling size for the save type passed.
            /// @param saveType Save type to return.
            /// @return Journal size if applicable. If not, 0.
            int64_t get_journal_size(uint8_t saveType) const noexcept;

            /// @brief Returns the maximum journal size for the save type passed.
            /// @param saveType Save type to return.
            /// @return Maximum journal size if applicable. If not, 0.
            int64_t get_journal_size_max(uint8_t saveType) const noexcept;

            /// @brief Returns if a title uses the save type passed.
            /// @param saveType Save type to check for.
            /// @return True on success. False on failure.
            bool has_save_data_type(uint8_t saveType) const noexcept;

            /// @brief Returns a pointer to the icon texture.
            /// @return Icon
            sdl::SharedTexture get_icon() const noexcept;

            /// @brief Allows the path safe title to be set to a new path.
            /// @param newPathSafe Buffer containing the new safe path to use.
            void set_path_safe_title(const char *newPathSafe) noexcept;

            /// @brief Loads the icon from the nacp.
            void load_icon() override;

        private:
            /// @brief This defines how long the buffer is for the path safe version of the title.
            static inline constexpr size_t SIZE_PATH_SAFE = 0x200;

            /// @brief Stores application ID for easier grabbing since JKSV is all pointers.
            uint64_t m_applicationID{};

            /// @brief Where all the good stuff is.
            NsApplicationControlData m_data{};
            // Number of icon bytes returned by a system service.
            size_t m_iconSize{};

            /// @brief Owns the selected entry, including titles decoded from compressed NACP data.
            NacpLanguageEntry m_entry{};

            /// @brief Owns the longer US/GB English title used for stable backup paths.
            char m_englishTitle[TitleInfo::SIZE_PATH_SAFE]{};

            /// @brief Saves whether or not the title has control data.
            bool m_hasData{};

            /// @brief This is the path safe version of the title.
            char m_pathSafeTitle[TitleInfo::SIZE_PATH_SAFE]{};

            /// @brief Shared icon texture.
            sdl::SharedTexture m_icon{};

            /// @brief Private function to get/create the path safe title.
            void get_create_path_safe_title() noexcept;

            /// @brief Decodes NACP titles and copies the selected language and English title.
            void load_language_entries() noexcept;

            /// @brief Checks and returns whether or not a US English or GB English title exists.
            /// @param titleOut Pointer to pointer to store title pointer.
            bool get_english_title(const char **titleOut) const noexcept;
    };
} // namespace data
