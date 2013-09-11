#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#ifdef _WIN32_WCE
#pragma comment(lib, "ws2")
#else
#pragma comment(lib, "wsock32")
#endif

namespace net {

  class Socket {
  public:
    enum Type {
      Blocking, 
      Nonblocking
    };

    operator SOCKET() {
      return s;
    }

    virtual ~Socket() {
      close();

      --sockets();
      if (!sockets()) {
        ::WSACleanup();
      }
    }

    ATL::CStringA receiveLine() {
      ATL::CStringA ret;

      while (1) {
        char r;

        switch(::recv(s, &r, 1, 0)) {
      case 0: // not connected anymore
        return ret;
      case -1:
        return "";
        }

        ret += r;
        if (r == '\n') {
          return ret;
        }
      }
    }

    ATL::CHeapPtr<byte> receiveBytes() {
      ATL::CHeapPtr<byte> buffer;
      buffer.Allocate(1024);
      size_t size = 1024;
      size_t pos = 0;
      while (1) {
        u_long arg = 0;
        if (::ioctlsocket(s, FIONREAD, &arg) != 0)
          break;

        if (arg == 0)
          break;

        if (arg > size) {
          buffer.Reallocate(arg);
          size = arg;
        }

        int read = ::recv(s, (char*)buffer.m_pData+pos, arg, 0);
        if (read <= 0) break;
        pos += read;
      }
      return buffer;
    }

    void close() {
      // Send FIN to the client
      if (s != INVALID_SOCKET) {
        ::shutdown(s, /*SHUT_WR*/1);
        u_long arg = 1;
        ::ioctlsocket(s, FIONBIO, &arg);
        // Read and discard pending data. If we do not do that and close the
        // socket, the data in the send buffer may be discarded. This
        // behaviour is seen on Windows, when client keeps sending data
        // when server decide to close the connection; then when client
        // does recv() it gets no data back.
        int read;
        char buffer[1024];
        do {
          read = ::recv(s, buffer, sizeof(buffer), 0);
        } while (read > 0);
        ::closesocket(s);
        s = INVALID_SOCKET;
      }
    }

    void sendLine(const ATL::CStringA& line) {
      ATL::CStringA fullLine(line);
      fullLine += '\n';
      send(fullLine);
    }

    void send(const ATL::CStringA& data) {
      send((char*)data.GetString(), data.GetLength());
    }

    void send(const char* data, const int len) {
      ::send(s, data, len, 0);
    }

    ATL::CString address() const {
      return ATL::CString(::inet_ntoa(ua.u.sin.sin_addr));
    }

    int port() const {
      return ua.u.sin.sin_port;
    }

    const sockaddr_in& addr() const {
      return ua.u.sin;
    }

  protected:
    // Unified socket address. For IPv6 support, add IPv6 address structure
    // in the union u.
    struct UnifiedSocketAddress {
      socklen_t len;
      union {
        struct sockaddr sa;
        struct sockaddr_in sin;
      } u;

      UnifiedSocketAddress() : len(sizeof(u.sin)) {}
    };
    friend class ServerSocket;

    Socket(SOCKET s, const UnifiedSocketAddress ua) : s(s), ua(ua) {
      init();
      ++sockets();
    }

    Socket() : s(INVALID_SOCKET) {
      init();
      ++sockets();
    }

    SOCKET s;
    const UnifiedSocketAddress ua;

  private:
    Socket(const Socket&);
    Socket& operator=(Socket&);

    static void init() {
      if (!sockets()) {
        WSADATA info;
        if (::WSAStartup(MAKEWORD(2,0), &info)) {
          throw "Could not start WSA";
        }
      }
    }

    static int& sockets() {
      static int s = 0;
      return s;
    }
  };

  class ServerSocket : public Socket {
  public:
    ServerSocket(int port, int connections, Socket::Type type = Blocking) {
      sockaddr_in sa;
      memset(&sa, 0, sizeof(sa));

      sa.sin_family = PF_INET;             
      sa.sin_port = ::htons(port);          
      s = ::socket(AF_INET, SOCK_STREAM, 0);
      if (INVALID_SOCKET == s) {
        throw "INVALID_SOCKET";
      }

      if (Nonblocking == type) {
        u_long arg = 1;
        ::ioctlsocket(s, FIONBIO, &arg);
      }

      if (SOCKET_ERROR == bind(s, (sockaddr *)&sa, sizeof(sockaddr_in))) {
        close();
        throw "INVALID_SOCKET";
      }
      ::listen(s, connections);   
    }

    Socket* accept() {
      UnifiedSocketAddress address;
      
      SOCKET newSocket = ::accept(s, &address.u.sa, &address.len);
      if (INVALID_SOCKET == newSocket) {
        int rc = WSAGetLastError();
        if (WSAEWOULDBLOCK == rc) {
          return 0; // non-blocking call, no request pending
        } else {
          throw "Invalid Socket";
        }
      }
      return new Socket(newSocket, address);
    }
  };

} // net