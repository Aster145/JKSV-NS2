#pragma once
#include <string>

// Foreground, single-client FTP service. The only exported filesystem is sdmc:/.
// Call start/stop from the UI thread. Snapshots are safe to read while it runs.
namespace ftp
{
    bool start();
    void stop();
    bool running();
    // Complete URL, including port 1337; empty while stopped.
    std::string address();
    std::string status();
}
