#include <string>
#include <valarray>
#include <regex>
#include <list>
#include <thread>
#include <cctype>
#include <sstream>
#include <map>

#define VERSION "hais/1.2"

#define DEBUG

#ifndef DEBUG
#define printf
#endif

#ifdef _MSC_VER

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <tchar.h>
#include <strsafe.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "User32.lib")

#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable : 4996)

#define err(st, msg, ...)\
{\
	fprintf(stderr, msg, __VA_ARGS__);\
	exit(st);\
}

#define close closesocket

void DisplayError(const char* msg)
{
	LPVOID lpMsgBuf;
	const auto dw = GetLastError();
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0,
		nullptr);
	const auto lpDisplayBuf =
		static_cast<LPVOID>(LocalAlloc(
			LMEM_ZEROINIT,
			(lstrlen(static_cast<LPCTSTR>(lpMsgBuf)) +
				lstrlen(static_cast<LPCTSTR>(msg)) + 40) * sizeof(TCHAR)));
	StringCchPrintf(
		static_cast<LPTSTR>(lpDisplayBuf),
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("%s failed with error %d: %s"),
		msg, dw, lpMsgBuf);
	fprintf(stderr, "%s\n", static_cast<const char*>(lpDisplayBuf));
	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
}

std::string ToUnixPath(const char* uri)
{
	auto r = std::string("/");
	r.append(uri, 1);
	r.append(uri + 2);
	for (auto& c : r)
	{
		if (c == '\\') c = '/';
	}
	return r;
}

std::string ToWindowsPath(const char* uri)
{
	auto r = std::string(uri + 1, 1);
	r.append(":");
	r.append(uri + 2);
	for (auto& c : r)
	{
		if (c == '/') c = '\\';
	}
	return r;
}

#else

#include <arpa/inet.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <err.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <csignal>

#endif

std::valarray<uint8_t> UrlEncodeTableGenerate()
{
	std::valarray<uint8_t> html5(256);
	std::generate(begin(html5), end(html5), [&, i = -1]() mutable
	{
		++i;
		return isalnum(i) || i == '*' || i == '-' || i == '.' || i == '_' ? i : 0;
	});
	return html5;
}

static auto UrlEncodeTable = UrlEncodeTableGenerate();

int FileExists(const char* path)
{
#ifdef _MSC_VER
	const auto attr = GetFileAttributes(path);
	return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat sb {};
	return !stat(path, &sb) && S_ISREG(sb.st_mode);
#endif
}

int DirectoryExists(const char* path)
{
#ifdef _MSC_VER
	const auto attr = GetFileAttributes(path);
	return attr != INVALID_FILE_ATTRIBUTES && attr & FILE_ATTRIBUTE_DIRECTORY;
#else
	struct stat sb {};
	return !stat(path, &sb) && S_ISDIR(sb.st_mode);
#endif
}

uint64_t FileSize(const char* path)
{
#ifdef _MSC_VER
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
	{
		fprintf(stderr, "%s Can't get file size", path);
		return 0;
	}
	LARGE_INTEGER size;
	size.HighPart = fad.nFileSizeHigh;
	size.LowPart = fad.nFileSizeLow;
	return size.QuadPart;
#else
	struct stat sb {};
	stat(path, &sb);
	return sb.st_size;
#endif
}

std::string FileLastModified(const char* path)
{
	time_t raw;
//#ifdef _MSC_VER
//	#define Oops()\
//	{\
//		DisplayError("Get last modification time of file");\
//		return nullptr;\
//	}
//	auto fh = CreateFile(
//		path,
//		GENERIC_READ | FILE_WRITE_ATTRIBUTES,
//		0,
//		NULL,
//		OPEN_EXISTING,
//		0,
//		NULL);
//	if (fh == INVALID_HANDLE_VALUE) Oops();
//	FILETIME ft;
//	if (GetFileTime(fh, NULL, NULL, &ft) == 0) Oops();
//	ULARGE_INTEGER ull;
//	ull.LowPart = ft.dwLowDateTime;
//	ull.HighPart = ft.dwHighDateTime;
//	raw = ull.QuadPart / 10000000ULL - 11644473600ULL;
//#else
	struct stat sb {};
	stat(path, &sb);
	raw = sb.st_mtime;
//#endif
	char res[35];
	strftime(res, 34, "%a, %d %b %G %T GMT", gmtime(&raw));
	return res;
}

std::string UrlEncode(const char* s, const uint16_t len)
{
#define ToHex(x) ((x) > 9 ? (x) + 55 : (x) + 48)
	auto res = new char[len * 3];
	auto _res = res;
	const auto end = s + len;
	for (; s < end; ++s)
	{
		const auto t = UrlEncodeTable[static_cast<uint8_t>(*s)];
		if (t)
		{
			*res++ = t;
			continue;
		}
		*res++ = '%';
		*res++ = ToHex(static_cast<uint8_t>(*s) >> 4);
		*res++ = ToHex(static_cast<uint8_t>(*s) % 16);
	}
	*res = 0;
	auto r = std::string(_res);
	delete[] _res;
	return r;
}

std::string UrlDecode(const char* s, const uint16_t len)
{
	const auto dec = new char[len + 1];
	const auto end = s + len;
	int c;
	auto o = dec;
	for (; s < end; o++)
	{
		c = *s++;
		if (c == '+') c = ' ';
		else if (c == '%')
		{
			*s++;
			*s++;
			sscanf(s - 2, "%2x", &c);
		}
		*o = c;
	}
	*o = 0;
	const auto r = std::string(dec);
	delete[] dec;
	return r;
}

std::string PathCombine(const char* lp, const char* rp)
{
#ifdef _MSC_VER
#define SplitChar "\\"
#else
#define SplitChar "/"
#endif
	auto path = std::string(lp);
	if (path[path.length() - 1] != SplitChar[0] && rp[0] != SplitChar[0])
	{
		path.append(SplitChar);
	}
	path.append(rp);
	return path;
}

void GetFiles(const char* path, std::ostringstream& dirs, std::ostringstream& files)
{
#define AddFile(oss, href, display, size) \
	((oss) << "<tr><td><a href=\"" << (href) << "\">" << (display) << "</a></td><td align=\"right\">" << (size) << "</td></tr>");
#define AddDir(oss, href, display) \
	((oss) << "<a href=\"" << (href) << "\">" << (display) << "</a><br/>")
#ifdef _MSC_VER
	WIN32_FIND_DATA ffd;
	LARGE_INTEGER filesize;
	size_t lengthOfArg;
	auto hFind = INVALID_HANDLE_VALUE;
	DWORD dwError = 0;
	StringCchLength(path, MAX_PATH, &lengthOfArg);
	if (lengthOfArg > (MAX_PATH - 3))
	err(EXIT_FAILURE, "Filename too long");
	const auto szDir = PathCombine(path, "*");
	hFind = FindFirstFile(szDir.c_str(), &ffd);
	if (INVALID_HANDLE_VALUE == hFind) DisplayError(szDir.c_str());
	do
	{
		if (!strcmp(ffd.cFileName, ".") || !strcmp(ffd.cFileName, "..")) continue;
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			auto dir = PathCombine(ffd.cFileName, "");
			auto href = ToUnixPath(PathCombine(path, dir.c_str()).c_str());
			AddDir(dirs, UrlEncode(href.c_str(), href.length()), dir);
		}
		else
		{
			filesize.LowPart = ffd.nFileSizeLow;
			filesize.HighPart = ffd.nFileSizeHigh;
			auto href = ToUnixPath(PathCombine(path, ffd.cFileName).c_str());
			AddFile(
				files,
				UrlEncode(href.c_str(), href.length()),
				ffd.cFileName,
				std::to_string(filesize.QuadPart));
		}
	}
	while (FindNextFile(hFind, &ffd) != 0);
	dwError = GetLastError();
	if (dwError != ERROR_NO_MORE_FILES) DisplayError(szDir.c_str());
	FindClose(hFind);
#else
	struct dirent* dent;
	struct stat st {};
	char fn[FILENAME_MAX] = { 0 };
	auto len = strlen(path);
	if (len >= FILENAME_MAX - 1) err(EXIT_FAILURE, "Filename too long");
	strcpy(fn, path);
	if (fn[len - 1] != '/')fn[len++] = '/';
	const auto dir = opendir(path);
	if (!dir) err(EXIT_FAILURE, "Can't open %s", path);
	while ((dent = readdir(dir)))
	{
		if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")) continue;
		strncpy(fn + len, dent->d_name, FILENAME_MAX - len);
		if (lstat(fn, &st) == -1)
		{
			warn("Can't stat %s", fn);
			continue;
		}
		auto _fn = std::string(fn);
		if (S_ISDIR(st.st_mode))
		{
			auto href = PathCombine(fn, "");
			AddDir(dirs, UrlEncode(href.c_str(), href.length()), PathCombine(_fn.substr(len).c_str(), ""));
		}
		else
		{
			AddFile(files, UrlEncode(_fn.c_str(), _fn.length()), _fn.substr(len), std::to_string(FileSize(_fn.c_str())));
		}
	}
	if (dir) closedir(dir);
#endif
}

#define GetHttpUrl(url, http, sm)\
	std::regex_search(http, sm, std::regex("(POST|GET) .+? HTTP"));\
	const auto (url) = std::regex_replace((sm)[0].str(), std::regex("(POST |GET | HTTP|)"), "")

static const std::map<std::string, std::string> ContentTypeTable =
{
	{"tif", "image/tiff"},
	{"001", "application/x-001"},
	{"301", "application/x-301"},
	{"323", "text/h323"},
	{"906", "application/x-906"},
	{"907", "drawing/907"},
	{"a11", "application/x-a11"},
	{"acp", "audio/x-mei-aac"},
	{"ai", "application/postscript"},
	{"aif", "audio/aiff"},
	{"aifc", "audio/aiff"},
	{"aiff", "audio/aiff"},
	{"anv", "application/x-anv"},
	{"asa", "text/asa"},
	{"asf", "video/x-ms-asf"},
	{"asp", "text/asp"},
	{"asx", "video/x-ms-asf"},
	{"au", "audio/basic"},
	{"avi", "video/avi"},
	{"awf", "application/vnd.adobe.workflow"},
	{"biz", "text/xml"},
	{"bmp", "application/x-bmp"},
	{"bot", "application/x-bot"},
	{"c4t", "application/x-c4t"},
	{"c90", "application/x-c90"},
	{"cal", "application/x-cals"},
	{"cat", "application/vnd.ms-pki.seccat"},
	{"cdf", "application/x-netcdf"},
	{"cdr", "application/x-cdr"},
	{"cel", "application/x-cel"},
	{"cer", "application/x-x509-ca-cert"},
	{"cg4", "application/x-g4"},
	{"cgm", "application/x-cgm"},
	{"cit", "application/x-cit"},
	{"class", "java/*"},
	{"cml", "text/xml"},
	{"cmp", "application/x-cmp"},
	{"cmx", "application/x-cmx"},
	{"cot", "application/x-cot"},
	{"crl", "application/pkix-crl"},
	{"crt", "application/x-x509-ca-cert"},
	{"csi", "application/x-csi"},
	{"css", "text/css"},
	{"cut", "application/x-cut"},
	{"dbf", "application/x-dbf"},
	{"dbm", "application/x-dbm"},
	{"dbx", "application/x-dbx"},
	{"dcd", "text/xml"},
	{"dcx", "application/x-dcx"},
	{"der", "application/x-x509-ca-cert"},
	{"dgn", "application/x-dgn"},
	{"dib", "application/x-dib"},
	{"dll", "application/x-msdownload"},
	{"doc", "application/msword"},
	{"dot", "application/msword"},
	{"drw", "application/x-drw"},
	{"dtd", "text/xml"},
	{"dwf", "application/x-dwf"},
	{"dwg", "application/x-dwg"},
	{"dxb", "application/x-dxb"},
	{"dxf", "application/x-dxf"},
	{"edn", "application/vnd.adobe.edn"},
	{"emf", "application/x-emf"},
	{"eml", "message/rfc822"},
	{"ent", "text/xml"},
	{"epi", "application/x-epi"},
	{"eps", "application/x-ps"},
	{"eps", "application/postscript"},
	{"etd", "application/x-ebx"},
	{"exe", "application/x-msdownload"},
	{"fax", "image/fax"},
	{"fdf", "application/vnd.fdf"},
	{"fif", "application/fractals"},
	{"fo", "text/xml"},
	{"frm", "application/x-frm"},
	{"g4", "application/x-g4"},
	{"gbr", "application/x-gbr"},
	{"gif", "image/gif"},
	{"gl2", "application/x-gl2"},
	{"gp4", "application/x-gp4"},
	{"hgl", "application/x-hgl"},
	{"hmr", "application/x-hmr"},
	{"hpg", "application/x-hpgl"},
	{"hpl", "application/x-hpl"},
	{"hqx", "application/mac-binhex40"},
	{"hrf", "application/x-hrf"},
	{"hta", "application/hta"},
	{"htc", "text/x-component"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{"htt", "text/webviewhtml"},
	{"htx", "text/html"},
	{"icb", "application/x-icb"},
	{"ico", "image/x-icon"},
	{"ico", "application/x-ico"},
	{"iff", "application/x-iff"},
	{"ig4", "application/x-g4"},
	{"igs", "application/x-igs"},
	{"iii", "application/x-iphone"},
	{"img", "application/x-img"},
	{"ins", "application/x-internet-signup"},
	{"isp", "application/x-internet-signup"},
	{"IVF", "video/x-ivf"},
	{"java", "java/*"},
	{"jfif", "image/jpeg"},
	{"jpe", "image/jpeg"},
	{"jpe", "application/x-jpe"},
	{"jpeg", "image/jpeg"},
	{"jpg", "image/jpeg"},
	{"jpg", "application/x-jpg"},
	{"js", "application/x-javascript"},
	{"jsp", "text/html"},
	{"la1", "audio/x-liquid-file"},
	{"lar", "application/x-laplayer-reg"},
	{"latex", "application/x-latex"},
	{"lavs", "audio/x-liquid-secure"},
	{"lbm", "application/x-lbm"},
	{"lmsff", "audio/x-la-lms"},
	{"ls", "application/x-javascript"},
	{"ltr", "application/x-ltr"},
	{"m1v", "video/x-mpeg"},
	{"m2v", "video/x-mpeg"},
	{"m3u", "audio/mpegurl"},
	{"m4e", "video/mpeg4"},
	{"mac", "application/x-mac"},
	{"man", "application/x-troff-man"},
	{"math", "text/xml"},
	{"mdb", "application/x-mdb"},
	{"mfp", "application/x-shockwave-flash"},
	{"mht", "message/rfc822"},
	{"mhtml", "message/rfc822"},
	{"mi", "application/x-mi"},
	{"mid", "audio/mid"},
	{"midi", "audio/mid"},
	{"mil", "application/x-mil"},
	{"mml", "text/xml"},
	{"mnd", "audio/x-musicnet-download"},
	{"mns", "audio/x-musicnet-stream"},
	{"mocha", "application/x-javascript"},
	{"movie", "video/x-sgi-movie"},
	{"mp1", "audio/mp1"},
	{"mp2", "audio/mp2"},
	{"mp2v", "video/mpeg"},
	{"mp3", "audio/mp3"},
	{"mp4", "video/mp4"},
	{"mpa", "video/x-mpg"},
	{"mpd", "application/vnd.ms-project"},
	{"mpe", "video/x-mpeg"},
	{"mpeg", "video/mpg"},
	{"mpg", "video/mpg"},
	{"mpga", "audio/rn-mpeg"},
	{"mpp", "application/vnd.ms-project"},
	{"mps", "video/x-mpeg"},
	{"mpt", "application/vnd.ms-project"},
	{"mpv", "video/mpg"},
	{"mpv2", "video/mpeg"},
	{"mpw", "application/vnd.ms-project"},
	{"mpx", "application/vnd.ms-project"},
	{"mtx", "text/xml"},
	{"mxp", "application/x-mmxp"},
	{"net", "image/pnetvue"},
	{"nrf", "application/x-nrf"},
	{"nws", "message/rfc822"},
	{"odc", "text/x-ms-odc"},
	{"out", "application/x-out"},
	{"p10", "application/pkcs10"},
	{"p12", "application/x-pkcs12"},
	{"p7b", "application/x-pkcs7-certificates"},
	{"p7c", "application/pkcs7-mime"},
	{"p7m", "application/pkcs7-mime"},
	{"p7r", "application/x-pkcs7-certreqresp"},
	{"p7s", "application/pkcs7-signature"},
	{"pc5", "application/x-pc5"},
	{"pci", "application/x-pci"},
	{"pcl", "application/x-pcl"},
	{"pcx", "application/x-pcx"},
	{"pdf", "application/pdf"},
	{"pdx", "application/vnd.adobe.pdx"},
	{"pfx", "application/x-pkcs12"},
	{"pgl", "application/x-pgl"},
	{"pic", "application/x-pic"},
	{"pko", "application/vnd.ms-pki.pko"},
	{"pl", "application/x-perl"},
	{"plg", "text/html"},
	{"pls", "audio/scpls"},
	{"plt", "application/x-plt"},
	{"png", "image/png"},
	{"png", "application/x-png"},
	{"pot", "application/vnd.ms-powerpoint"},
	{"ppa", "application/vnd.ms-powerpoint"},
	{"ppm", "application/x-ppm"},
	{"pps", "application/vnd.ms-powerpoint"},
	{"ppt", "application/vnd.ms-powerpoint"},
	{"ppt", "application/x-ppt"},
	{"pr", "application/x-pr"},
	{"prf", "application/pics-rules"},
	{"prn", "application/x-prn"},
	{"prt", "application/x-prt"},
	{"ps", "application/x-ps"},
	{"ps", "application/postscript"},
	{"ptn", "application/x-ptn"},
	{"pwz", "application/vnd.ms-powerpoint"},
	{"r3t", "text/vnd.rn-realtext3d"},
	{"ra", "audio/vnd.rn-realaudio"},
	{"ram", "audio/x-pn-realaudio"},
	{"ras", "application/x-ras"},
	{"rat", "application/rat-file"},
	{"rdf", "text/xml"},
	{"rec", "application/vnd.rn-recording"},
	{"red", "application/x-red"},
	{"rgb", "application/x-rgb"},
	{"rjs", "application/vnd.rn-realsystem-rjs"},
	{"rjt", "application/vnd.rn-realsystem-rjs"},
	{"rlc", "application/x-rlc"},
	{"rle", "application/x-rle"},
	{"rm", "application/vnd.rn-realmedia"},
	{"rmf", "application/vnd.adobe.rmf"},
	{"rmi", "audio/mid"},
	{"rmj", "application/vnd.rn-realsystem-rmj"},
	{"rmm", "audio/x-pn-realaudio"},
	{"rmp", "application/vnd.rn-rn_music_package"},
	{"rms", "application/vnd.rn-realmedia-secure"},
	{"rmvb", "application/vnd.rn-realmedia-vbr"},
	{"rmx", "application/vnd.rn-realsystem-rmx"},
	{"rnx", "application/vnd.rn-realplayer"},
	{"rp", "image/vnd.rn-realpix"},
	{"rpm", "audio/x-pn-realaudio-plugin"},
	{"rsml", "application/vnd.rn-rsml"},
	{"rt", "text/vnd.rn-realtext"},
	{"rtf", "application/msword"},
	{"rtf", "application/x-rtf"},
	{"rv", "video/vnd.rn-realvideo"},
	{"sam", "application/x-sam"},
	{"sat", "application/x-sat"},
	{"sdp", "application/sdp"},
	{"sdw", "application/x-sdw"},
	{"sit", "application/x-stuffit"},
	{"slb", "application/x-slb"},
	{"sld", "application/x-sld"},
	{"slk", "drawing/x-slk"},
	{"smi", "application/smil"},
	{"smil", "application/smil"},
	{"smk", "application/x-smk"},
	{"snd", "audio/basic"},
	{"sol", "text/plain"},
	{"sor", "text/plain"},
	{"spc", "application/x-pkcs7-certificates"},
	{"spl", "application/futuresplash"},
	{"spp", "text/xml"},
	{"ssm", "application/streamingmedia"},
	{"sst", "application/vnd.ms-pki.certstore"},
	{"stl", "application/vnd.ms-pki.stl"},
	{"stm", "text/html"},
	{"sty", "application/x-sty"},
	{"svg", "text/xml"},
	{"swf", "application/x-shockwave-flash"},
	{"tdf", "application/x-tdf"},
	{"tg4", "application/x-tg4"},
	{"tga", "application/x-tga"},
	{"tif", "image/tiff"},
	{"tiff", "image/tiff"},
	{"tld", "text/xml"},
	{"top", "drawing/x-top"},
	{"torrent", "application/x-bittorrent"},
	{"tsd", "text/xml"},
	{"txt", "text/plain"},
	{"uin", "application/x-icq"},
	{"uls", "text/iuls"},
	{"vcf", "text/x-vcard"},
	{"vda", "application/x-vda"},
	{"vdx", "application/vnd.visio"},
	{"vml", "text/xml"},
	{"vpg", "application/x-vpeg005"},
	{"vsd", "application/vnd.visio"},
	{"vsd", "application/x-vsd"},
	{"vss", "application/vnd.visio"},
	{"vst", "application/vnd.visio"},
	{"vst", "application/x-vst"},
	{"vsw", "application/vnd.visio"},
	{"vsx", "application/vnd.visio"},
	{"vtx", "application/vnd.visio"},
	{"vxml", "text/xml"},
	{"wav", "audio/wav"},
	{"wax", "audio/x-ms-wax"},
	{"wb1", "application/x-wb1"},
	{"wb2", "application/x-wb2"},
	{"wb3", "application/x-wb3"},
	{"wbmp", "image/vnd.wap.wbmp"},
	{"wiz", "application/msword"},
	{"wk3", "application/x-wk3"},
	{"wk4", "application/x-wk4"},
	{"wkq", "application/x-wkq"},
	{"wks", "application/x-wks"},
	{"wm", "video/x-ms-wm"},
	{"wma", "audio/x-ms-wma"},
	{"wmd", "application/x-ms-wmd"},
	{"wmf", "application/x-wmf"},
	{"wml", "text/vnd.wap.wml"},
	{"wmv", "video/x-ms-wmv"},
	{"wmx", "video/x-ms-wmx"},
	{"wmz", "application/x-ms-wmz"},
	{"wp6", "application/x-wp6"},
	{"wpd", "application/x-wpd"},
	{"wpg", "application/x-wpg"},
	{"wpl", "application/vnd.ms-wpl"},
	{"wq1", "application/x-wq1"},
	{"wr1", "application/x-wr1"},
	{"wri", "application/x-wri"},
	{"wrk", "application/x-wrk"},
	{"ws", "application/x-ws"},
	{"ws2", "application/x-ws"},
	{"wsc", "text/scriptlet"},
	{"wsdl", "text/xml"},
	{"wvx", "video/x-ms-wvx"},
	{"xdp", "application/vnd.adobe.xdp"},
	{"xdr", "text/xml"},
	{"xfd", "application/vnd.adobe.xfd"},
	{"xfdf", "application/vnd.adobe.xfdf"},
	{"xhtml", "text/html"},
	{"xls", "application/vnd.ms-excel"},
	{"xls", "application/x-xls"},
	{"xlw", "application/x-xlw"},
	{"xml", "text/xml"},
	{"xpl", "audio/scpls"},
	{"xq", "text/xml"},
	{"xql", "text/xml"},
	{"xquery", "text/xml"},
	{"xsd", "text/xml"},
	{"xsl", "text/xml"},
	{"xslt", "text/xml"},
	{"xwd", "application/x-xwd"},
	{"x_b", "application/x-x_b"},
	{"sis", "application/vnd.symbian.install"},
	{"sisx", "application/vnd.symbian.install"},
	{"x_t", "application/x-x_t"},
	{"ipa", "application/vnd.iphone"},
	{"apk", "application/vnd.android.package-archive"},
	{"xap", "application/x-silverlight-app"}
};

std::string GetContentType(const char* path)
{
	const auto len = strlen(path);
	auto i = path + len - 1;
	for (; i >= path; --i)
	{
		if (*i == '.') break;
	}
	const auto ct = ContentTypeTable.find(std::string(i + 1));
	if (ct != ContentTypeTable.end()) return ct->second;
	return "application/octet-stream";
}

std::string GetHttpUrlWithoutGet(const char* http, const uint32_t size)
{
	auto start = 0, end = 0;
	for (auto i = 0; i < size; ++i)
	{
		if (http[i] == ' ')
		{
			start = i + 1;
			break;
		}
	}
	for (auto i = start; i < size; ++i)
	{
		if (http[i] == ' ' || http[i] == '?')
		{
			end = i;
			break;
		}
	}
	return !start && !end ? std::string() : std::string(http + start, end - start);
}

void HttpNotFound(const int fd)
{

	static const auto html =
		"<html><head><title>404 Not Found</title></head>"
		"<body>"
		"<center><h1>404 Not Found</h1></center>"
		"<hr><center>iriszero/" VERSION "</center>"
		"</body></html>";
	static const auto len = strlen(html);
	std::ostringstream oss;
	oss <<
		"HTTP/1.1 404 Not Found\r\n"
		"Content-Length: " << std::to_string(len) << "\r\n"
		"Content-Type: text/html\r\n"
		"Server: iriszero/" VERSION "\r\n"
		"Connection: close\r\n\r\n" <<
		html;
	const auto http = oss.str();
	printf("<========================\n%s\n", http.c_str());
	send(fd, http.c_str(), http.length(), 0);
}

void HttpNotModified(const int fd, const char* lastModified)
{
	std::ostringstream oss;
	oss << "HTTP/1.1 304 Not Modified\r\n"
		"Server: iriszero/" VERSION "\r\n"
		"Last-Modified: " << lastModified << "\r\n"
		"Connection: close\r\n\r\n";
	const auto http = oss.str();
	printf("<========================\n%s\n", http.c_str());
	send(fd, http.c_str(), http.length(), 0);
}

void HttpFile(
	const int fd,
	const char* path,
	const char* lastModified,
	const uint64_t fileSize,
	const uint64_t offset = 0,
	uint64_t size = 0)
{
#define HttpHead(value, http, sm) \
	std::regex_search(http, sm, std::regex(""#value": {0,1}.+?\\r{0,1}\\n", std::regex::icase)); \
	const auto (value) = std::regex_replace((sm)[0].str(), std::regex("("#value": {0,1}|\\r{0,1}\\n)", std::regex::icase), "")

#define IfErrorThenReturn(fun, fp) if((fun) < 0) { fclose(fp); return; }
	char buf[4096] = {0};
	size_t len = 0;
	const auto fp = fopen(path, "rb");
	if (!offset && !size)
	{
		std::ostringstream head;
		head << "HTTP/1.1 200 OK\r\nContent-Length:" <<
			std::to_string(fileSize) <<
			"\r\nConnection: close"
			"\r\nLast-Modified: " << lastModified <<
			"\r\nContent-Type: " << GetContentType(path) <<
			"\r\nServer: iriszero/" VERSION
			"\r\n\r\n";
		printf("<========================\n%s\n", head.str().c_str());
		IfErrorThenReturn(send(fd, head.str().c_str(), head.str().length(), 0), fp);
		while ((len = fread(buf, sizeof(uint8_t), 4096, fp)) == 4096)
			IfErrorThenReturn(send(fd, buf, 4096, 0), fp);
		IfErrorThenReturn(send(fd, buf, len, 0), fp);
	}
	else
	{
		fseek(fp, offset, SEEK_SET);
		std::ostringstream head;
		head << "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\n" <<
			"Server: iriszero/" VERSION "\r\n" <<
			"Content-Type: " << GetContentType(path) << "\r\n"
			"Content-Length: " << std::to_string(size)
			<< "\r\nContent-Range: bytes " <<
			std::to_string(offset) << "-" <<
			std::to_string(offset + size - 1) << "/" <<
			std::to_string(fileSize) << "\r\nConnection: close\r\n\r\n";
		printf("<========================\n%s\n", head.str().c_str());
		IfErrorThenReturn(send(fd, head.str().c_str(), head.str().length(), 0), fp);
		for (; size > 4096; size -= len)
		{
			len = fread(buf, sizeof(uint8_t), 4096, fp);
			IfErrorThenReturn(send(fd, buf, len, 0), fp);
		}
		len = fread(buf, sizeof(uint8_t), size, fp);
		IfErrorThenReturn(send(fd, buf, len, 0), fp);
	}
}

void IndexOf(const int fd, const char* path, const char* coding)
{
	std::ostringstream dirs;
	std::ostringstream files;
	GetFiles(path, dirs, files);
	std::ostringstream html;
	html << " <!DOCTYPE html>"
		"<html>" <<
		"<head><title>Index of " << path << "</title>"
		"<meta charset=\"" << coding << "\"/>"
		"</head>"
		"<body>"
		"<h1>Index of " << path << "</h1><hr>" <<
		dirs.str() << (dirs.str().empty() ? "" : "<hr>") <<
		"<table>" <<
		(files.str().empty() ? "" : "<tr><th>File Name</th><th>Size</th></tr>") <<
		files.str() <<
		"</table></body>"
		"</html>";
	std::ostringstream head;
	head << "HTTP/1.1 200 OK\r\nContent-length: " << std::to_string(html.str().length()) <<
		"\r\nServer: iriszero/" VERSION <<
		"\r\nContent-Type: text/html\r\n\r\n";
	printf("<========================\n%s\n", head.str().c_str());
	send(fd, head.str().c_str(), head.str().length(), 0);
	send(fd, html.str().c_str(), html.str().length(), 0);
}

bool CheckUrl(const std::string& url, const char* path)
{
	std::regex_replace(url, std::regex("\\.\\."), "");
	return !memcmp(url.c_str(), path, strlen(path) - 1);
}

std::list<std::tuple<uint64_t, uint64_t>> GetOffsetAndSize(
	const std::string& __range,
	const uint64_t fileSize)
{
	std::list<std::tuple<uint64_t, uint64_t>> res{};
	auto _range = __range.substr(6, __range.length() - 6);
	auto pos = 0;
	for (auto i = 0; i < _range.length(); ++i)
	{
		if (_range[i] == ',')
		{
			auto range = _range.substr(pos, i - pos);
			const int _pos = range.find('-');
			auto _start = range.substr(0, _pos);
			auto _end = range.substr(_pos + 1, range.length() - _pos - 1);
			if (_start.empty())
			{
				auto end = std::stoull(_end);
				res.emplace_back(fileSize - end, end);
			}
			else if (_end.empty())
			{
				auto start = std::stoull(_start);
				res.emplace_back(start, fileSize - start);
			}
			else
			{
				auto start = std::stoull(_start);
				res.emplace_back(start, std::stoull(_end) - start + 1);
			}
			pos = i + 1;
		}
	}
	const auto range = _range.substr(pos, _range.length() - pos);
	const int _pos = range.find('-');
	const auto _start = range.substr(0, _pos);
	const auto _end = range.substr(_pos + 1, range.length() - _pos - 1);
	if (_start.empty())
	{
		auto end = std::stoull(_end);
		res.emplace_back(fileSize - end, end);
	}
	else if (_end.empty())
	{
		auto start = std::stoull(_start);
		res.emplace_back(start, fileSize - start);
	}
	else
	{
		auto start = std::stoull(_start);
		res.emplace_back(start, std::stoull(_end) - start + 1);
	}
	return res;
}

void Index(const char* path, const int port, const int threadNum, const char* coding, const char* icoPath)
{
	UrlEncodeTable['/'] = '/';
	sockaddr_in svrAddr{}, cliAddr{};
	svrAddr.sin_family = AF_INET;
	svrAddr.sin_addr.s_addr = INADDR_ANY;
	svrAddr.sin_port = htons(port);
	char one[4] = {0};
	socklen_t sinLen = sizeof(cliAddr);
#ifdef _MSC_VER
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) < 0)
	err(EXIT_FAILURE, "WinSock init fail");
	auto sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
	err(EXIT_FAILURE, "Can't open socket");
#else
	struct sigaction action;
	action.sa_handler = [](int) {};
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(SIGPIPE, &action, nullptr);
	auto sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock <= 0) err(EXIT_FAILURE, "Can't open socket");
#endif
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, one, sizeof(one));
	if (bind(sock, (struct sockaddr *)&svrAddr, sizeof(svrAddr)) < 0)
	{
		close(sock);
		err(1, "Can't bind");
	}
	listen(sock, threadNum);
	std::valarray<std::thread> pool(threadNum);
	std::generate(begin(pool), end(pool), [&]()
	{
		return std::thread([&]()
		{
			const auto iconPath = PathCombine(path, "favicon.ico");
			while (true)
			{
				const auto clientFd = accept(sock, (struct sockaddr *)&cliAddr, &sinLen);
				char buf[4096] = {0};
				auto len = 0;
				std::ostringstream _http;
				while ((len = recv(clientFd, buf, 4096, 0)) == 4096)
					_http << buf;
				_http.write(buf, len);
				auto http = _http.str();
				printf(
					"%s:%d===================>\n%s\n",
					inet_ntoa(cliAddr.sin_addr),
					ntohs(cliAddr.sin_port),
					http.c_str());
				std::smatch sm;
				auto _url = GetHttpUrlWithoutGet(http.c_str(), http.length());
#ifdef _MSC_VER
				auto url = ToWindowsPath(
					UrlDecode(_url.c_str(), _url.length()).c_str());
#else
						auto url = UrlDecode(_url.c_str(), _url.length());
#endif
				auto urlStatus = false;
				if (_url.empty()) continue;
				if (_url == "/") goto index;
				if (_url == "/favicon.ico" && !FileExists(iconPath.c_str()))
				{
					if (!icoPath[0]) HttpNotFound(clientFd);
					else HttpFile(
						clientFd, 
						icoPath,
						FileLastModified(iconPath.c_str()).c_str(),
						FileSize(iconPath.c_str()));
					close(clientFd);
					continue;
				}
				urlStatus = CheckUrl(url, path);
				if (urlStatus && DirectoryExists(url.c_str()))
				{
					IndexOf(clientFd, url.c_str(), coding);
				}
				else if (urlStatus && FileExists(url.c_str()))
				{
					HttpHead(Range, http, sm);
					if (Range.empty())
					{
						std::regex_search(
							http, 
							sm, 
							std::regex("If-Modified-Since: {0,1}.+?\\r{0,1}\\n", std::regex::icase));
						auto fileLastModified = FileLastModified(url.c_str());
						auto lastModified = std::regex_replace(
							sm[0].str(), 
							std::regex("(If-Modified-Since: {0,1}|\\r{0,1}\\n)", std::regex::icase),
							"");
						if(lastModified == fileLastModified)
						{
							HttpNotModified(clientFd, fileLastModified.c_str());
						}
						else
						{
							HttpFile(
								clientFd,
								url.c_str(),
								FileLastModified(url.c_str()).c_str(),
								FileSize(url.c_str()));
						}
					}
					else
					{
						for (auto& i : GetOffsetAndSize(Range, FileSize(url.c_str())))
						{
							HttpFile(
								clientFd,
								url.c_str(),
								nullptr,
								FileSize(url.c_str()),
								std::get<0>(i),
								std::get<1>(i));
						}
					}
				}
				else
				{
				index:;
					IndexOf(clientFd, path, coding);
				}
				close(clientFd);
			}
		});
	});
	for (auto& t : pool) t.join();
}

int main(const int argc, char* argv[])
{
	if (argc == 5)
	{
		Index(
			argv[1],
			strtol(argv[2], &argv[2], 10),
			strtol(argv[3], &argv[3], 10),
			argv[4],
			"");
	}
	if (argc == 6)
	{
		Index(
			argv[1],
			strtol(argv[2], &argv[2], 10),
			strtol(argv[3], &argv[3], 10),
			argv[4],
			argv[5]);
	}
	err(EXIT_FAILURE, "%s IndexPath Port threadNum Coding [IcoPath]\n", argv[0]);
}
