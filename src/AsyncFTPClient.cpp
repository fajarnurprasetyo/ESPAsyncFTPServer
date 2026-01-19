#include "ESPAsyncFTPServer.h"

void AsyncFTPClient::_onData(void *buf, size_t len)
{
  _command.write((char *)buf, len);
  while (_command.hasLine())
  {
    _handleCommand();
    _command.nextLine();
  }
}

void AsyncFTPClient::_sendSyntaxError()
{
  write("501 Syntax error in parameters or arguments.");
}

void AsyncFTPClient::_sendBadSequence()
{
  write("503 Bad sequence of commands");
}

void AsyncFTPClient::_authenticate()
{
  String cmd = _command.getWord();

  if (cmd.equalsIgnoreCase("USER"))
  {
    String user = _command.getRest();

    if (user.isEmpty())
    {
      _sendSyntaxError();
      return;
    }

    if (user == _server->user())
    {
      write("331 User name okay, need password.");
      return;
    }

    if (user == "anonymous")
    {
      write("332 Need account for login.");
      return;
    }
  }
  else if (cmd.equalsIgnoreCase("PASS"))
  {
    String pass = _command.getRest();

    if (pass.isEmpty())
    {
      _sendSyntaxError();
      return;
    }

    if (pass == _server->password())
    {
      _authenticated = true;
      write("230 User logged in, proceed.");
      return;
    }
  }

  write("530 Not logged in.");
}

static String normalizePath(const String &input)
{
  String path = input;
  path.replace("\\", "/");

  if (!path.startsWith("/"))
    path = "/" + path;

  String out = "/";
  int len = path.length();
  int i = 1;

  while (i < len)
  {
    while (i < len && path[i] == '/')
      i++;

    if (i >= len)
      break;

    int start = i;
    while (i < len && path[i] != '/')
      i++;

    String token = path.substring(start, i);

    if (token != ".")
    {
      if (token == "..")
      {
        int last = out.lastIndexOf('/', out.length() - 2);
        if (last >= 0)
          out = out.substring(0, last + 1);
      }
      else
      {
        out += token;
        out += "/";
      }
    }
  }

  return out;
}

void AsyncFTPClient::_handleCWD(String path, bool cdup)
{
  if (path.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  if (path == ".")
    path = _cwd;
  else if (!path.startsWith("/"))
    path = _cwd + path;

  path = normalizePath(path);

  bool success = false;

  if (path == FTP_ROOT_PATH)
    success = true;
  else
  {
    File file = _server->resolveFile(path);
    if (file)
    {
      success = file.isDirectory();
      file.close();
    }
  }

  if (success)
  {
    _cwd = path;
    if (cdup)
      write("200 Working directory changed.");
    else
      write("250 Working directory changed.");
  }
  else
    write("550 Directory not found.");
}

void AsyncFTPClient::_handleQUIT()
{
  write("221 Goodbye.");
  _client->close();
}

void AsyncFTPClient::_handlePASV()
{
  if (_pasiveServer)
    _pasiveServer->end();

  uint16_t port = random(FTP_PASV_PORT_MIN, FTP_PASV_PORT_MAX);
  _pasiveServer = new AsyncFTPPasiveServer(_server, this, port);

  if (!_pasiveServer)
    write("425 Can't open data connection.");
}

void AsyncFTPClient::_handleTYPE()
{
  String type = _command.getWord();

  if (type.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  if (type.equalsIgnoreCase("A"))
    _dataType = FTP_TYPE_ASCII;
  else if (type.equalsIgnoreCase("E"))
    _dataType = FTP_TYPE_EBCDIC;
  else if (type.equalsIgnoreCase("I"))
    _dataType = FTP_TYPE_IMAGE;
  else if (type.equalsIgnoreCase("L"))
    _dataType = FTP_TYPE_LOCAL;
  else
  {
    write("504 Unknown data type " + type);
    return;
  }

  write("200 Type set to " + type);
}

void AsyncFTPClient::_handleOPTS()
{
  String type = _command.getWord();
  String state = _command.getWord();
  type.toUpperCase();
  state.toUpperCase();

  bool enabled = state == "ON";

  if (type == "UTF8")
    _utf8 = enabled;

  write("200 " + type + " " + state);
}

void AsyncFTPClient::_handleSTOR()
{
  if (!_pasiveServer)
  {
    _sendBadSequence();
    return;
  }

  String path = _command.getRest();

  if (path.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  if (_cwd == FTP_ROOT_PATH)
  {
    write("550 Cannot write to read-only directory.");
    return;
  }

  FS *fs;
  String fsPath;
  if (!_server->resolveFsPath(_cwd + path, fs, fsPath))
  {
    write("451 Local error in processing.");
    return;
  }

  File file = fs->open(fsPath, FILE_WRITE);
  if (!file)
  {
    write("553 Cannot open file for writing.");
    _pasiveServer->end();
  }
  else
    _pasiveServer->setCommand(FTP_COMMAND_STOR, file, fs);
}

void AsyncFTPClient::_handleSIZE()
{
  String path = _command.getRest();

  if (path.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  path = _cwd + path;

  FS *fs;
  String fsPath;
  if (!_server->resolveFsPath(path, fs, fsPath))
  {
    write("450 File not found.");
    return;
  }

  File file = fs->open(fsPath);
  if (!file)
  {
    write("450 File not found.");
    return;
  }

  size_t size = file.size();
  file.close();

  write("213 " + String(size));
}

void AsyncFTPClient::_handleRETR()
{
  String path = _command.getRest();

  if (path.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  path = _cwd + path;

  FS *fs;
  String fsPath;
  if (!_server->resolveFsPath(path, fs, fsPath))
  {
    write("451 Local error in processing.");
    return;
  }

  File file = fs->open(fsPath, FILE_READ);
  if (!file)
    write("450 File not found.");
  else
    _pasiveServer->setCommand(FTP_COMMAND_RETR, file);
}

void AsyncFTPClient::_handleLIST()
{
  if (!_pasiveServer)
  {
    _sendBadSequence();
    return;
  }

  String path = _command.getRest();

  if (path.isEmpty())
    path = _cwd;

  File file;
  if (path != FTP_ROOT_PATH)
    file = _server->resolveFile(path);
  _pasiveServer->setCommand(FTP_COMMAND_LIST, file);
}

void AsyncFTPClient::_handleRNFR()
{
  String path = _command.getRest();

  if (path.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  path = _cwd + path;

  FS *fs;
  String fsPath;
  if (!_server->resolveFsPath(path, fs, fsPath))
    write("451 Local error in processing.");
  else if (!fs->exists(fsPath))
    write("550 File not found.");
  else
  {
    _renameFromPath = path;
    write("350 Ready for rename.");
  }
}

void AsyncFTPClient::_handleRNTO()
{
  if (_renameFromPath.isEmpty())
  {
    _sendBadSequence();
    return;
  }

  String path = _command.getRest();

  if (path.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  path = _cwd + path;

  FS *dstFs;
  String dstPath;
  if (!_server->resolveFsPath(path, dstFs, dstPath))
    write("451 Local error in processing.");
  else if (dstFs->exists(dstPath))
    write("553 Destination file already exists.");
  else
  {
    FS *srcFs;
    String srcPath;
    _server->resolveFsPath(_renameFromPath, srcFs, srcPath);

    bool success = false;

    if (dstFs != srcFs)
    {
      File srcFile = srcFs->open(srcPath);
      File dstFile = dstFs->open(dstPath, FILE_WRITE);

      if (srcFile && dstFile)
      {
        uint8_t buf[2048];
        while (srcFile.available())
        {
          size_t size = srcFile.read(buf, sizeof(buf));
          dstFile.write(buf, size);
        }
        srcFile.close();
        dstFile.close();
        srcFs->remove(srcPath);
        success = true;
      }
    }
    else
      success = srcFs->rename(srcPath, dstPath);

    _renameFromPath = "";
    if (success)
      write("250 File renamed successfully.");
    else
      write("450 Rename failed.");
  }
}

void AsyncFTPClient::_handleDELE()
{
  String path = _command.getRest();

  if (path.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  FS *fs;
  String fsPath;
  if (!_server->resolveFsPath(_cwd + path, fs, fsPath))
    write("451 Local error in processing.");
  else if (!fs->exists(fsPath))
    write("550 File not found.");
  else if (!fs->remove(fsPath))
    write("450 Cannot delete file.");
  else
    write("250 File deleted successfully.");
}

void AsyncFTPClient::_handleRMD()
{
  String path = _command.getRest();

  if (path.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  path = _cwd + path;

  FS *fs;
  String fsPath;
  if (!_server->resolveFsPath(path, fs, fsPath))
    write("451 Local error in processing.");
  else if (!fs->exists(fsPath))
    write("550 File not found.");
  else if (!fs->rmdir(fsPath))
    write("550 Failed to delete directory.");
  else
    write("250 Directory succesfully deleted.");
}

void AsyncFTPClient::_handleMKD()
{
  String path = _command.getRest();

  if (path.isEmpty())
  {
    _sendSyntaxError();
    return;
  }

  path = _cwd + path;

  FS *fs;
  String fsPath;
  if (!_server->resolveFsPath(path, fs, fsPath))
    write("451 Local error in processing.");
  else if (fs->exists(fsPath))
    write("553 File name already exists.");
  else if (!fs->mkdir(fsPath))
    write("550 Failed to create directory.");
  else
    write("257 \"" + path + "\" created.");
}

void AsyncFTPClient::_handlePWD()
{
  write("257 \"" + _cwd + "\" is current directory.");
}

void AsyncFTPClient::_handleSYST()
{
  write("215 UNIX Type: L8.");
}

void AsyncFTPClient::_handleSTAT()
{
}

void AsyncFTPClient::_handleFEAT()
{
  write("211-Features:\r\n");
  write(" PASV\r\n");
  write(" SIZE\r\n");
  write(" UTF8\r\n");
  write(" TVFS\r\n");
  write("211 End\r\n");
}

void AsyncFTPClient::_handleCommand()
{
  // Login
  if (!_authenticated)
  {
    _authenticate();
    return;
  }

  String cmd = _command.getWord();

  // Serial.print("FTP: ");
  // Serial.print(_command.peekLine());

  if (cmd.equalsIgnoreCase("CWD"))
  {
    String path = _command.getRest();
    _handleCWD(path);
  }
  else if (cmd.equalsIgnoreCase("CDUP"))
    _handleCWD("..");

  // Logout
  else if (cmd.equalsIgnoreCase("QUIT"))
    _handleQUIT();

  // Transfer parameters
  else if (cmd.equalsIgnoreCase("PASV"))
    _handlePASV();
  else if (cmd.equalsIgnoreCase("TYPE"))
    _handleTYPE();
  else if (cmd.equalsIgnoreCase("OPTS"))
    _handleOPTS();

  // File action commands
  else if (cmd.equalsIgnoreCase("STOR"))
    _handleSTOR();
  else if (cmd.equalsIgnoreCase("SIZE"))
    _handleSIZE();
  else if (cmd.equalsIgnoreCase("RETR"))
    _handleRETR();
  else if (cmd.equalsIgnoreCase("LIST"))
    _handleLIST();
  else if (cmd.equalsIgnoreCase("RNFR"))
    _handleRNFR();
  else if (cmd.equalsIgnoreCase("RNTO"))
    _handleRNTO();
  else if (cmd.equalsIgnoreCase("DELE"))
    _handleDELE();
  else if (cmd.equalsIgnoreCase("RMD"))
    _handleRMD();
  else if (cmd.equalsIgnoreCase("MKD"))
    _handleMKD();
  else if (cmd.equalsIgnoreCase("PWD"))
    _handlePWD();

  // Informational commands
  else if (cmd.equalsIgnoreCase("SYST"))
    _handleSYST();
  else if (cmd.equalsIgnoreCase("FEAT"))
    _handleFEAT();

  // Miscellaneous commands
  else if (cmd.equalsIgnoreCase("NOOP"))
    write("200 Ok.");

  // Not implemented commands
  else
  {
    // Serial.print(" (not implemented)");
    write("502 Command not implemented.");
  }

  // Serial.println();
}

AsyncFTPClient::AsyncFTPClient(AsyncFTPServer *s, AsyncClient *c)
    : _server(s), _client(c)
{
  c->onTimeout(
      [](void *, AsyncClient *c, uint32_t)
      {
        c->close();
      },
      this);

  c->onData(
      [](void *s, AsyncClient *, void *buf, size_t len)
      {
        // async_ws_log_e("AsyncFTPClient::_onData");
        static_cast<AsyncFTPClient *>(s)->_onData(buf, len);
      },
      this);

  c->onDisconnect(
      [](void *s, AsyncClient *)
      {
        // async_ws_log_e("AsyncFTPClient::_onDisconnect");
        delete static_cast<AsyncFTPClient *>(s);
      },
      this);

  write("220 Service ready for new user.");
}

AsyncFTPClient::~AsyncFTPClient()
{
  if (_pasiveServer)
  {
    _pasiveServer->end();
    delete _pasiveServer;
    _pasiveServer = nullptr;
  }

  if (_client)
    _client = nullptr;
}

void AsyncFTPClient::write(String message)
{
  write(message.c_str());
}

void AsyncFTPClient::write(const char *data)
{
  char response[strlen(data) + 3];
  sprintf(response, "%s\r\n", data);
  _client->write(response);
}

IPAddress AsyncFTPClient::localIP()
{
  return _client->localIP();
}
