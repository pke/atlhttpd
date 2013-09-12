#include "stdafx.h"
#include "HttpServerImpl.h"

using namespace net;
using namespace net::http;


template<class T, class Base = Servlet>
class ProtectedServletImpl : public Base {
protected:
  /*void handleAuthorizedRequest(Request& request, Response& response, const ATL::CString& mimeType) {
  }*/
  ProtectedServletImpl(const ATL::CString& realm, const ATL::CString& user, const ATL::CString& password) : 
    realm(realm), user(user), password(password) {
  }
  typedef ProtectedServletImpl<T> inherited;

private:
  void handleRequest(Request& request, Response& response, const ATL::CString& mimeType) override {
    BasicAuthentication basicAuth(request);
    if (!basicAuth.isBasic() || !basicAuth.authenticate(user, password)) {
      response.header("WWW-Authenticate", ATL::CStringA(utils::string::format(_T("Basic realm=\"%s\""), realm)))
        .status("401 Not authorized");
      response << "Permission Denied!";
    } else {
      T* pT = (T*)this;
      pT->handleAuthorizedRequest(request, response, mimeType);
    }
  }

  const ATL::CString realm;
  const ATL::CString user;
  const ATL::CString password;
};

class ProtectedServlet : public ProtectedServletImpl<ProtectedServlet> {
public:
  ProtectedServlet() : ProtectedServletImpl<ProtectedServlet>(_T("NSA HQ"), _T("NSA"), _T("FUCK!YOU!")) {
  }

  void handleAuthorizedRequest(Request& request, Response& response, const ATL::CString& mimeType) {
    if (mimeType == "json") {
      response << "{\"message\":\"You have entered the NSA HQ\"}";
    } else {
      response << "<html><h1>NSA HQ</h1><h2>Permission granted.</h2></html>";
    }
  }
};

class EchoServlet : public Servlet {
  void handleRequest(Request& request, Response& response, const ATL::CString& mimeType) {
    if (mimeType == _T("xml")) {
      //response.header("Content-Type", "text/xml; charset=UTF-8");
      response << "<?xml version=\"1.0\" encoding=\"UTF-8\"?><echo>";
      ATL::CComVariant var(request.params[_T("times")]);
      if SUCCEEDED(var.ChangeType(VT_I4)) {
        for (int i=0; i<V_I4(&var); ++i) {
          response << "<value>" << request.params[_T("text")] << "</value>";
        }
      }
      response << "</echo>";
    } else {
      response << "Invalid mime type\n";
      response << "Times = " << request.params[_T("times")];
    }
  }
};

int _tmain(int argc, _TCHAR* argv[]) {
  HttpServer server;
  EchoServlet echoServlet;
  server.addServlet("GET /echo/:times", &echoServlet);
  ProtectedServlet protectedServlet;
  server.addServlet("GET /protected", &protectedServlet);
  // server.addACL("+192.168.2.125");
  server.run(3000);
	return 0;
}