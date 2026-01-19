#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>

#define FTP_ROOT_PATH "/"

#ifndef FTP_USE_LITTLEFS
#define FTP_USE_LITTLEFS 1
#endif

#if FTP_USE_LITTLEFS
#include <LittleFS.h>
#ifndef FTP_LITTLEFS_ROOT_PATH
#define FTP_LITTLEFS_ROOT_PATH "/LittleFS"
#endif
#endif

#if FTP_USE_SDFS
#include <SD.h>
#ifndef FTP_SDFS_ROOT_PATH
#define FTP_SDFS_ROOT_PATH "/SD"
#endif
#endif

#ifndef FTP_PASV_PORT_MIN
#define FTP_PASV_PORT_MIN 49152
#endif
#ifndef FTP_PASV_PORT_MAX
#define FTP_PASV_PORT_MAX 65535
#endif

class AsyncFTPCommand;
class AsyncFTPPasiveClient;
class AsyncFTPPasiveServer;
class AsyncFTPClient;
class AsyncFTPServer;

typedef std::function<void(void *, AsyncFTPClient *)> AsyncFTPCommandHandler;

typedef enum
{
  FTP_TYPE_ASCII,
  FTP_TYPE_EBCDIC,
  FTP_TYPE_IMAGE,
  FTP_TYPE_LOCAL,
} FTPDataType;

typedef enum
{
  FTP_COMMAND_NONE,
  FTP_COMMAND_LIST,
  FTP_COMMAND_NLST,
  FTP_COMMAND_MLSD,
  FTP_COMMAND_MLST,
  FTP_COMMAND_RETR,
  FTP_COMMAND_STOR,
  FTP_COMMAND_APPE
} FTPCommand;

class AsyncFTPCommand
{
private:
  String _buffer;
  int _index = 0;

public:
  AsyncFTPCommand() = default;

  void write(const void *data, size_t len);

  bool eof(void) const;
  void skipWs(void);
  String getWord(void);
  String getRest(void);

  bool hasLine(void) const;
  String peekLine(void);
  void nextLine(void);
};

class AsyncFTPPasiveClient
{
private:
  AsyncClient *_client;

public:
  AsyncFTPPasiveClient(AsyncClient *c) : _client(c) {};

  void writeDirEntry(File &file);
  void writeDirEntry(const String &name, bool isDir = true, size_t size = 0, time_t t = 0);
  void writeDirEntry(const char *name, bool isDir = true, size_t size = 0, time_t t = 0);
  size_t write(const char *data, size_t size);

  void close();
};

class AsyncFTPPasiveServer
{
private:
  AsyncFTPServer *_ftpServer;
  AsyncFTPClient *_controlClient;

  AsyncServer _server;
  AsyncFTPPasiveClient *_client = nullptr;

  FTPCommand _command = FTP_COMMAND_NONE;
  File _file;
  FS *_fs = nullptr;

  size_t _remainingSpace;
  uint8_t _sendBuf[2048];
  size_t _sendBufSize;
  size_t _sendBufOffset;
  bool _storeSuccess;

  void _sendFile(void);
  void _sendList(void);
  void _tryStartTransfer(void);

  void _onClientAck(size_t len, uint32_t time);
  void _onClientData(void *data, size_t len);
  void _onClientDisconnect();
  void _onClient(AsyncClient *c);

public:
  AsyncFTPPasiveServer(AsyncFTPServer *s, AsyncFTPClient *c, uint16_t port);

  void setCommand(FTPCommand c, File f, FS *fs = nullptr);
  void end(void);
};

class AsyncFTPClient
{
private:
  AsyncFTPServer *_server;
  AsyncClient *_client;

  AsyncFTPCommand _command;
  bool _authenticated = false;
  String _cwd = "/";
  FTPDataType _dataType = FTP_TYPE_ASCII;
  bool _utf8 = true;

  AsyncFTPPasiveServer *_pasiveServer = nullptr;
  String _renameFromPath = "";

  void _onData(void *buf, size_t len);

  void _sendSyntaxError(void);
  void _sendBadSequence(void);

  void _authenticate(void);
  void _handleCWD(String path, bool cdup = false);

  void _handleQUIT(void);

  void _handlePASV(void);
  void _handleTYPE(void);
  void _handleOPTS(void);

  void _handleSTOR(void);
  void _handleSIZE(void);
  void _handleRETR(void);
  void _handleLIST(void);
  void _handleRNFR(void);
  void _handleRNTO(void);
  void _handleDELE(void);
  void _handleRMD(void);
  void _handleMKD(void);
  void _handlePWD(void);

  void _handleSYST(void);
  void _handleSTAT(void);
  void _handleFEAT(void);

  void _handleCommand(void);

public:
  AsyncFTPClient(AsyncFTPServer *s, AsyncClient *c);
  ~AsyncFTPClient();

  void write(String data);
  void write(const char *data);

  IPAddress localIP(void);
};

class AsyncFTPServer
{
private:
  AsyncServer _server;
  const char *_user = nullptr;
  const char *_password = nullptr;

#if FTP_USE_LITTLEFS
  bool _littleFSAvailable = false;
#endif
#if FTP_USE_SDFS
  bool _sdFSAvailable = false;
#endif

public:
  AsyncFTPServer(uint16_t port) : _server(port) {};

  void begin(const char *user, const char *password);

  const char *user(void) const;
  const char *password(void) const;

#if FTP_USE_LITTLEFS
  bool littleFSAvailable(void) const;
#endif
#if FTP_USE_SDFS
  bool sdFSAvailable(void) const;
#endif

  bool resolveFsPath(const String &virtualPath, FS *&fs, String &fsPath, bool checkExists = false) const;
  File resolveFile(const String &virtualPath, const char *mode = FILE_READ, bool create = false);
};
