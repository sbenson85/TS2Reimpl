// The iostream templates. With the static runtime (/MT) libcpmt.lib holds only the non-template parts (locale, ios_base,
// the char facets' specialisations), so the game's basic_filebuf / fstream / stream / num_get / num_put code is its own
// instantiation of the 14.40 headers (cRZIFStream and cRZOFStream sit on std::ifstream / std::ofstream). Compiled by
// stlprobe.ps1 like probe.cpp.
#include <cstdint>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <streambuf>
#include <string>

template class std::basic_streambuf<char>;
template class std::basic_ios<char>;
template class std::basic_istream<char>;
template class std::basic_ostream<char>;
template class std::basic_iostream<char>;
template class std::basic_filebuf<char>;
template class std::basic_ifstream<char>;
template class std::basic_ofstream<char>;
template class std::basic_fstream<char>;
template class std::basic_stringbuf<char>;
template class std::basic_istringstream<char>;
template class std::basic_ostringstream<char>;
template class std::basic_stringstream<char>;
template class std::num_get<char>;
template class std::num_put<char>;
template class std::istreambuf_iterator<char>;
template class std::ostreambuf_iterator<char>;

template class std::basic_streambuf<wchar_t>;
template class std::basic_ios<wchar_t>;
template class std::basic_istream<wchar_t>;
template class std::basic_ostream<wchar_t>;
template class std::basic_filebuf<wchar_t>;
template class std::basic_ifstream<wchar_t>;
template class std::basic_ofstream<wchar_t>;
template class std::basic_stringbuf<wchar_t>;
template class std::num_get<wchar_t>;
template class std::num_put<wchar_t>;

// Function templates and members the classes above only reach inline: the facet lookups, the extractors and
// inserters for the arithmetic and string types, getline.
const std::num_get<char> &ProbeUseNumGet(const std::locale &l) { return std::use_facet<std::num_get<char>>(l); }
const std::num_put<char> &ProbeUseNumPut(const std::locale &l) { return std::use_facet<std::num_put<char>>(l); }
const std::ctype<char> &ProbeUseCtype(const std::locale &l) { return std::use_facet<std::ctype<char>>(l); }
const std::codecvt<char, char, _Mbstatet> &ProbeUseCodecvt(const std::locale &l)
{
    return std::use_facet<std::codecvt<char, char, _Mbstatet>>(l);
}
std::istream &ProbeGetline(std::istream &s, std::string &t) { return std::getline(s, t); }
std::istream &ProbeGetlineDelim(std::istream &s, std::string &t, char c) { return std::getline(s, t, c); }
std::istream &ProbeReadString(std::istream &s, std::string &t) { return s >> t; }
std::ostream &ProbeWriteString(std::ostream &s, const std::string &t) { return s << t; }
std::ostream &ProbeWriteText(std::ostream &s, const char *t) { return s << t; }
std::ostream &ProbeWriteChar(std::ostream &s, char c) { return s << c; }
std::ostream &ProbeEndl(std::ostream &s) { return s << std::endl; }
std::ifstream *ProbeNewIfstream(const char *name, std::ios_base::openmode m) { return new std::ifstream(name, m); }
std::ofstream *ProbeNewOfstream(const char *name, std::ios_base::openmode m) { return new std::ofstream(name, m); }
std::fstream *ProbeNewFstream(const char *name, std::ios_base::openmode m) { return new std::fstream(name, m); }
std::string ProbeStringOf(const std::ostringstream &s) { return s.str(); }
