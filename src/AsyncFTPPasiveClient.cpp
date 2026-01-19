#include "ESPAsyncFTPServer.h"

static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static String formatDirEntry(const char *name, bool isDir, size_t size, time_t t)
{
  char line[128];

  char date[16];
  if (t == 0)
  {
    strcpy(date, "Jan  1 00:00");
  }
  else
  {
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (tm_info.tm_year == tm_now.tm_year)
    {
      snprintf(date, sizeof(date), "%s %2d %02d:%02d",
               MONTHS[tm_info.tm_mon],
               tm_info.tm_mday,
               tm_info.tm_hour,
               tm_info.tm_min);
    }
    else
    {
      snprintf(date, sizeof(date), "%s %2d  %4d",
               MONTHS[tm_info.tm_mon],
               tm_info.tm_mday,
               tm_info.tm_year + 1900);
    }
  }

  const char *base = (name[0] == '/') ? name + 1 : name;

  if (isDir)
    snprintf(line, sizeof(line),
             "drwxr-xr-x 1 user group %8d %s %s\r\n",
             0, date, base);
  else
    snprintf(line, sizeof(line),
             "-rw-r--r-- 1 user group %8u %s %s\r\n",
             (unsigned)size, date, base);

  return line;
}

void AsyncFTPPasiveClient::writeDirEntry(File &file)
{
  writeDirEntry(file.name(), file.isDirectory(), file.size(), file.getLastWrite());
}

// void AsyncFTPPasiveClient::writeDirEntry(const char *name, size_t used, size_t total)
// {
//   writeDirEntry(name, used, total);
// }

// void AsyncFTPPasiveClient::writeDirEntry(const String &name, size_t used, size_t total)
// {
//   writeDirEntry(name + " (" + formatBytes(total - used) + " free of " + formatBytes(total) + ")");
// }

void AsyncFTPPasiveClient::writeDirEntry(const String &name, bool isDir, size_t size, time_t t)
{
  writeDirEntry(name.c_str(), isDir, size, t);
}

void AsyncFTPPasiveClient::writeDirEntry(const char *name, bool isDir, size_t size, time_t t)
{
  String entry = formatDirEntry(name, isDir, size, t);
  _client->write(entry.c_str());
}

size_t AsyncFTPPasiveClient::write(const char *data, size_t size)
{
  return _client->write(data, size);
}

void AsyncFTPPasiveClient::close()
{
  _client->close();
}
