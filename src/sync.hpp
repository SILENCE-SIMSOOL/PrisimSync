#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <atomic>
#include <array>
#include <set>
#include <algorithm>
#include "vendor/json.hpp"
#include "vendor/toml.hpp"
#pragma comment(lib,"bcrypt.lib")

namespace syncer {
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;
using Progress = std::function<void(const fs::path&,uint64_t,uint64_t,size_t,size_t)>;
struct Cancelled : std::runtime_error { Cancelled():std::runtime_error("Cancelled") {} };
inline void check(const std::atomic<bool>& cancel) { if(cancel) throw Cancelled(); }
inline void winCheck(bool ok,const char* operation) {
	if(!ok) throw std::system_error(GetLastError(),std::system_category(),operation);
}
inline std::string pathText(const fs::path& path) {
	auto text=path.u8string();
	return {text.begin(),text.end()};
}
inline void validatePath(const fs::path& path) {
	auto absolute=fs::absolute(path).lexically_normal();
	for(auto p=absolute;!p.empty();p=p.parent_path()) {
		DWORD attributes=GetFileAttributesW(p.c_str());
		if(attributes==INVALID_FILE_ATTRIBUTES) {
			auto error=GetLastError();
			if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)
				throw std::system_error(error,std::system_category(),"Inspect path");
		} else if(attributes&FILE_ATTRIBUTE_REPARSE_POINT) {
			throw std::runtime_error("Symbolic links and junctions are not supported: "+pathText(p));
		}
		if(p==p.root_path()) break;
	}
}
inline bool within(const fs::path& child,const fs::path& parent) {
	auto c=fs::absolute(child).lexically_normal().wstring();
	auto p=fs::absolute(parent).lexically_normal().wstring();
	if(c.size()<p.size() || _wcsnicmp(c.c_str(),p.c_str(),p.size())) return false;
	return c.size()==p.size() || p.back()==L'\\' || c[p.size()]==L'\\';
}
inline fs::path under(const fs::path& root,const fs::path& relative) {
	if(relative.empty() || relative.is_absolute()) throw std::runtime_error("Invalid relative path");
	for(const auto& part:relative)
		if(part==L".." || part==L"." || part.wstring().find(L':')!=std::wstring::npos)
			throw std::runtime_error("Unsafe relative path");
	auto result=root/relative;
	if(!within(result,root)) throw std::runtime_error("Path escapes instance");
	validatePath(result);
	return result;
}
struct Hash {
	BCRYPT_ALG_HANDLE algorithm=nullptr;
	BCRYPT_HASH_HANDLE handle=nullptr;
	Hash() {
		if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
			throw std::runtime_error("SHA-256 unavailable");
		if(BCryptCreateHash(algorithm,&handle,nullptr,0,nullptr,0,0)<0) {
			BCryptCloseAlgorithmProvider(algorithm,0);
			throw std::runtime_error("SHA-256 initialization failed");
		}
	}
	~Hash() { if(handle) BCryptDestroyHash(handle); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
	void add(const char* data,size_t size) {
		if(BCryptHashData(handle,(PUCHAR)data,(ULONG)size,0)<0) throw std::runtime_error("SHA-256 failed");
	}
	std::string finish() {
		unsigned char bytes[32];
		if(BCryptFinishHash(handle,bytes,sizeof(bytes),0)<0) throw std::runtime_error("SHA-256 failed");
		std::string value;
		for(auto b:bytes) { value+="0123456789abcdef"[b>>4]; value+="0123456789abcdef"[b&15]; }
		return value;
	}
};
inline std::string digest(const fs::path& file,const std::atomic<bool>& cancel) {
	validatePath(file);
	std::ifstream input(file,std::ios::binary);
	if(!input) throw std::runtime_error("Cannot read: "+pathText(file));
	Hash hash;
	std::array<char,65536> buffer;
	while(input) {
		check(cancel);
		input.read(buffer.data(),buffer.size());
		hash.add(buffer.data(),(size_t)input.gcount());
	}
	if(!input.eof()) throw std::runtime_error("Read failed: "+pathText(file));
	return hash.finish();
}
inline std::string read(const fs::path& file) {
	validatePath(file);
	std::ifstream input(file,std::ios::binary);
	if(!input) throw std::runtime_error("Cannot read: "+pathText(file));
	std::ostringstream result;
	result<<input.rdbuf();
	if(input.bad()) throw std::runtime_error("Read failed: "+pathText(file));
	return result.str();
}
inline void write(const fs::path& file,const std::string& data) {
	validatePath(file);
	std::ofstream output(file,std::ios::binary|std::ios::trunc);
	output.write(data.data(),data.size());
	output.close();
	if(!output) throw std::runtime_error("Write failed: "+pathText(file));
}
inline void flush(const fs::path& file) {
	HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
	winCheck(handle!=INVALID_HANDLE_VALUE,"Open for flush");
	bool ok=FlushFileBuffers(handle)!=0;
	auto error=GetLastError();
	CloseHandle(handle);
	if(!ok) throw std::system_error(error,std::system_category(),"Flush file");
}
inline void copy(const fs::path& source,const fs::path& target,const std::atomic<bool>& cancel,
	const std::function<void(uint64_t)>& progress={}) {
	validatePath(source); validatePath(target);
	std::ifstream input(source,std::ios::binary);
	std::ofstream output(target,std::ios::binary|std::ios::trunc);
	if(!input || !output) throw std::runtime_error("Cannot open copy files");
	std::array<char,65536> buffer;
	while(input) {
		check(cancel);
		input.read(buffer.data(),buffer.size());
		auto count=input.gcount();
		output.write(buffer.data(),count);
		if(!output) throw std::runtime_error("Copy write failed");
		if(progress) progress((uint64_t)count);
	}
	if(!input.eof()) throw std::runtime_error("Copy read failed");
	output.close();
	if(!output) throw std::runtime_error("Copy flush failed");
	flush(target);
}
inline fs::path uniqueDirectory(const fs::path& parent) {
	validatePath(parent);
	fs::create_directories(parent);
	for(unsigned n=0;n<1000;n++) {
		auto path=parent/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(n));
		if(fs::create_directory(path)) return path;
	}
	throw std::runtime_error("Cannot create working directory");
}
inline fs::path temporary(const fs::path& target) {
	for(unsigned n=0;n<1000;n++) {
		auto path=target.parent_path()/(L".prismsync-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(n)+L".tmp");
		HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
		if(file!=INVALID_HANDLE_VALUE) { CloseHandle(file); return path; }
		if(GetLastError()!=ERROR_FILE_EXISTS) winCheck(false,"Create temporary file");
	}
	throw std::runtime_error("Cannot create temporary file");
}
struct TempFile {
	fs::path path;
	~TempFile() { std::error_code ec; fs::remove(path,ec); }
};
inline void replace(const fs::path& temp,const fs::path& target,bool existed) {
	validatePath(temp); validatePath(target);
	if(existed) winCheck(ReplaceFileW(target.c_str(),temp.c_str(),nullptr,0,nullptr,nullptr)!=0,"Replace file");
	else winCheck(MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)!=0,"Install file");
}

struct Entry {
	fs::path relative;
	std::string contents;
	std::string sourceHash,previousHash,resultHash,action;
	uint64_t bytes=0;
	bool existed=false;
};
struct Plan {
	fs::path source,target;
	std::vector<Entry> files;
	std::vector<std::string> errors;
	size_t unchanged=0;
	uint64_t bytes=0;
};
using Merge = std::function<std::string(const fs::path&,const fs::path&)>;
inline Plan prepare(const fs::path& source,const fs::path& target,
	const std::array<bool,7>& categories,const std::atomic<bool>& cancel,const Merge& merge,const Progress& progress={}) {
	Plan plan;
	plan.source=fs::absolute(source).lexically_normal();
	plan.target=fs::absolute(target).lexically_normal();
	validatePath(plan.source); validatePath(plan.target);
	if(!fs::is_directory(source) || !fs::is_directory(target)) throw std::runtime_error("Instance game directory not found");
	if(within(source,target) || within(target,source)) throw std::runtime_error("Source and target overlap");
	std::set<fs::path> seen;
	auto add=[&](const fs::path& relative,bool configuration) {
		check(cancel);
		if(!seen.insert(relative).second) return;
		try {
			auto src=under(plan.source,relative), dst=under(plan.target,relative);
			if(!fs::is_regular_file(src)) throw std::runtime_error("Source is not a regular file");
			if(progress) progress(relative,0,0,plan.files.size()+plan.unchanged,0);
			Entry item;
			item.relative=relative; item.existed=fs::exists(dst);
			if(item.existed && !fs::is_regular_file(dst)) throw std::runtime_error("Target is not a regular file");
			item.sourceHash=digest(src,cancel);
			if(item.existed) item.previousHash=digest(dst,cancel);
			if(item.existed && item.sourceHash==item.previousHash) { plan.unchanged++; return; }
			item.action=item.existed?"REPLACE":"CREATE";
			auto ext=relative.extension().wstring();
			std::transform(ext.begin(),ext.end(),ext.begin(),towlower);
			if(configuration && item.existed && (ext==L".json" || ext==L".toml")) {
				item.action="MERGE";
				item.contents=merge(src,dst);
				if(digest(src,cancel)!=item.sourceHash || digest(dst,cancel)!=item.previousHash)
					throw std::runtime_error("File changed during preview; retry with Minecraft closed");
				Hash hash;
				hash.add(item.contents.data(),item.contents.size());
				item.resultHash=hash.finish();
			} else item.resultHash=item.sourceHash;
			if(item.existed && item.resultHash==item.previousHash) { plan.unchanged++; return; }
			item.bytes=item.action=="MERGE"?item.contents.size():fs::file_size(src);
			plan.bytes+=item.bytes;
			plan.files.push_back(std::move(item));
		} catch(const Cancelled&) { throw; }
		catch(const std::exception& e) { plan.errors.push_back(pathText(relative)+": "+e.what()); }
	};
	auto visit=[&](const fs::path& relative,bool configuration) {
		try {
			auto path=under(source,relative);
			if(!fs::exists(path)) return;
			if(!fs::is_directory(path)) { add(relative,configuration); return; }
			for(auto it=fs::recursive_directory_iterator(path);it!=fs::recursive_directory_iterator();++it) {
				check(cancel);
				auto child=it->path();
				try { validatePath(child); }
				catch(const std::exception& e) { it.disable_recursion_pending(); plan.errors.push_back(pathText(child)+": "+e.what()); continue; }
				if(!it->is_directory()) add(child.lexically_relative(source),configuration);
			}
		} catch(const Cancelled&) { throw; }
		catch(const std::exception& e) { plan.errors.push_back(pathText(relative)+": "+e.what()); }
	};
	if(categories[0]) for(auto name:{L"options.txt",L"command_history.txt",L"servers.dat"}) visit(name,true);
	if(categories[1] && !categories[2]) {
		auto config=under(source,L"config");
		if(fs::is_directory(config)) for(auto& entry:fs::directory_iterator(config))
			if(entry.path().filename().wstring().starts_with(L"sodium")) visit(fs::path(L"config")/entry.path().filename(),true);
	}
	if(categories[2]) visit(L"config",true);
	if(categories[3]) { visit(L"resourcepacks",false); visit(L"texturepacks",false); }
	if(categories[4]) visit(L"shaderpacks",false);
	if(categories[5]) visit(L"saves",false);
	if(categories[6]) visit(L"screenshots",false);
	return plan;
}
struct Result {
	size_t completed=0;
	bool cancelled=false;
	std::vector<std::string> errors;
};
inline Result execute(const Plan& plan,const std::atomic<bool>& cancel,const Progress& progress={}) {
	Result result;
	uint64_t done=0;
	for(const auto& item:plan.files) {
		try {
			check(cancel);
			auto src=under(plan.source,item.relative), dst=under(plan.target,item.relative);
			if(fs::exists(dst)!=item.existed || (item.existed && digest(dst,cancel)!=item.previousHash))
				throw std::runtime_error("Target changed after preview; not overwritten");
			if(digest(src,cancel)!=item.sourceHash) throw std::runtime_error("Source changed after preview; not copied");
			fs::create_directories(dst.parent_path());
			TempFile temp{temporary(dst)};
			uint64_t fileDone=0;
			auto updateProgress=[&](uint64_t count) {
				fileDone+=count;
				if(progress) progress(item.relative,done+fileDone,plan.bytes,result.completed,plan.files.size());
			};
			if(item.action=="MERGE") {
				write(temp.path,item.contents); flush(temp.path); updateProgress(item.bytes);
			} else copy(src,temp.path,cancel,updateProgress);
			if(digest(temp.path,cancel)!=item.resultHash) throw std::runtime_error("Copy checksum mismatch");

			check(cancel);
			under(plan.target,item.relative);
			if(fs::exists(dst)!=item.existed || (item.existed && digest(dst,cancel)!=item.previousHash))
				throw std::runtime_error("Target changed before replacement; not overwritten");
			replace(temp.path,dst,item.existed);
			result.completed++;
			done+=item.bytes;
			if(progress) progress(item.relative,done,plan.bytes,result.completed,plan.files.size());
		} catch(const Cancelled&) { result.cancelled=true; break; }
		catch(const std::exception& e) { result.errors.push_back(pathText(item.relative)+": "+e.what()); }
	}
	return result;
}
}