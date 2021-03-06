/*
cppcryptfs : user-mode cryptographic virtual overlay filesystem.

Copyright (C) 2016-2019 Bailey Brown (github.com/bailey27/cppcryptfs)

cppcryptfs is based on the design of gocryptfs (github.com/rfjakob/gocryptfs)

The MIT License (MIT)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "stdafx.h"

#include <stdio.h>

#include "util/util.h"
#include "crypt/cryptdefs.h"
#include "crypt/crypt.h"
#include <openssl/rand.h>

#include <iostream>

#include "crypt/randombytes.h"
#include "context/cryptcontext.h"

#ifdef _WIN32
#include <wincrypt.h>
#include <tlhelp32.h>
#include <atlenc.h>
#include <shellapi.h>
#endif


template<typename T>
T swapBytesVal(T x)
{
	T y;
	char* px = (char*)&x;
	char* py = (char*)&y;
	for (int i = 0; i<sizeof(T); i++)
		py[i] = px[sizeof(T) - 1 - i];
	return y;
}

template<typename T>
T MakeBigEndian(T n)
{

	if (!IsBigEndianMachine()) {
		return swapBytesVal(n);
	} else {
		return n;
	}
}

template unsigned long long MakeBigEndian(unsigned long long);

template<typename T>
T MakeBigEndianNative(T n)
{
	return MakeBigEndian(n);
}

template unsigned short MakeBigEndianNative(unsigned short);

template unsigned long long MakeBigEndianNative(unsigned long long);

BOOL
IsBigEndianMachine()
{
	const unsigned int a = 1;

	const unsigned char *p = (const unsigned char *)&a;

	return !p[0];
}

const char *
unicode_to_utf8(const WCHAR *unicode_str, char *buf, int buflen)
{
	int len = WideCharToMultiByte(CP_UTF8, 0, unicode_str, -1, NULL, 0, NULL, NULL);

	if (len == 0)
		return NULL;

	// len includes space for null char
	if (len > buflen)
		return NULL;

	if (WideCharToMultiByte(CP_UTF8, 0, unicode_str, -1, buf, len, NULL, NULL) == 0) {
		return NULL;
	}

	return buf;
}

const char *
unicode_to_utf8(const WCHAR *unicode_str, string& storage)
{

	int len = WideCharToMultiByte(CP_UTF8, 0, unicode_str, -1, NULL, 0, NULL, NULL);

	if (len == 0)
		return NULL;

	// len includes space for null char
	char *p_utf8 = new char[len];

	if (!p_utf8)
		return NULL;

	if (WideCharToMultiByte(CP_UTF8, 0, unicode_str, -1, p_utf8, len, NULL, NULL) == 0) {
		delete[] p_utf8;
		return NULL;
	}

	storage = p_utf8;

	delete[] p_utf8;

	return &storage[0];
}


const WCHAR *
utf8_to_unicode(const char *utf8_str, wstring& storage)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);

	if (len == 0)
		return NULL;

	// len includes space for null char
	WCHAR *p_unicode = new WCHAR[len];

	if (!p_unicode)
		return NULL;

	if (MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, p_unicode, len) == 0) {
		delete[] p_unicode;
		return NULL;
	}

	storage = p_unicode;

	delete[] p_unicode;

	return &storage[0];
}

static const char *
add_base64_padding(const char *str, string& storage)
{
	// storage won't contain padded string if no padding needed

	size_t len = strlen(str);

	if (len % 4 == 0)
		return str;

	size_t npad = 4 - len % 4;

	storage.reserve(len + npad);

	storage = str;

	storage.resize(len + npad, '=');

	return storage.c_str();
}

static const char *
remove_base64_padding(string& str)
{

	while (str.length() > 0 && str[str.length() - 1] == '=')
		str.pop_back();

	return str.c_str();
}

bool
base64_decode(const char *str, vector<unsigned char>& storage, bool urlTransform, bool padding)
{
	if (!urlTransform) {
		_ASSERT(padding);
	}

	string padded_storage;

	if (!padding)
		str = add_base64_padding(str, padded_storage);

	size_t str_len;

	if (!str || (str_len = strlen(str)) < 1)
		return false;
	
	char *p = NULL;
	
	if (urlTransform) {

		p = _strdup(str);

		if (!p)
			return false;
	}

	if (urlTransform) {	

		size_t i;
		for (i = 0; i < str_len; i++) {
			if (p[i] == '-')
				p[i] = '+';
			else if (p[i] == '_')
				p[i] = '/';
		}
	}

	DWORD len = (DWORD)str_len;

	BOOL bResult = FALSE;

	try {

		storage.resize(len);

		// CryptStringToBinary() is supposedly a little faster than ATL Base64Decode()

		bResult = CryptStringToBinaryA(p ? p : str, 0, CRYPT_STRING_BASE64, &storage[0], &len, NULL, NULL);

	} catch (...) {
		bResult = FALSE;
	}

	if (p)
		::free(p);

	if (bResult) {
		storage.resize(len);
		return true;
	} else {
		return false;
	}
}

bool
base64_decode(const WCHAR *str, vector<unsigned char>& storage, bool urlTransform, bool padding)
{

	// profiling shows that the WCHAR versions of the windows
	// base64 conversions just convert to utf8 anyway

	string utf8;

	size_t len = wcslen(str);

	bool error = false;

	try {

		// can do trivial conversion to utf8 because it's a base64 string
		utf8.reserve(len);

		size_t i;

		for (i = 0; i < len; i++) {
			utf8.push_back((char)str[i]);
		}
	} catch (...) {
		error = true;
	}

	if (error)
		return false;

	return base64_decode(&utf8[0], storage, urlTransform, padding);
}


const char *
base64_encode(const BYTE *data, DWORD datalen, string& storage, bool urlTransform, bool padding)
{

	if (!urlTransform) {
		_ASSERT(padding);
	}

	if (!data || datalen < 1)
		return NULL;

	int base64len = (int)datalen*2;

	char *base64str = NULL;

	BOOL bResult = FALSE;

	try {

		base64str = new char[base64len + 1];

		// ATL Base64Encode() is supposedly way faster than CryptBinaryToString()
	
		bResult = Base64Encode(data, (int)datalen, base64str, &base64len, ATL_BASE64_FLAG_NOCRLF);

		// ATL Base64Encode() doesn't null terminate the string
		if (bResult)
			base64str[base64len] = '\0';

	} catch (...) {
		bResult = FALSE;
	}
	if (bResult) {

		if (urlTransform) {
			size_t len = strlen(base64str);
			size_t i;
			for (i = 0; i < len; i++) {
				if (base64str[i] == '+')
					base64str[i] = '-';
				else if (base64str[i] == '/')
					base64str[i] = '_';
			}
		}
		storage = base64str;
		delete[] base64str;
		if (!padding)
			remove_base64_padding(storage);
		return storage.c_str();
	} else {
		if (base64str)
			delete[] base64str;
		return NULL;
	}
}

const WCHAR *
base64_encode(const BYTE *data, DWORD datalen, wstring& storage, bool urlTransform, bool padding)
{

	// profiling shows that the WCHAR versions of the windows
	// base64 conversion functions just convert from utf8 anyway

	const WCHAR *rs = NULL;

	try {
		string utf8;

		if (base64_encode(data, datalen, utf8, urlTransform, padding)) {

			// can do trivial conversion to unicode because it's a base64 string

			size_t len = utf8.size();

			storage.clear();

			storage.reserve(len);

			size_t i;

			for (i = 0; i < len; i++)
				storage.push_back(utf8[i]);

			rs = &storage[0];

		} else {
			throw (-1);
		}
	} catch (...) {
		rs = NULL;
	}

	return rs;
}

bool read_password(WCHAR *pwbuf, int pwbuflen, const WCHAR *prompt)
{
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);

	if (!hStdin)
		return false;

	wcout << (prompt ? prompt : L"Password: ");

	DWORD old_mode = 0;
	if (!GetConsoleMode(hStdin, &old_mode))
		return false;

	if (!SetConsoleMode(hStdin, old_mode & (~ENABLE_ECHO_INPUT)))
		return false;

	DWORD nRead = 0;

	ReadConsole(hStdin, pwbuf, pwbuflen - 1, &nRead, NULL);

	if (nRead >= 2 && pwbuf[nRead - 2] == L'\r' && pwbuf[nRead - 1] == L'\n')
		pwbuf[nRead - 2] = '\0';

	SetConsoleMode(hStdin, old_mode);

	wcout << L"\n";

	return true;
}


bool
get_sys_random_bytes(unsigned char *buf, DWORD len)
{
	
	HCRYPTPROV hProvider = NULL;

	if (!CryptAcquireContextW(&hProvider, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
		return false;
	}

	if (!CryptGenRandom(hProvider, len, buf)) {
		CryptReleaseContext(hProvider, 0);
		return false;
	}

	CryptReleaseContext(hProvider, 0);

	return true;

}



bool
get_random_bytes(CryptContext *con, unsigned char *buf, DWORD len)
{
	if (con)
		return con->m_prand_bytes->GetRandomBytes(buf, len);
	else
		return get_sys_random_bytes(buf, len);
}


DWORD 
getppid()
{
	HANDLE hSnapshot;
	PROCESSENTRY32 pe32;
	DWORD ppid = 0, pid = GetCurrentProcessId();

	hSnapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	__try{
		if( hSnapshot == INVALID_HANDLE_VALUE ) __leave;

		ZeroMemory( &pe32, sizeof( pe32 ) );
		pe32.dwSize = sizeof( pe32 );
		if( !Process32First( hSnapshot, &pe32 ) ) __leave;

		do{
			if( pe32.th32ProcessID == pid ){
				ppid = pe32.th32ParentProcessID;
				break;
			}
		}while( Process32Next( hSnapshot, &pe32 ) );

	}
	__finally{
		if( hSnapshot != INVALID_HANDLE_VALUE ) CloseHandle( hSnapshot );
	}
	return ppid;
}


bool 
have_args()
{
	int argc = 1;

	LPCWSTR cmdLine = GetCommandLineW();

	LPTSTR *argv = NULL;

	if (cmdLine)
		argv = CommandLineToArgvW(cmdLine, &argc);

	if (argv)
		LocalFree(argv);
	else
		argc = 1;

	return argc > 1;
}

bool 
OpenConsole(DWORD pid)
{
	FreeConsole();

	if (AttachConsole(pid ? pid : ATTACH_PARENT_PROCESS)) {
#pragma warning( push )
#pragma warning(disable : 4996)
		if (!freopen("CONOUT$", "wt", stdout)) {
			FreeConsole();
			return false;
		}
		if (!freopen("CONOUT$", "wt", stderr)) {
			fclose(stdout);
			FreeConsole();
			return false;
		}
#pragma warning( pop )
		return true;
	} else {
		return false;
	}
}

void
CloseConsole()
{
	fclose(stderr);
	fclose(stdout);
	
	FreeConsole();
}

void 
ConsoleErrMes(LPCWSTR err, DWORD pid)
{
	if (OpenConsole(pid)) {
		fwprintf(stderr, L"cppcryptfs: %s\n", err);
		CloseConsole();
	}
}


bool 
GetProductVersionInfo(wstring& strProductName, wstring& strProductVersion,
	wstring& strLegalCopyright, HMODULE hMod)
{

	TCHAR fullPath[MAX_PATH+1];
	*fullPath = L'\0';
	if (!GetModuleFileName(hMod, fullPath, MAX_PATH)) {
		return false;
	}
	DWORD dummy = 0;
	DWORD vSize = GetFileVersionInfoSize(fullPath, &dummy);
	if (vSize < 1) {
		return false;
	}

	void *pVersionResource = NULL;

	pVersionResource = malloc(vSize);

	if (pVersionResource == NULL)
	{
		return false;
	}

	if (!GetFileVersionInfo(fullPath, NULL, vSize, pVersionResource)) {
		free(pVersionResource);
		return false;
	}

	// get the name and version strings
	LPVOID pvProductName = NULL;
	unsigned int iProductNameLen = 0;
	LPVOID pvProductVersion = NULL;
	unsigned int iProductVersionLen = 0;
	LPVOID pvLegalCopyright = NULL;
	unsigned int iLegalCopyrightLen = 0;

	struct LANGANDCODEPAGE {
		WORD wLanguage;
		WORD wCodePage;
	} *lpTranslate;

	// Read the list of languages and code pages.
	unsigned int cbTranslate;
	if (!VerQueryValue(pVersionResource,
		TEXT("\\VarFileInfo\\Translation"),
		(LPVOID*)&lpTranslate,
		&cbTranslate)) {

		return false;
	}

	if (cbTranslate/sizeof(struct LANGANDCODEPAGE) < 1) {
		return false;
	}

	wstring lang;

	WCHAR buf[16];

	// use the first language/codepage;

	wsprintf(buf, L"%04x%04x", lpTranslate->wLanguage, lpTranslate->wCodePage);

	lang = buf;

	// replace "040904e4" with the language ID of your resources
	if (!VerQueryValue(pVersionResource, (L"\\StringFileInfo\\" + lang + L"\\ProductName").c_str(), &pvProductName, &iProductNameLen) ||
		!VerQueryValue(pVersionResource, (L"\\StringFileInfo\\" + lang + L"\\ProductVersion").c_str(), &pvProductVersion, &iProductVersionLen) ||
		!VerQueryValue(pVersionResource, (L"\\StringFileInfo\\" + lang + L"\\LegalCopyright").c_str(), &pvLegalCopyright, &iLegalCopyrightLen))
	{
		free(pVersionResource);
		return false;
	}

	if (iProductNameLen < 1 || iProductVersionLen < 1 || iLegalCopyrightLen < 1) {
		free(pVersionResource);
		return false;
	}

	strProductName = (LPCTSTR)pvProductName;
	strProductVersion = (LPCTSTR)pvProductVersion;
	strLegalCopyright = (LPCTSTR)pvLegalCopyright;

	free(pVersionResource);

	return true;
}


bool touppercase(LPCWSTR in, wstring& out)
{
	WCHAR sbuf[MAX_PATH]; // if longer then use heap
	WCHAR *hbuf = NULL;
	WCHAR *buf;

	bool bRet = true;

	try {

		size_t len = wcslen(in);

		if (len < sizeof(sbuf) / sizeof(sbuf[0])) {
			buf = sbuf;
		} else {

			hbuf = new WCHAR[len + 1];

			buf = hbuf;
		}

		wcscpy_s(buf, len+1, in);

		CharUpperBuff(buf, (DWORD)len);

		out = buf;

	} catch (...) {
		bRet = false;
	}

	if (hbuf)
		delete[] hbuf;

	return bRet;
}

int compare_names(CryptContext *con, LPCWSTR name1, LPCWSTR name2)
{
	if (con->IsCaseInsensitive()) {
		return lstrcmpi(name1, name2);
	} else {
		return wcscmp(name1, name2);
	}
}

template <typename T> bool test_zero_bytes(const BYTE *buf, size_t len)
{
	return *((T *)buf) == 0 && !memcmp(buf, buf + sizeof(T), len - sizeof(T));
}

#define TESTZERO(T) \
	if ((UINT_PTR)buf % sizeof(T) == 0) { \
		return test_zero_bytes<T>(buf, len); \
	}

bool is_all_zeros(const BYTE *buf, size_t len)
{

	if (len <= sizeof(uint64_t)) {
		const BYTE zeros[sizeof(uint64_t)] = { 0 };
		return !memcmp(buf, zeros, len);
	}

	TESTZERO(uint64_t);

	TESTZERO(uint32_t);

	TESTZERO(uint16_t);
	
	return test_zero_bytes<uint8_t>(buf, len);
}



BOOL GetPathHash(LPCWSTR path, wstring& hashstr)
{

	hashstr = L"";

	wstring ucpath;

	if (!touppercase(path, ucpath))
		return FALSE;

	size_t len = ucpath.length();

	while (len > 3) {
		
		if (ucpath[len - 1] == '\\') {
			ucpath.resize(len - 1);
		} else {
			break;
		}
		len = ucpath.length();
	}

	if (len < 1)
		return FALSE;

	path = ucpath.c_str();

	string str;

	if (!unicode_to_utf8(path, str))
		return FALSE;

	BYTE sum[32];

	if (!sha256(str, sum))
		return FALSE;

	int i;

	// use only 128bits of the sha256 to keep registry key length shorter

	for (i = 0; i < 16; i++) {
		WCHAR buf[3];
		swprintf_s(buf, L"%02x", sum[i]);
		hashstr += buf;
	}

	return TRUE;
}

wstring GetWindowsErrorString(DWORD dwLastErr)
{
	wstring mes;

	if (dwLastErr == 0) {
		mes += L"unknown windows error 0";
		return mes;
	}

	LPTSTR errorText = NULL;

	if (!::FormatMessageW(
		// use system message tables to retrieve error text
		FORMAT_MESSAGE_FROM_SYSTEM
		// allocate buffer on local heap for error text
		| FORMAT_MESSAGE_ALLOCATE_BUFFER
		// Important! will fail otherwise, since we're not 
		// (and CANNOT) pass insertion parameters
		| FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,    // unused with FORMAT_MESSAGE_FROM_SYSTEM
		dwLastErr,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&errorText,  // output 
		0, // minimum size for output buffer
		NULL)) {   // arguments - see note 

		mes += L"unable to get message for error " + to_wstring(dwLastErr);

		if (errorText) {
			LocalFree(errorText);
		}

		return mes;
	} 

	if (errorText) {
		// ... do something with the string `errorText` - log it, display it to the user, etc.
		mes += errorText;
		// release memory allocated by FormatMessage()
		LocalFree(errorText);
		errorText = NULL;
	} else {
		mes += L"got null message for error " + to_wstring(dwLastErr);
	}

	return mes;
}

void SetOverlapped(LPOVERLAPPED pOv, LONGLONG offset)
{
	LARGE_INTEGER l;

	l.QuadPart = offset;

	memset(pOv, 0, sizeof(*pOv));

	pOv->Offset = l.LowPart;
	pOv->OffsetHigh = l.HighPart;
}

void IncOverlapped(LPOVERLAPPED pOv, DWORD increment)
{
	LARGE_INTEGER l;

	l.LowPart = pOv->Offset;
	l.HighPart = pOv->OffsetHigh;

	l.QuadPart += increment;

	memset(pOv, 0, sizeof(*pOv));

	pOv->Offset = l.LowPart;
	pOv->OffsetHigh = l.HighPart;
}

