#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "ftp/server.hpp"

#include "build_options.hpp"
#include "fslib.hpp"
#include "logging/logger.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <string_view>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    constexpr uint16_t PORT = 1337;
    constexpr size_t BUFFER_SIZE = 64 * 1024;
    constexpr int DATA_TIMEOUT = 30;
    using Clock = std::chrono::steady_clock;

    std::mutex s_lifecycleLock;
    std::mutex s_snapshotLock;
    std::atomic<bool> s_running{false};
    std::atomic<bool> s_stop{false};
    std::string s_address;
    std::string s_status{"Stopped"};
    Thread s_thread{};
    bool s_threadCreated{};
    bool s_socketOwned{};
    bool s_nifmOwned{};
    int s_listener{-1};
    uint32_t s_ip{};
    uint64_t s_tempSerial{};

    void set_status(std::string value)
    {
        std::lock_guard guard{s_snapshotLock};
        s_status = std::move(value);
    }

    void close_socket(int &fd)
    {
        if (fd >= 0) { ::close(fd); fd = -1; }
    }

    bool nonblocking(int fd)
    {
        const int flags = fcntl(fd, F_GETFL, 0);
        return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    bool retryable()
    {
        return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
    }

    // All network I/O is nonblocking. Only this thread closes its sockets;
    // stop() sets a flag and waits at most one select interval for network I/O.
    bool wait_socket(int fd, bool writing, Clock::time_point deadline)
    {
        while (!s_stop && Clock::now() < deadline)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            timeval timeout{0, 100000};
            const int result = select(fd + 1, writing ? nullptr : &fds, writing ? &fds : nullptr, nullptr, &timeout);
            if (result > 0) { return true; }
            if (result < 0 && errno != EINTR) { return false; }
        }
        errno = s_stop ? ECANCELED : ETIMEDOUT;
        return false;
    }

    bool send_all(int fd, const void *data, size_t size)
    {
        const char *bytes = static_cast<const char *>(data);
        auto deadline = Clock::now() + std::chrono::seconds(DATA_TIMEOUT);
        while (size && !s_stop)
        {
            if (!wait_socket(fd, true, deadline)) { return false; }
            const ssize_t sent = send(fd, bytes, size, MSG_NOSIGNAL);
            if (sent < 0 && retryable()) { continue; }
            if (sent <= 0) { return false; }
            bytes += sent;
            size -= static_cast<size_t>(sent);
            deadline = Clock::now() + std::chrono::seconds(DATA_TIMEOUT);
        }
        return !s_stop && size == 0;
    }

    bool send_text(int fd, std::string_view text) { return send_all(fd, text.data(), text.size()); }

    bool reply(int fd, int code, std::string_view text)
    {
        return send_text(fd, std::to_string(code) + " " + std::string(text) + "\r\n");
    }

    std::string_view command_body(std::string_view line)
    {
        // FTP ABOR may be preceded by Telnet IP/DM. SO_OOBINLINE keeps the
        // urgent DM byte in order with the control stream.
        while (line.size() >= 2 && static_cast<unsigned char>(line[0]) == 255)
        {
            const auto telnet = static_cast<unsigned char>(line[1]);
            const size_t count = telnet >= 251 && telnet <= 254 ? 3 : 2;
            if (line.size() < count) { return {}; }
            line.remove_prefix(count);
        }
        return line;
    }

    // FTP uses UTF-8 bytes verbatim. Remove exactly the command separator, not
    // the whitespace in the filename. Never interpret device names or backslashes.
    bool path_for(std::string_view cwd, std::string_view argument, std::string &out)
    {
        for (const unsigned char c : argument)
        {
            if (c < 0x20 || c == 0x7f || std::string_view{"\\<>:\"|?*"}.find(c) != std::string_view::npos)
            { return false; }
        }
        std::string input;
        if (argument.empty() || argument.front() != '/') { input = std::string(cwd) + "/"; }
        input += argument;
        std::vector<std::string_view> parts;
        size_t begin = 0;
        while (begin <= input.size())
        {
            const size_t end = input.find('/', begin);
            const auto part = std::string_view(input).substr(begin, end == input.npos ? input.size() - begin : end - begin);
            if (part == "..") { if (!parts.empty()) { parts.pop_back(); } }
            else if (!part.empty() && part != ".") { parts.push_back(part); }
            if (end == input.npos) { break; }
            begin = end + 1;
        }
        out.clear();
        for (const auto part : parts) { out += '/'; out += part; }
        if (out.empty()) { out = "/"; }
        return out.size() < FS_MAX_PATH;
    }

    fslib::Path sd_path(const std::string &path) { return fslib::Path{"sdmc:" + path}; }

    bool exists(const std::string &path)
    {
        const auto sd = sd_path(path);
        return fslib::file_exists(sd) || fslib::directory_exists(sd);
    }

    std::string temporary_path(const std::string &target, const char *suffix)
    {
        const auto parent = target.substr(0, target.find_last_of('/') + 1);
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const auto candidate = parent + ".jksv-ftp-" + std::to_string(++s_tempSerial) + suffix;
            if (candidate.size() >= FS_MAX_PATH) { return {}; }
            if (!exists(candidate)) { return candidate; }
        }
        return {};
    }

    struct TemporaryFile
    {
        std::string path;
        fslib::File file;
        explicit TemporaryFile(std::string name) : path(std::move(name)) {}
        ~TemporaryFile()
        {
            file.close();
            if (!path.empty()) { fslib::delete_file(sd_path(path)); }
        }
    };

    // FS rename does not replace an existing file. Retain it until the newly
    // received file has been installed; rollback if the second rename fails.
    bool install_upload(TemporaryFile &upload, const std::string &target)
    {
        std::string previous;
        if (fslib::file_exists(sd_path(target)))
        {
            previous = temporary_path(target, ".bak");
            if (previous.empty() || !fslib::rename_file(sd_path(target), sd_path(previous))) { return false; }
        }
        if (!fslib::rename_file(sd_path(upload.path), sd_path(target)))
        {
            if (!previous.empty() && !fslib::rename_file(sd_path(previous), sd_path(target)))
            { logger::log("FTP: original file retained at sdmc:%s", previous.c_str()); }
            return false;
        }
        upload.path.clear();
        if (!previous.empty() && !fslib::delete_file(sd_path(previous)))
        { logger::log("FTP: previous file retained at sdmc:%s", previous.c_str()); }
        return true;
    }

    std::string timestamp(const std::string &path)
    {
        FsTimeStampRaw stamp{};
        if (!fslib::get_file_timestamp(sd_path(path), stamp) || !stamp.is_valid) { return {}; }
        const time_t modified = static_cast<time_t>(stamp.modified);
        tm utc{};
        char text[32]{};
        if (!gmtime_r(&modified, &utc) || !strftime(text, sizeof(text), "%Y%m%d%H%M%S", &utc)) { return {}; }
        return text;
    }

    std::string facts(const std::string &path, bool directory, int64_t size)
    {
        std::string result = directory ? "type=dir;" : "type=file;size=" + std::to_string(size) + ";";
        const auto modified = timestamp(path);
        if (!modified.empty()) { result += "modify=" + modified + ";"; }
        return result;
    }

    struct Session
    {
        int control;
        uint32_t peerIP;
        int passive{-1};
        int data{-1};
        bool usernameAccepted{};
        bool authenticated{};
        bool epsvOnly{};
        bool restartSet{};
        int64_t restart{};
        std::string cwd{"/"};
        std::string renameFrom;
        std::string incoming;

        Session(int fd, uint32_t ip) : control(fd), peerIP(ip) {}
        ~Session() { close_socket(data); close_socket(passive); close_socket(control); }

        bool command_line(std::string &line)
        {
            auto deadline = Clock::now() + std::chrono::minutes(5);
            while (!s_stop)
            {
                const size_t end = incoming.find('\n');
                if (end != incoming.npos)
                {
                    line = incoming.substr(0, end);
                    incoming.erase(0, end + 1);
                    if (!line.empty() && line.back() == '\r') { line.pop_back(); }
                    return true;
                }
                if (incoming.size() >= 4096) { reply(control, 500, "Command too long"); return false; }
                if (!wait_socket(control, false, deadline)) { return false; }
                char buffer[1024];
                const ssize_t count = recv(control, buffer, sizeof(buffer), 0);
                if (count < 0 && retryable()) { continue; }
                if (count <= 0) { return false; }
                incoming.append(buffer, static_cast<size_t>(count));
                deadline = Clock::now() + std::chrono::minutes(5);
            }
            return false;
        }

        bool upload_cancelled()
        {
            // Preserve pending commands for dispatch after the transfer. In
            // particular ABOR must still receive its final 226 after our 426.
            char buffer[1024];
            while (incoming.size() < 4096)
            {
                const ssize_t count = recv(control, buffer, sizeof(buffer), 0);
                if (count == 0) { return true; }
                if (count < 0)
                {
                    if (errno == EINTR) { continue; }
                    if (!retryable()) { return true; }
                    break;
                }
                incoming.append(buffer, static_cast<size_t>(count));
            }
            if (incoming.size() >= 4096) { return true; }
            size_t begin = 0;
            while (begin < incoming.size())
            {
                const size_t end = incoming.find('\n', begin);
                if (end == incoming.npos) { break; }
                auto line = command_body(std::string_view(incoming).substr(begin, end - begin));
                if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }
                if (line.size() >= 4 && (line.size() == 4 || line[4] == ' ') &&
                    (line[0] == 'A' || line[0] == 'a') && (line[1] == 'B' || line[1] == 'b') &&
                    (line[2] == 'O' || line[2] == 'o') && (line[3] == 'R' || line[3] == 'r'))
                { return true; }
                begin = end + 1;
            }
            return false;
        }

        bool wait_upload_data(Clock::time_point deadline)
        {
            while (!s_stop && Clock::now() < deadline)
            {
                if (upload_cancelled()) { return false; }
                fd_set reads;
                FD_ZERO(&reads);
                FD_SET(data, &reads);
                FD_SET(control, &reads);
                timeval timeout{0, 100000};
                const int result = select(std::max(data, control) + 1, &reads, nullptr, nullptr, &timeout);
                if (result < 0 && errno != EINTR) { return false; }
                if (result > 0 && FD_ISSET(control, &reads) && upload_cancelled()) { return false; }
                if (result > 0 && FD_ISSET(data, &reads)) { return true; }
            }
            return false;
        }

        void passive_mode(bool extended)
        {
            close_socket(passive);
            close_socket(data);
            passive = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in local{};
            socklen_t length = sizeof(local);
            if (getsockname(control, reinterpret_cast<sockaddr *>(&local), &length) != 0)
            { local.sin_family = AF_INET; local.sin_addr.s_addr = s_ip; }
            local.sin_port = 0;
            if (passive < 0 || !nonblocking(passive) ||
                bind(passive, reinterpret_cast<sockaddr *>(&local), sizeof(local)) < 0 || listen(passive, 1) < 0 ||
                getsockname(passive, reinterpret_cast<sockaddr *>(&local), &length) < 0)
            {
                close_socket(passive);
                reply(control, 425, "Cannot open passive listener");
                return;
            }
            const unsigned port = ntohs(local.sin_port);
            char response[128];
            if (extended) { snprintf(response, sizeof(response), "Entering Extended Passive Mode (|||%u|)", port); }
            else
            {
                const uint32_t ip = ntohl(local.sin_addr.s_addr);
                snprintf(response, sizeof(response), "Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
                         (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, port >> 8, port & 255);
            }
            reply(control, extended ? 229 : 227, response);
        }

        bool open_data()
        {
            if (passive < 0) { reply(control, 425, "Use PASV or EPSV first"); return false; }
            if (!reply(control, 150, "Opening data connection")) { return false; }
            const auto deadline = Clock::now() + std::chrono::seconds(DATA_TIMEOUT);
            while (wait_socket(passive, false, deadline))
            {
                sockaddr_in peer{};
                socklen_t length = sizeof(peer);
                data = accept(passive, reinterpret_cast<sockaddr *>(&peer), &length);
                if (data < 0) { if (retryable()) { continue; } break; }
                if (peer.sin_addr.s_addr != peerIP || !nonblocking(data)) { close_socket(data); continue; }
                close_socket(passive);
                return true;
            }
            close_socket(passive);
            reply(control, 425, "Data connection unavailable");
            return false;
        }

        void transfer_result(bool success, const char *operation, uint64_t bytes = 0)
        {
            close_socket(data);
            close_socket(passive);
            if (s_stop) { success = false; }
            reply(control, success ? 226 : 426, success ? "Transfer complete" : "Transfer failed or interrupted");
            set_status(success ? "Transfer complete" : "Transfer failed");
            logger::log("FTP: %s %s, %llu bytes", operation, success ? "complete" : "failed",
                        static_cast<unsigned long long>(bytes));
        }

        void download(const std::string &path)
        {
            const int64_t offset = restartSet ? restart : 0;
            restartSet = false;
            fslib::File file(sd_path(path), FsOpenMode_Read);
            if (!file.is_open() || offset > file.get_size()) { reply(control, 550, "File or restart offset unavailable"); return; }
            file.seek(offset, fslib::File::BEGINNING);
            if (!open_data()) { return; }
            set_status("Downloading");
            std::array<char, BUFFER_SIZE> buffer;
            uint64_t total = 0;
            bool good = true;
            while (!s_stop)
            {
                const ssize_t count = file.read(buffer.data(), buffer.size());
                if (count < 0) { good = false; break; }
                if (!count) { break; }
                if (!send_all(data, buffer.data(), static_cast<size_t>(count))) { good = false; break; }
                total += static_cast<uint64_t>(count);
            }
            transfer_result(good, "RETR", total);
        }

        void upload(const std::string &path, bool append)
        {
            const bool resume = restartSet;
            int64_t offset = restartSet ? restart : 0;
            restartSet = false;
            if (path == "/" || fslib::directory_exists(sd_path(path))) { reply(control, 550, "Not a file path"); return; }
            const bool originalExists = fslib::file_exists(sd_path(path));
            const int64_t originalSize = originalExists ? fslib::get_file_size(sd_path(path)) : 0;
            if (append) { offset = originalSize; }
            if (offset < 0 || offset > originalSize || (resume && offset && !originalExists))
            { reply(control, 550, "Restart offset unavailable"); return; }
            if (passive < 0) { reply(control, 425, "Use PASV or EPSV first"); return; }
            TemporaryFile staging(temporary_path(path, ".tmp"));
            if (staging.path.empty()) { reply(control, 550, "Cannot allocate temporary filename"); return; }
            staging.file.open(sd_path(staging.path), FsOpenMode_Write | FsOpenMode_Create);
            if (!staging.file.is_open()) { reply(control, 550, "Cannot create upload file"); return; }
            std::array<char, BUFFER_SIZE> buffer;
            if ((resume || append) && originalExists)
            {
                fslib::File original(sd_path(path), FsOpenMode_Read);
                if (!original.is_open()) { reply(control, 550, "Cannot read existing file"); return; }
                while (!s_stop)
                {
                    const ssize_t count = original.read(buffer.data(), buffer.size());
                    if (count < 0 || (count > 0 && staging.file.write(buffer.data(), static_cast<size_t>(count)) != count))
                    { reply(control, 451, "Cannot prepare resumed upload"); return; }
                    if (!count) { break; }
                }
                if (s_stop) { return; }
                staging.file.seek(offset, fslib::File::BEGINNING);
            }
            if (!open_data()) { return; }
            set_status("Uploading");
            bool good = false;
            uint64_t total = 0;
            auto deadline = Clock::now() + std::chrono::seconds(DATA_TIMEOUT);
            while (!s_stop && wait_upload_data(deadline))
            {
                const ssize_t count = recv(data, buffer.data(), buffer.size(), 0);
                if (count < 0 && retryable()) { continue; }
                if (count < 0) { break; }
                if (!count) { good = true; break; }
                if (staging.file.write(buffer.data(), static_cast<size_t>(count)) != count) { break; }
                total += static_cast<uint64_t>(count);
                deadline = Clock::now() + std::chrono::seconds(DATA_TIMEOUT);
            }
            good = good && !s_stop && staging.file.flush();
            staging.file.close();
            // A control disconnect or queued ABOR is not permission to replace
            // an existing file, even if the data socket closed cleanly first.
            if (upload_cancelled()) { good = false; }
            good = good && !s_stop;
            if (good) { good = install_upload(staging, path); }
            transfer_result(good, append ? "APPE" : "STOR", total);
        }

        void listing(const std::string &path, const std::string &command)
        {
            const bool directory = fslib::directory_exists(sd_path(path));
            if (!directory && !fslib::file_exists(sd_path(path))) { reply(control, 550, "Path unavailable"); return; }
            fslib::Directory entries;
            if (directory)
            {
                entries.open(sd_path(path), false);
                if (!entries.is_open()) { reply(control, 550, "Cannot read directory"); return; }
            }
            if (!open_data()) { return; }
            auto send_entry = [&](const std::string &name, bool isDirectory, int64_t size)
            {
                // Newline names cannot be represented by FTP directory listings.
                if (name.find_first_of("\r\n") != name.npos) { return true; }
                const auto full = directory ? (path == "/" ? "/" + name : path + "/" + name) : path;
                std::string line;
                if (command == "MLSD") { line = facts(full, isDirectory, size) + " " + name; }
                else if (command == "NLST") { line = name; }
                else
                {
                    std::string date = "Jan 01  2000";
                    FsTimeStampRaw stamp{};
                    if (fslib::get_file_timestamp(sd_path(full), stamp) && stamp.is_valid)
                    {
                        const time_t modified = static_cast<time_t>(stamp.modified);
                        tm utc{};
                        char formatted[32]{};
                        if (gmtime_r(&modified, &utc) && strftime(formatted, sizeof(formatted), "%b %d  %Y", &utc))
                        { date = formatted; }
                    }
                    line = std::string(isDirectory ? "drwxr-xr-x" : "-rw-r--r--") + " 1 ftp ftp " +
                           std::to_string(isDirectory ? 0 : size) + " " + date + " " + name;
                }
                return send_text(data, line + "\r\n");
            };
            bool good = true;
            if (directory)
            {
                for (const auto &entry : entries)
                {
                    if (s_stop || !send_entry(entry.get_filename(), entry.is_directory(), entry.get_size()))
                    { good = false; break; }
                }
            }
            else { good = send_entry(path.substr(path.find_last_of('/') + 1), false, fslib::get_file_size(sd_path(path))); }
            transfer_result(good, command.c_str());
        }

        bool dispatch(const std::string &rawLine)
        {
            const std::string line(command_body(rawLine));
            const size_t split = line.find(' ');
            std::string command = line.substr(0, split);
            for (char &c : command) { if (c >= 'a' && c <= 'z') { c -= 'a' - 'A'; } }
            std::string argument = split == line.npos ? "" : line.substr(split + 1);
            if (command != "RNTO" && command != "RNFR") { renameFrom.clear(); }
            if (command == "QUIT") { reply(control, 221, "Goodbye"); return false; }
            if (command == "USER")
            {
                authenticated = false;
                usernameAccepted = argument == "anonymous" || argument == "ftp";
                reply(control, usernameAccepted ? 331 : 530, usernameAccepted ? "Anonymous login; send password" : "Use anonymous login");
            }
            else if (command == "PASS")
            {
                authenticated = usernameAccepted;
                reply(control, authenticated ? 230 : 530, authenticated ? "Logged in; SD card root" : "Send USER anonymous first");
                if (authenticated) { set_status("Connected"); }
            }
            else if (command == "FEAT")
            { send_text(control, "211-Features\r\n UTF8\r\n EPSV\r\n PASV\r\n SIZE\r\n MDTM\r\n REST STREAM\r\n MLST type*;size*;modify*;\r\n211 End\r\n"); }
            else if (command == "SYST") { reply(control, 215, "UNIX Type: L8"); }
            else if (command == "NOOP") { reply(control, 200, "OK"); }
            else if (command == "OPTS")
            {
                const bool supported = argument == "UTF8 ON" || argument == "UTF8" || argument.rfind("MLST ", 0) == 0;
                reply(control, supported ? 200 : 501, supported ? "Options accepted" : "Unsupported option");
            }
            else if (!authenticated) { reply(control, 530, "Login required"); }
            else if (command == "PWD" || command == "XPWD") { reply(control, 257, "\"" + cwd + "\""); }
            else if (command == "TYPE")
            {
                const bool supported = argument == "I" || argument == "A" || argument == "L 8";
                reply(control, supported ? 200 : 504, supported ? "Type accepted" : "Unsupported type");
            }
            else if (command == "MODE" || command == "STRU")
            {
                const bool supported = argument == (command == "MODE" ? "S" : "F");
                reply(control, supported ? 200 : 504, supported ? "OK" : "Unsupported mode");
            }
            else if (command == "PASV" && !epsvOnly) { passive_mode(false); }
            else if (command == "EPSV")
            {
                if (argument == "ALL") { epsvOnly = true; reply(control, 200, "EPSV only"); }
                else if (argument.empty() || argument == "1") { passive_mode(true); }
                else { reply(control, 522, "IPv4 only; use EPSV 1"); }
            }
            else if (command == "REST")
            {
                int64_t value = 0;
                bool valid = !argument.empty();
                for (const char c : argument)
                {
                    if (c < '0' || c > '9' || value > (INT64_MAX - (c - '0')) / 10) { valid = false; break; }
                    value = value * 10 + (c - '0');
                }
                restartSet = valid;
                restart = value;
                reply(control, valid ? 350 : 501, valid ? "Restart offset accepted" : "Invalid restart offset");
            }
            else if (command == "ABOR")
            {
                close_socket(data); close_socket(passive); restartSet = false;
                reply(control, 226, "No transfer active");
            }
            else if (command == "ALLO") { reply(control, 202, "Allocation not needed"); }
            else
            {
                const bool list = command == "LIST" || command == "NLST" || command == "MLSD";
                if (command == "CDUP" || command == "XCUP") { argument = ".."; }
                if (list)
                {
                    // WinSCP LIST -a/-la; only known switches are options.
                    for (const auto option : {"-a", "-l", "-la", "-al"})
                    {
                        if (argument == option) { argument.clear(); break; }
                        const std::string prefix = std::string(option) + " ";
                        if (argument.rfind(prefix, 0) == 0) { argument.erase(0, prefix.size()); break; }
                    }
                }
                std::string path;
                if (!path_for(cwd, argument, path)) { reply(control, 550, "Invalid SD card path"); return true; }
                const auto sd = sd_path(path);
                if (command == "CWD" || command == "XCWD" || command == "CDUP" || command == "XCUP")
                {
                    if (fslib::directory_exists(sd)) { cwd = path; reply(control, 250, "Directory changed"); }
                    else { reply(control, 550, "Directory unavailable"); }
                }
                else if (list) { listing(path, command); }
                else if (command == "MLST")
                {
                    const bool directory = fslib::directory_exists(sd);
                    if (!directory && !fslib::file_exists(sd)) { reply(control, 550, "Path unavailable"); }
                    else { send_text(control, "250-Listing\r\n " + facts(path, directory, directory ? 0 : fslib::get_file_size(sd)) +
                                             " " + path + "\r\n250 End\r\n"); }
                }
                else if (argument.empty()) { reply(control, 501, "Path required"); }
                else if (command == "RETR") { download(path); }
                else if (command == "STOR" || command == "APPE") { upload(path, command == "APPE"); }
                else if (command == "SIZE")
                {
                    const int64_t size = fslib::get_file_size(sd);
                    reply(control, size >= 0 ? 213 : 550, size >= 0 ? std::to_string(size) : "File unavailable");
                }
                else if (command == "MDTM")
                {
                    const auto modified = timestamp(path);
                    reply(control, modified.empty() ? 550 : 213, modified.empty() ? "Timestamp unavailable" : modified);
                }
                else if (command == "MKD" || command == "XMKD")
                {
                    const bool good = path != "/" && fslib::create_directory(sd);
                    reply(control, good ? 257 : 550, good ? "\"" + path + "\" created" : "Cannot create directory");
                }
                else if (command == "RMD" || command == "XRMD")
                {
                    const bool good = path != "/" && fslib::delete_directory(sd);
                    reply(control, good ? 250 : 550, good ? "Directory removed" : "Cannot remove directory");
                }
                else if (command == "DELE")
                {
                    const bool good = path != "/" && fslib::delete_file(sd);
                    reply(control, good ? 250 : 550, good ? "File removed" : "Cannot remove file");
                }
                else if (command == "RNFR")
                {
                    renameFrom.clear();
                    if (path != "/" && exists(path)) { renameFrom = path; reply(control, 350, "Send RNTO"); }
                    else { reply(control, 550, "Path unavailable"); }
                }
                else if (command == "RNTO")
                {
                    if (renameFrom.empty()) { reply(control, 503, "Send RNFR first"); }
                    else
                    {
                        const auto from = sd_path(renameFrom);
                        const bool good = path != "/" && !exists(path) &&
                            (fslib::directory_exists(from) ? fslib::rename_directory(from, sd) : fslib::rename_file(from, sd));
                        reply(control, good ? 250 : 550, good ? "Renamed" : "Cannot rename path");
                        renameFrom.clear();
                    }
                }
                else { reply(control, 502, "Command not supported; use passive FTP"); }
            }
            return true;
        }

        void run()
        {
            if (!reply(control, 220, "JKSV-NS2 FTP; anonymous; SD card root")) { return; }
            std::string line;
            while (!s_stop && command_line(line))
            {
                if (!dispatch(line)) { break; }
            }
        }
    };

    void server_thread(void *)
    {
        while (!s_stop)
        {
            if (!wait_socket(s_listener, false, Clock::now() + std::chrono::seconds(1)))
            {
                if (errno == ETIMEDOUT || s_stop) { continue; }
                set_status("Socket wait failed");
                logger::log("FTP: listener wait failed, errno=%d", errno);
                break;
            }
            sockaddr_in peer{};
            socklen_t length = sizeof(peer);
            const int client = accept(s_listener, reinterpret_cast<sockaddr *>(&peer), &length);
            if (client < 0)
            {
                if (retryable()) { continue; }
                set_status("Accept failed");
                logger::log("FTP: accept failed, errno=%d", errno);
                break;
            }
            Session session(client, peer.sin_addr.s_addr);
            if (!nonblocking(client)) { continue; }
            const int inlineUrgent = 1;
            setsockopt(client, SOL_SOCKET, SO_OOBINLINE, &inlineUrgent, sizeof(inlineUrgent));
            set_status("Connected");
            logger::log("FTP: client connected");
            session.run();
            logger::log("FTP: client disconnected");
            if (!s_stop) { set_status("Waiting for connection"); }
        }
        close_socket(s_listener);
        s_running = false;
    }

    void release_services()
    {
        if (s_nifmOwned) { nifmExit(); s_nifmOwned = false; }
        if (s_socketOwned) { socketExit(); s_socketOwned = false; }
    }

    void stop_locked(bool updateStatus)
    {
        s_stop = true;
        if (s_threadCreated)
        {
            threadWaitForExit(&s_thread);
            threadClose(&s_thread);
            s_threadCreated = false;
        }
        close_socket(s_listener);
        s_running = false;
        release_services();
        std::lock_guard guard{s_snapshotLock};
        s_address.clear();
        if (updateStatus) { s_status = "Stopped"; }
    }

    bool start_error(const char *operation, uint32_t result)
    {
        char message[128];
        snprintf(message, sizeof(message), "%s failed (0x%08X)", operation, result);
        set_status(message);
        logger::log("FTP: %s", message);
        stop_locked(false);
        return false;
    }
}

bool ftp::start()
{
    std::lock_guard guard{s_lifecycleLock};
    if (s_running) { return true; }
    stop_locked(false);
    s_stop = false;
    if (!fslib::directory_exists(fslib::Path{"sdmc:/"})) { return start_error("SD card", 0); }
#if !JKSV_ENABLE_CURL
    Result result = socketInitializeDefault();
    if (R_FAILED(result)) { return start_error("Socket service", result); }
    s_socketOwned = true;
    result = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(result)) { return start_error("Network service", result); }
    s_nifmOwned = true;
#endif
    const Result ipResult = nifmGetCurrentIpAddress(&s_ip);
    if (R_FAILED(ipResult) || !s_ip) { return start_error("Network address", ipResult); }
    s_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listener < 0) { return start_error("Listen socket", errno); }
    const int reuse = 1;
    setsockopt(s_listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (!nonblocking(s_listener) || bind(s_listener, reinterpret_cast<sockaddr *>(&local), sizeof(local)) < 0 ||
        listen(s_listener, 1) < 0)
    { return start_error("Listen on port 1337", errno); }
    char ip[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &s_ip, ip, sizeof(ip))) { return start_error("Format network address", errno); }
    {
        std::lock_guard snapshotGuard{s_snapshotLock};
        s_address = std::string("ftp://") + ip + ":1337";
        s_status = "Waiting for connection";
    }
    // Large transfer buffers are on this worker's stack, never the render stack.
    const Result createResult = threadCreate(&s_thread, server_thread, nullptr, nullptr, 256 * 1024, 0x2C, -2);
    if (R_FAILED(createResult)) { return start_error("FTP thread", createResult); }
    s_threadCreated = true;
    s_running = true;
    const Result startResult = threadStart(&s_thread);
    if (R_FAILED(startResult))
    {
        s_running = false;
        threadClose(&s_thread);
        s_threadCreated = false;
        return start_error("Start FTP thread", startResult);
    }
    logger::log("FTP: listening at %s; root sdmc:/", ftp::address().c_str());
    return true;
}

void ftp::stop()
{
    // StateManager may release an already-stopped FTP page during global
    // destruction, after this translation unit's strings/mutexes are gone.
    // These plain lifetime flags remain valid and are only owned by the UI.
    if (!s_threadCreated && !s_socketOwned && !s_nifmOwned && s_listener < 0) { return; }
    std::lock_guard guard{s_lifecycleLock};
    const bool wasStarted = s_threadCreated || s_socketOwned || s_nifmOwned;
    stop_locked(wasStarted);
    if (wasStarted) { logger::log("FTP: stopped"); }
}

bool ftp::running() { return s_running.load(); }
std::string ftp::address() { std::lock_guard guard{s_snapshotLock}; return s_address; }
std::string ftp::status() { std::lock_guard guard{s_snapshotLock}; return s_status; }
