#pragma once

#include <atlstr.h>
#include <atlutil.h>
#include <wininet.h>
#include "atlrx.h"

#include "Socket.h"
#include "utils/string/join.h"
#include "utils/string/base64.h"

#pragma comment(lib, "wininet")
#pragma comment(lib, "Urlmon") // FindMimeFromData

// Disable IP restrictions
// Saves 1KiB on exe size
#define NO_ACL


namespace utils { namespace string {

  static ATL::CString format(PCTSTR text, ...) {
    va_list args;
    va_start(args, text);
    ATL::CString result;
    result.FormatV(text, args);
    return result;
  }

  template <class T>
  ATL::CString inspect(const T& a) {
    ATL::CString result;
    POSITION pos = a.GetStartPosition();
    while (pos) {
      result += "\t";
      const T::CPair* pair = a.GetNext(pos);
      result += pair->m_key;
      result += "=>";
      result += pair->m_value;
      result += "\n";
    }
    return result;
  }
}} // utils::string


namespace net { namespace http {

  class Request {
  public:
    ATL::CStringA method;
    ATL::CAtlMap<ATL::CStringA, ATL::CStringA> headers;
    ATL::CAtlMap<ATL::CString, ATL::CString> params;
    ATL::CUrl url;

    ATL::CString inspect() const {
      ATL::CString result("Method: ");
      result += method;
      result += "\n";

      result += "Path: ";
      result += url.GetUrlPath();
      result += "\n";

      result += "Headers: [\n";
      result += utils::string::inspect(headers);
      result += "]\n";

      if (params.GetCount()) {
        result += "Params: [\n";
        result += utils::string::inspect(params);
        result += "]\n";
      }

      return result;
    }
  };

  class BasicAuthentication {
  public:
    BasicAuthentication(const Request& request) : request(request), valid(false) {
      const ATL::CAtlMap<ATL::CStringA, ATL::CStringA>::CPair* pos = request.headers.Lookup("Authorization");
      if (pos && strncmp(pos->m_value, "Basic", 5) == 0) {
        credentials = pos->m_value.Mid(6);
      }
    }

    bool isBasic() {
      return !credentials.IsEmpty();
    }

    bool authenticate(PCTSTR user, PCTSTR password) {
      return utils::string::toBase64(ATL::CStringA(utils::string::format(_T("%s:%s"), user, password))) == credentials;
    }

  private:
    bool valid;
    const Request& request;
    ATL::CStringA credentials;
  };

  class Response {
  public:
    /**
      Sets the status to "200 OK" and the following default headers:
      * Connection: close
      * Server: The Dude's Server
      * Date: current system date
    */
    Response(const Request& request, Socket& s) : request(request), s(s), _status("200 OK"), sent(0) {
      header("Connection", "close");
      header("Server", "The Dude's Server");
      SYSTEMTIME time;
      GetSystemTime(&time);
      // API states, that the buffer must be INTERNET_RFC1123_BUFSIZE in size. But this would be too small and return ERROR_INSUFFICIENT_BUFFER
      CHAR timeString[INTERNET_RFC1123_BUFSIZE+1];
      ATLVERIFY(InternetTimeFromSystemTimeA(&time, INTERNET_RFC1123_FORMAT, timeString, sizeof(timeString)));
      header("Date", timeString);
    }

    /**
        Sets the status code and message for this Response object.
        @return a reference to itself for chaining.
    */
    Response& status(PCSTR status) {
      _status = status;
      return *this;
    }

    /**
        Performs a send "400 Bad Request" with the current response object.

        @return always 400
        @see send()
    */
    int badRequest() {
      return status("400 Bad Request").send();
    }

    Response& operator << (PCSTR text) {
      _body += text;
      return *this;
    }

    Response& operator << (PCWSTR text) {
      _body += text;
      return *this;
    }

    Response& operator << (const ATL::CStringA& text) {
      _body += text;
      return *this;
    }

    Response& operator << (const ATL::CStringW& text) {
      _body += text;
      return *this;
    }

    /**
      Used to add or remove a HTTP response header.

      @param name of the header
      @param value if set to <code>null</code> it will remove the header

      @return a reference to itself for chaining.
    */
    Response& header(PCSTR name, PCSTR value) {
      if (value) {
        headers.SetAt(name, value);
      } else {
        headers.RemoveKey(name);
      }
      return *this;
    }

    /**
      Sends this response to the socket and closes the socket.

      @param data additional data that is sent after the body content
      @param dataLen size of the data in bytes.

      @remarks
      This method does nothing if the response has already been sent.

      @return the HTTP status code
    */
    int send(const char* data = 0, const int dataLen = 0) {
      if (!sent) {
        sent = ::GetTickCount();

        if (!headers.Lookup("Content-Type")) {
          PWSTR mimeType = 0;
          // FUCK YOU MSFT!
          // According to a user (!) comment in MSDN, the buffer needs to be writable, or the function might crash. WTF!?
          // Anyway, 256 bytes should be enough to sniff
          BYTE buffer[256];
          DWORD size = min(_body.GetLength(), 256);
          memcpy_s(buffer, 256, _body.GetBuffer(), size);
          if SUCCEEDED(FindMimeFromData(0, 
            0, 
            buffer, 
            size, 
            0, 
            FMFD_ENABLEMIMESNIFFING | 0x00000020/*FMFD_RETURNUPDATEDIMGMIMES*/, 
            &mimeType, 
            0)) {
            header("Content-Type", ATL::CStringA(mimeType));
            ::CoTaskMemFree((LPVOID)mimeType);
          }
        }
        header("Content-Length", ATL::CStringA(utils::string::format(_T("%d"), _body.GetLength() + dataLen)));
        
        s.send("HTTP/1.1 ");
        s.sendLine(_status);
        
        POSITION pos = headers.GetStartPosition();
        while (pos) {
          ATL::CAtlMap<ATL::CStringA, ATL::CStringA>::CPair* header = headers.GetNext(pos);
          s.send(header->m_key);
          s.send(": ");
          s.sendLine(header->m_value);
        }      

        s.sendLine("");
        if (request.method != "HEAD") {
          s.send(_body);
          if (data && dataLen) {
            s.send(data, dataLen);
          }
        }
        s.close();
      }
      return atoi(_status);
    }

  private:
    const Request& request;
    Socket& s;
    DWORD sent; // time at which the response was sent
    ATL::CStringA _status;
    ATL::CStringA _body;
    ATL::CAtlMap<ATL::CStringA, ATL::CStringA> headers;
  };


  /**
    Interface all Servlets must implement.
  */
  __interface Servlet {
    /**
        @param extension (.ext) part of a resource request. GET /books.xml would yield "xml"
    */
    void handleRequest(Request& request, Response& response, const ATL::CString& extension);
  };

  /**
    Very simple, unfinished HTTP 1.0 server.
    
    Hacked together in one day.

    Runtime memory consumption ~30KiB.

    @author_philk
  */
  class HttpServer {
  public:
    HttpServer() : running(true) {
    }

    /**
      converts 
      GET /cufa-web/rest/voice/experts/:expert/dailyprogram
      into
      GET /cufa-web/rest/voice/experts/{\w}/dailyprogram

      and 
      GET /cufa-web/rest/voice/car/:serialNumber{\\d+}/inspection
      into
      GET /cufa-web/rest/voice/car/{\\d+}/inspection

      The named parameters are accessible in a Servlet using request.params["serialNumber"]
    */
    void addServlet(const ATL::CStringA& path, Servlet* servlet) {
      servlets.AddTail(ATL::CAutoPtr<Entry>(new Entry(path, servlet)));    
    }

    void addACL(PCSTR rule) {
#ifndef NO_ACL
      int a, b, c, d, n, mask = 32;
      char flag;
      if (5 != sscanf(rule, "%c%d.%d.%d.%d%n", &flag, &a, &b, &c, &d, &n)) {
        // subnet must be [+|-]x.x.x.x[/x]
        return;
      } else if (flag != '+' && flag != '-') {
        // flag must be + or -
        return;
      } else if (!isByte(a)||!isByte(b)||!isByte(c)||!isByte(d)) {
        // bad IP address
        return;
      } else if (0 == sscanf(rule + n, "/%d", &mask)) {
        // Just catched the mask
      } else if (mask < 0 || mask > 32) {
        // bad subnet mask
        return;
      }

      acl.Add(ACLRule((a << 24) | (b << 16) | (c << 8) | d, mask ? 0xffffffffU << (32 - mask) : 0, flag));
#endif
    }

    void run(int port) {
      ServerSocket s(port, 5);

      while (running) {
        Socket* newSocket = s.accept();
        if (newSocket) {
          if (!allowed(*newSocket)) {
            newSocket->close();
            delete newSocket;
          } else {
            RequestHandler::start(newSocket, *this);
          }
        }
      }
    }

    void stop() {
      running = false;
    }

  private:
    typedef ATL::CAtlRegExp<ATL::CAtlRECharTraitsA> RegEx;
    typedef ATL::CAtlREMatchContext<ATL::CAtlRECharTraitsA> MatchContext;

    static __inline bool isByte(int b) {
      return b>=0 && b<256;
    }

    bool allowed(const Socket& socket) {
#ifdef NO_ACL
      return true;
#else
      // if acl contains any entries than deny by default
      if (!acl.GetSize()) {
        return true;
      }

      char allowed = '-';
      unsigned int check;
      memcpy_s(&check, sizeof(check), &socket.addr().sin_addr, sizeof(check));
      check = ::ntohl(check);

      for (int i=0,len=acl.GetSize();i<len;++i) {
        acl[i].check(check, allowed);
      }

      ATLTRACE(_T("Connection from %s %s\n"), socket.address(), (allowed == '+' ? _T("allowed") : _T("refused")));
      return allowed == '+';
#endif
    }

    static ATL::CStringA getMatch(MatchContext& match, const int group) {
      const MatchContext::RECHAR* start = 0;
      const MatchContext::RECHAR* end = 0;
      match.GetMatch(group, &start, &end);
      const int len = (int)(end - start);
      return ATL::CStringA(start, len);
    }

    struct Entry {
      RegEx regex;
      Servlet* servlet;
      ATL::CSimpleArray<ATL::CStringA> paramNames;

      Entry(const ATL::CStringA& path, Servlet* servlet) : servlet(servlet) {
        RegEx regEx;
        regEx.Parse("{\\:\\w}{(\\{.+\\})?}", false);

        ATL::CStringA servletRegEx("^");

        MatchContext match;
        const MatchContext::RECHAR* start = 0;
        const MatchContext::RECHAR* end = 0;
        const MatchContext::RECHAR* e = path;
        const MatchContext::RECHAR* lastPos = e;
        
        while (regEx.Match(e, &match, &e)) {
          match.GetMatch(0, &start, &end);
          // Add +1 cause the match group does include the colon ":"
          paramNames.Add(ATL::CStringA(start+1, (int)(end-start)-1));

          int prevPartLen = int(start-lastPos);
          servletRegEx += ATL::CStringA(lastPos, prevPartLen);
          match.GetMatch(1, &start, &end);
          int len = int(end - start);
          if (!len) {
            servletRegEx += "{\\a+}";
          } else {
            servletRegEx += ATL::CString(start, len);
          }
          lastPos = end;
        }
        servletRegEx += lastPos;
        // Add MIME-type detection at the end.
        servletRegEx += "(\\.{.+})?$";
        this->regex.Parse(servletRegEx);
      }
    };

    void handleRequest(Request& request, Response& response) {
      POSITION pos = servlets.GetHeadPosition();
      const ATL::CStringA methodAndPath(request.method + " " + ATL::CStringA(request.url.GetUrlPath()));
      while (pos) {
        const ATL::CAutoPtr<Entry>& entry = servlets.GetNext(pos);
        MatchContext match;
        if (entry.m_p->regex.Match(methodAndPath, &match)) {
          ATL::CSimpleArray<ATL::CString> args;
          for (UINT i=0; i<match.m_uNumGroups-1;++i) {
            request.params.SetAt(ATL::CString(entry.m_p->paramNames[i]), ATL::CString(getMatch(match, i)));
          }
          // Try to extract the MIME-Type from the extension if not explicitly set by header
          ATL::CStringA mimeType;
          if (!request.headers.Lookup("Content-Type", mimeType) || mimeType.IsEmpty()) {
            mimeType = getMatch(match, match.m_uNumGroups-1);
            if (mimeType.IsEmpty()) {
              mimeType = "text/plain";
            }
          }
          entry.m_p->servlet->handleRequest(request, response, ATL::CString(mimeType));
          return;
        }
      }
      // Or return 405 Method not allowed here?
      response.status("404 Not Found");
    }

    typedef ATL::CAutoPtrList<Entry> ServletList;

    struct ACLRule {
      const unsigned int subnet;
      const unsigned int mask;
      const char flag;

      ACLRule(unsigned int subnet, unsigned int mask, char flag) : subnet(subnet), mask(mask), flag(flag) {}

      void check(int addr, char& flag) {
        if (subnet == (addr & mask)) {
          flag = this->flag;
        }
      }
    };
    ATL::CSimpleArray<ACLRule> acl;

    ServletList servlets;
    friend class RequestHandler;

  private:
    volatile bool running;

    class RequestHandler {
      static DWORD CALLBACK threadStartProc(LPVOID data) {
        DWORD result = ((RequestHandler*)data)->run();
        delete (RequestHandler*)data;
        return result;
      }

      RequestHandler(Socket* s, HttpServer& server) : startTime(::GetTickCount()), s(s), server(server) {        
      }

    public:
      static void start(Socket* s, HttpServer& server) {
        ATL::CHandle threadHandle(CreateThread(0, 0, threadStartProc, new RequestHandler(s, server), 0, 0));
      }

      ~RequestHandler() {
        delete s;
        DWORD totalTime = ::GetTickCount() - startTime;
        ATLTRACE(_T("Request completed in %3.2f sec\n"), totalTime * 0.001f);
      }

      static ATL::CString urlDecode(const ATL::CString& encoded) {
        DWORD decodedLen = 0;
        ATL::CString decoded;
        decoded.Preallocate(encoded.GetAllocLength());
        BOOL b = AtlUnescapeUrl(encoded, decoded.GetBuffer(), &decodedLen, decoded.GetAllocLength());
        if (decodedLen > (DWORD)decoded.GetAllocLength()) {
          decoded.Preallocate(decodedLen);
          b = AtlUnescapeUrl(encoded, decoded.GetBuffer(), &decodedLen, decoded.GetAllocLength());
        }
        decoded.ReleaseBufferSetLength(decodedLen);
        return decoded;
      }

      DWORD run() {
        ATLTRACE(_T("Request from %s:%d\n"), s->address(), s->port());

        RegEx headerExp;
        headerExp.Parse("^({(GET)|(POST)|(HEAD)|(DELETE)|(PUT)|(UPDATE)}) ({.*}) (HTTP/{1\\.\\d})\\n$");

        Request request;
        Response response(request, *s);
        
        ATL::CStringA line(s->receiveLine());
        ATLTRACE(_T("%s: %S\n"), s->address(), line);
        MatchContext match;
        if (!headerExp.Match(line, &match)) {          
          return response.badRequest();
        }

        request.method = getMatch(match, 0);
        ATL::CStringA path = getMatch(match, 1);
        const ATL::CStringA httpVersion(getMatch(match, 2));
        
        while (1) {
          line = s->receiveLine().TrimRight("\r\n");
          if (line.IsEmpty()) {
            break;
          }
          const int colPos = line.Find(": ");
          if (colPos <= 0) {
            return response.badRequest();
          }
          request.headers.SetAt(line.Left(colPos), line.Mid(colPos+2));
        }

        if (httpVersion == "1.1") {
          if (!request.headers.Lookup("Host")) {
            response << "HTTP 1.1 requests must include the Host: header.";
            return response.status("505 HTTP Version Not Supported").send();
          }
        } else if (httpVersion != "1.0") {
          return response.badRequest();
        }

        request.url.CrackUrl(utils::string::format(_T("http://%S%S"), request.headers["Host"], path));
        
        if (request.url.GetExtraInfo()[0] == _T('?') || request.url.GetExtraInfo()[0] == _T('#')) {
          ATL::CSimpleArray<ATL::CString> params(utils::string::split<ATL::CString>(request.url.GetExtraInfo()+1, ATL::CString(_T("&"))));
          for (int i=0,len=params.GetSize();i<len;++i) {
            ATL::CSimpleArray<ATL::CString> param(utils::string::split<ATL::CString>(params[i], ATL::CString(_T("="))));
            request.params.SetAt(urlDecode(param[0]), (param.GetSize() > 1) ? urlDecode(param[1]) : _T(""));
          }
        }
        ATLTRACE(_T("%s: %s\n"), s->address(), request.inspect());

        server.handleRequest(request, response);
        
        return response.send();
      }

    private:
      DWORD startTime;
      Socket* s;
      HttpServer& server;
    };
  };

}} // net::http