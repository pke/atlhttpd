#include "stdafx.h"
#include "HttpServerImpl.h"

using namespace net;
using namespace net::http;


template<class T, class Base = Servlet>
class ProtectedServletImpl : public Base {
protected:
  void handleAuthorizedRequest(Request& request, Response& response, const ATL::CString& mimeType) {
  }

private:
  void handleRequest(Request& request, Response& response, const ATL::CString& mimeType) {
    BasicAuthentication basicAuth(request);
    if (!basicAuth.isBasic() || !basicAuth.authenticate(_T("root"), _T("puma"))) {
      response.header("WWW-Authenticate", "Basic realm=\"Test Server\"")
        .status("401 Not authorized");
      response << "Please authenticate!";
    } else {
      T* pT = (T*)this;
      pT->handleAuthorizedRequest(request, response, mimeType);
    }
  }
};

class EchoServlet : public Servlet {
  void handleRequest(Request& request, Response& response, const ATL::CString& mimeType) {
    if (mimeType == _T("xml")) {
      response.header("Content-Type", "text/xml; charset=UTF-8");
      response << utils::string::format(_T("<?xml version=\"1.0\" encoding=\"UTF-8\"?><echo>"));
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
  server.addServlet("GET /echo/:times", &EchoServlet());
  // server.addACL("+192.168.2.125");
  server.run(3000);
	return 0;
}