#pragma once
#include "sync.hpp"

namespace syncer {
inline void mergeJson(Json& target,const Json& source) {
	if(!source.is_object() || !target.is_object()) { target=source; return; }
	for(auto it=source.begin();it!=source.end();++it) {
		auto found=target.find(it.key());
		if(found==target.end()) target[it.key()]=it.value();
		else mergeJson(*found,it.value());
	}
}
inline void mergeToml(toml::table& target,const toml::table& source) {
	for(const auto& [key,value]:source) {
		auto existing=target.get(key.str());
		if(existing && existing->is_table() && value.is_table()) mergeToml(*existing->as_table(),*value.as_table());
		else target.insert_or_assign(key,value);
	}
}
inline std::string mergeConfiguration(const fs::path& source,const fs::path& target) {
	auto extension=source.extension().wstring();
	std::transform(extension.begin(),extension.end(),extension.begin(),towlower);
	auto src=read(source), dst=read(target);
	if(extension==L".json") {
		auto from=Json::parse(src), to=Json::parse(dst);
		auto previous=to;
		mergeJson(to,from);
		return to==previous?dst:to.dump(2)+"\n";
	}
	auto from=toml::parse(src), to=toml::parse(dst);
	auto previous=to;
	mergeToml(to,from);
	if(to==previous) return dst;
	std::ostringstream out;
	out<<toml::toml_formatter{to}<<'\n';
	return out.str();
}
}