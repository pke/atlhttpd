#pragma once

namespace utils { namespace string {

template <typename T>
static ATL::CSimpleArray<T> split(const T& str, const T& delimiter) {
  T text(str);
  const int len = text.GetLength();
  int start = 0;
  ATL::CSimpleArray<T> items;
  do {
    items.Add(T(text.Tokenize(delimiter, start)));
  } while (start != -1 && start<len);
  return items;
}

template <class T, class Composer>
ATL::CString joinMap(const T& a, Composer c, PCTSTR delimiter = _T(",")) {
  ATL::CString result;
  POSITION pos = a.GetStartPosition();
  while (pos) {
    const T::CPair* pair = a.GetNext(pos);
    result += c(pair->m_key, pair->m_value);
    if (pos && delimiter) {
      result += delimiter;
    }
  }
  return result;
}

}}