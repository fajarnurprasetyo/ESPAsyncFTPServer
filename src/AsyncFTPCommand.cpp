#include "ESPAsyncFTPServer.h"

void AsyncFTPCommand::write(const void *data, size_t len)
{
  if (!data || !len)
    return;
  _buffer.concat((const char *)data, len);
}

bool AsyncFTPCommand::eof() const
{
  return _index >= _buffer.length();
}

void AsyncFTPCommand::skipWs()
{
  while (!eof() && _buffer[_index] == ' ')
    _index++;
}

String AsyncFTPCommand::getWord()
{
  if (eof())
    return "";

  skipWs();

  size_t start = _index;

  while (!eof())
  {
    char c = _buffer[_index];
    if (c == ' ' || c == '\r' || c == '\n')
      break;
    _index++;
  }

  return _buffer.substring(start, _index);
}

String AsyncFTPCommand::getRest()
{
  if (eof())
    return "";

  skipWs();

  int lineEnd = _buffer.indexOf("\r\n", _index);
  if (lineEnd < 0)
    lineEnd = _buffer.length();

  String rest = _buffer.substring(_index, lineEnd);
  _index = lineEnd;

  return rest;
}

bool AsyncFTPCommand::hasLine() const
{
  return _buffer.indexOf("\r\n") >= 0;
}

String AsyncFTPCommand::peekLine()
{
  return _buffer.substring(0, _buffer.indexOf("\r\n"));
}

void AsyncFTPCommand::nextLine()
{
  int end = _buffer.indexOf("\r\n");
  if (end >= 0)
  {
    _buffer.remove(0, end + 2);
    _index = 0;
  }
}
