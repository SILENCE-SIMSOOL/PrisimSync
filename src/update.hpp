#pragma once
#include <wininet.h>
#include <charconv>
#include "sync.hpp"
#pragma comment(lib,"wininet.lib")
namespace updater {
using syncer::Json;
namespace fs=std::filesystem;
inline std::array<unsigned,3> version(std::string text) {
	if(!text.empty() && (text.front()=='v' || text.front()=='V')) text.erase(0,1);
	std::array<unsigned,3> parts{};
	for(int i=0;i<3;i++) {
		auto end=text.find('.');
		auto token=text.substr(0,end);
		auto result=std::from_chars(token.data(),token.data()+token.size(),parts[i]);
		if(token.empty() || result.ec!=std::errc{} || result.ptr!=token.data()+token.size() ||
			(i<2 && end==std::string::npos) || (i==2 && end!=std::string::npos))
			throw std::runtime_error("Unsupported release version");
		text.erase(0,end==std::string::npos?text.size():end+1);
	}
	return parts;
}
struct Release { std::string tag,url,digest; uint64_t size=0; };
inline Release select(const Json& metadata,const std::string& current) {
	if(metadata.value("draft",false) || metadata.value("prerelease",false)) return {};
	auto tag=metadata.at("tag_name").get<std::string>();
	if(version(tag)<=version(current)) return {};
	auto number=tag;
	if(number.starts_with('v') || number.starts_with('V')) number.erase(0,1);
	auto expected="PrismSync-Setup-"+number+".exe";
	Release release;
	for(const auto& asset:metadata.at("assets")) {
		if(asset.at("name")!=expected) continue;
		if(!release.url.empty()) throw std::runtime_error("Ambiguous installer asset");
		release={tag,asset.at("browser_download_url").get<std::string>(),asset.at("digest").get<std::string>(),asset.at("size").get<uint64_t>()};
		if(!release.url.starts_with("https://github.com/SILENCE-SIMSOOL/PrismSync/releases/download/"))
			throw std::runtime_error("Installer URL is outside the release repository");
		if(!release.digest.starts_with("sha256:") || release.digest.size()!=71 ||
			!std::all_of(release.digest.begin()+7,release.digest.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}) ||
			!release.size || release.size>1024ull*1024*1024)
			throw std::runtime_error("Installer has no valid size or SHA-256 digest");
	}
	if(release.url.empty()) throw std::runtime_error("Expected versioned installer not found");
	return release;
}
struct Internet {
	HINTERNET handle=nullptr;
	~Internet() { if(handle) InternetCloseHandle(handle); }
};
inline void fetch(const std::wstring& url,std::ostream& out,uint64_t limit,const std::atomic<bool>& cancel,
	const std::function<void(uint64_t)>& progress={}) {
	if(!url.starts_with(L"https://")) throw std::runtime_error("HTTPS required");
	Internet session{InternetOpenW(L"PrismSync",INTERNET_OPEN_TYPE_PRECONFIG,nullptr,nullptr,0)};
	syncer::winCheck(session.handle!=nullptr,"Open network");
	DWORD timeout=5000;
	for(auto option:{INTERNET_OPTION_CONNECT_TIMEOUT,INTERNET_OPTION_RECEIVE_TIMEOUT,INTERNET_OPTION_SEND_TIMEOUT})
		InternetSetOptionW(session.handle,option,&timeout,sizeof(timeout));
	syncer::check(cancel);
	Internet response{InternetOpenUrlW(session.handle,url.c_str(),L"Accept: application/vnd.github+json\r\n",(DWORD)-1,
		INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_SECURE|INTERNET_FLAG_NO_UI,0)};
	syncer::winCheck(response.handle!=nullptr,"Open HTTPS request");
	DWORD status=0,length=sizeof(status);
	syncer::winCheck(HttpQueryInfoW(response.handle,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&status,&length,nullptr)!=0,"Read HTTP status");
	if(status!=200) throw std::runtime_error("HTTP status "+std::to_string(status));
	std::wstring finalUrl(16384,L'\0');
	length=(DWORD)(finalUrl.size()*sizeof(wchar_t));
	syncer::winCheck(InternetQueryOptionW(response.handle,INTERNET_OPTION_URL,finalUrl.data(),&length)!=0,"Read final URL");
	if(!finalUrl.starts_with(L"https://")) throw std::runtime_error("Insecure download redirect");
	std::array<char,65536> buffer;
	uint64_t total=0;
	for(;;) {
		syncer::check(cancel);
		DWORD count=0;
		syncer::winCheck(InternetReadFile(response.handle,buffer.data(),(DWORD)buffer.size(),&count)!=0,"Download");
		if(!count) break;
		total+=count;
		if(total>limit) throw std::runtime_error("Download exceeds expected size");
		out.write(buffer.data(),count);
		if(!out) throw std::runtime_error("Cannot save download");
		if(progress) progress(total);
	}
}
inline Release latest(const std::string& current,const std::atomic<bool>& cancel) {
	std::ostringstream body;
	fetch(L"https://api.github.com/repos/SILENCE-SIMSOOL/PrismSync/releases/latest",body,4*1024*1024,cancel);
	return select(Json::parse(body.str()),current);
}
inline void verify(const fs::path& path,const Release& release,const std::atomic<bool>& cancel) {
	if(fs::file_size(path)!=release.size || "sha256:"+syncer::digest(path,cancel)!=release.digest)
		throw std::runtime_error("Installer size or SHA-256 mismatch; installation blocked");
}
inline fs::path download(const Release& release,const fs::path& directory,const std::atomic<bool>& cancel,
	const std::function<void(uint64_t)>& progress={}) {
	auto folder=syncer::uniqueDirectory(directory);
	syncer::TempFile partial{folder/L"installer.part"};
	std::ofstream out(partial.path,std::ios::binary);
	std::wstring url(release.url.begin(),release.url.end());
	fetch(url,out,release.size,cancel,progress);
	out.close();
	if(!out) throw std::runtime_error("Installer write failed");
	syncer::flush(partial.path);
	verify(partial.path,release,cancel);
	auto final=folder/L"PrismSync-Setup.exe";
	syncer::replace(partial.path,final,false);
	return final;
}
}