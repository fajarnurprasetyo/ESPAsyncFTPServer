#include "ESPAsyncFTPServer.h"

static String formatBytes(size_t bytes)
{
  if (bytes < 1024)
    return String(bytes) + "B";
  if (bytes < 1024 * 1024)
    return String(bytes / 1024.0) + "KB";
  if (bytes < 1024 * 1024 * 1024)
    return String(bytes / (1024.0 * 1024.0)) + "MB";
  return String(bytes / (1024.0 * 1024.0 * 1024.0)) + "GB";
}

static String formatUsage(size_t used, size_t total)
{
  return String(" (") + formatBytes(total - used) + " of " + formatBytes(total) + ")";
}

void AsyncFTPPasiveServer::_sendFile()
{
  if (_sendBufOffset == _sendBufSize)
  {
    if (!_file || !_file.available())
    {
      _client->close();
      return;
    }

    _sendBufSize = _file.read(_sendBuf, sizeof(_sendBuf));
    _sendBufOffset = 0;
  }

  size_t sent = _client->write((char *)_sendBuf + _sendBufOffset, _sendBufSize - _sendBufOffset);
  _sendBufOffset += sent;
}

void AsyncFTPPasiveServer::_sendList()
{
  if (!_file)
  {
#if FTP_USE_LITTLEFS
    if (_ftpServer->littleFSAvailable())
    {
      String usage = formatUsage(LittleFS.usedBytes(), LittleFS.totalBytes());
      _client->writeDirEntry(FTP_LITTLEFS_ROOT_PATH + usage);
    }
#endif

#if FTP_USE_SDFS
    if (_ftpServer->sdFSAvailable())
    {
      String usage = formatUsage(SD.usedBytes(), SD.totalBytes());
      _client->writeDirEntry(FTP_LITTLEFS_ROOT_PATH + usage);
    }
#endif

    _client->close();
    return;
  }

  File f = _file.openNextFile();
  if (!f)
  {
    _client->close();
    return;
  }

  _client->writeDirEntry(f);
  f.close();
}

void AsyncFTPPasiveServer::_tryStartTransfer()
{
  if (!_client || !_command)
    return;

  _controlClient->write("150 Opening data connection.");

  switch (_command)
  {
  case FTP_COMMAND_RETR:
    _sendFile();
    break;
  case FTP_COMMAND_LIST:
    _sendList();
    break;
  }
}

void AsyncFTPPasiveServer::_onClientAck(size_t len, uint32_t time)
{
  switch (_command)
  {
  case FTP_COMMAND_RETR:
    _sendFile();
    break;
  case FTP_COMMAND_LIST:
    _sendList();
    break;
  }
}

void AsyncFTPPasiveServer::_onClientData(void *data, size_t len)
{
  switch (_command)
  {
  case FTP_COMMAND_STOR:
  {
    if (!_file || !_fs)
      return;

    _remainingSpace -= len;
    if (_remainingSpace < 8192)
    {
      const char *path = _file.path();
      _file.close();
      _fs->remove(path);
      _storeSuccess = false;
      _client->close();
      return;
    }

    _file.write((uint8_t *)data, len);
  }
  break;
  }
}

void AsyncFTPPasiveServer::_onClientDisconnect()
{
  if (_command != FTP_COMMAND_NONE)
  {
    if (_file)
      _file.close();
    if (_command == FTP_COMMAND_STOR && !_storeSuccess)
      _controlClient->write("452 Insufficient storage space.");
    else
      _controlClient->write("226 Transfer complete.");
  }

  delete _client;
  _client = nullptr;
}

void AsyncFTPPasiveServer::_onClient(AsyncClient *c)
{
  if (!c)
    return;

  _client = new AsyncFTPPasiveClient(c);

  if (!_client)
  {
    c->abort();
    return;
  }

  c->onAck(
      [](void *s, AsyncClient *, size_t len, uint32_t time)
      {
        static_cast<AsyncFTPPasiveServer *>(s)->_onClientAck(len, time);
      },
      this);

  c->onData(
      [](void *s, AsyncClient *, void *data, size_t len)
      {
        static_cast<AsyncFTPPasiveServer *>(s)->_onClientData(data, len);
      },
      this);

  c->onDisconnect(
      [](void *s, AsyncClient *)
      {
        static_cast<AsyncFTPPasiveServer *>(s)->_onClientDisconnect();
      },
      this);

  _tryStartTransfer();
}

AsyncFTPPasiveServer::AsyncFTPPasiveServer(AsyncFTPServer *s, AsyncFTPClient *c, uint16_t port)
    : _ftpServer(s), _controlClient(c), _server(port)
{
  _server.onClient(
      [](void *s, AsyncClient *c)
      {
        if (!c)
          return;
        static_cast<AsyncFTPPasiveServer *>(s)->_onClient(c);
      },
      this);

  _server.begin();

  char response[64];
  IPAddress ip = _controlClient->localIP();
  snprintf(response, sizeof(response),
           "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).",
           ip[0], ip[1], ip[2], ip[3],
           port >> 8, port & 0xFF);

  _controlClient->write(response);
}

void AsyncFTPPasiveServer::setCommand(FTPCommand c, File f, FS *fs)
{
  if (_command != FTP_COMMAND_NONE)
    return;

  _command = c;
  _file = f;
  _fs = fs;

  switch (_command)
  {
  case FTP_COMMAND_STOR:
    _remainingSpace = 0;

#if FTP_USE_LITTLEFS
    if (_fs == &LittleFS)
      _remainingSpace = LittleFS.totalBytes() - LittleFS.usedBytes();
#endif

#if FTP_USE_SDFS
    else if (_fs == &SD)
      _remainingSpace = SD.totalBytes() - SD.usedBytes();
#endif

    _storeSuccess = true;
    break;
  }

  _tryStartTransfer();
}

void AsyncFTPPasiveServer::end(void)
{
  if (_client)
  {
    _client->close();
    _client = nullptr;
  }

  _server.end();
}
