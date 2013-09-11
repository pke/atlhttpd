#pragma once

#include <atlstr.h>
#include <atlenc.h>

namespace utils { namespace string {

static ATL::CStringA toBase64(const void* bytes, int byteLength, DWORD flags = ATL_BASE64_FLAG_NONE) {
  ATLASSERT(bytes);

  ATL::CStringA base64;
  int base64Length = ATL::Base64EncodeGetRequiredLength(byteLength);

  ATLVERIFY(ATL::Base64Encode(static_cast<const BYTE*>(bytes),
    byteLength,
    base64.GetBufferSetLength(base64Length),
    &base64Length));

  base64.ReleaseBufferSetLength(base64Length);
  return base64;
}

template<class T>
static ATL::CStringA toBase64(const T& string) {
  return toBase64((const void*)string.GetString(), string.GetLength() * sizeof(T::XCHAR));
}

}}