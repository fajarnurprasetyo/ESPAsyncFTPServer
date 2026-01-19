#include "ESPAsyncFTPServer.h"

void AsyncFTPServer::begin(const char *user, const char *password)
{
  _user = user;
  _password = password;

#if FTP_USE_LITTLEFS
  _littleFSAvailable = LittleFS.begin();
#endif
#if FTP_USE_SDFS
  _sdFSAvailable = SD.begin();
#endif

  _server.onClient(
      [](void *s, AsyncClient *c)
      {
        if (!c)
          return;

        AsyncFTPServer *server = static_cast<AsyncFTPServer *>(s);
        AsyncFTPClient *client = new AsyncFTPClient(server, c);

        if (!client)
          c->abort();
      },
      this);

  _server.begin();
}

const char *AsyncFTPServer::user() const
{
  return _user;
}

const char *AsyncFTPServer::password() const
{
  return _password;
}

#if FTP_USE_LITTLEFS
bool AsyncFTPServer::littleFSAvailable() const
{
  return _littleFSAvailable;
}
#endif

#if FTP_USE_SDFS
bool AsyncFTPServer::sdFSAvailable() const
{
  return _sdFSAvailable;
}
#endif

bool AsyncFTPServer::resolveFsPath(const String &virtualPath, FS *&fs, String &fsPath, bool checkExists) const
{
#if FTP_USE_LITTLEFS
  if (virtualPath.startsWith(FTP_LITTLEFS_ROOT_PATH) && _littleFSAvailable)
  {
    fs = &LittleFS;
    fsPath = virtualPath.substring(virtualPath.indexOf("/", 1));
    return !checkExists || fs->exists(fsPath);
  }
#endif

#if FTP_USE_SDFS
  if (virtualPath.startsWith(FTP_SD_ROOT_PATH) && _sdFSAvailable)
  {
    fs = &LittleFS;
    fsPath = virtualPath.substring(virtualPath.indexOf("/", 1));
    return !checkExists || fs->exists(fsPath);
  }
#endif

  return false;
}

File AsyncFTPServer::resolveFile(const String &virtualPath, const char *mode, bool create)
{
  FS *fs;
  String fsPath;
  if (!resolveFsPath(virtualPath, fs, fsPath))
    return File();
  return fs->open(fsPath, mode, create);
}
