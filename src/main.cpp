#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <urlmon.h>
#include <wininet.h>
#include <thread>
#include <atomic>
#include <map>
#include <regex>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <array>
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"shlwapi.lib")
#pragma comment(lib,"urlmon.lib")
#pragma comment(lib,"wininet.lib")
using namespace Gdiplus;
namespace fs = std::filesystem;

struct Instance { std::wstring id, name, version, icon; };
struct Click { Rect r; int kind, index; };

static const std::wstring APP_VERSION=L"1.0.0";
static const std::wstring GITHUB_REPO_URL=L"https://github.com/SILENCE-SIMSOOL/PrisimSync";
static const std::wstring GITHUB_RELEASES_URL=L"https://github.com/SILENCE-SIMSOOL/PrisimSync/releases";

static HWND windowHandle;
static fs::path settingsDir, prismDir, sharedFontDir;
static std::vector<Instance> instances;
static std::vector<Click> clicks;
static std::array<bool,7> enabled{true,true,false,true,true,true,false};
static std::wstring mainId, language=L"en", selectedId, status;
static bool optionsPage=false, choosingMain=false, langDropdownOpen=false;
static int scrollX=0, contentWidth=0;
static ULONG_PTR gdiplusToken;

static std::unique_ptr<PrivateFontCollection> privateFonts, medColl, semiColl, boldColl, iconFonts;
static std::unique_ptr<FontFamily> regularFamily, mediumFamily, semiboldFamily, boldFamily, iconFamily;
static fs::path assetDir;
static std::map<std::wstring,std::unique_ptr<Bitmap>> iconCache;

static std::array<float,7> togglePosition{};
static float scrollPosition=0, tabPosition=0, choosingMainAnim=0, langDropdownAnim=0;
static float navHoverLeft=0, navHoverRight=0, btnApplyHover=0, btnChangeHover=0, btnUpdateHover=0;
static std::array<float,32> cardHover{};
static int hoveredKind=-1, hoveredIndex=-1;
static bool draggingScroll=false, animations=true;
static int dragOffset=0;
static ULONGLONG lastFrame=0;

enum class FontWeight { Regular, Medium, SemiBold, Bold };

static constexpr int listTop=284, listHeight=134, trackY=422;
static constexpr int cardWidth=108, cardHeight=130, cardGap=12;

static std::wstring getEnv(const wchar_t* key) {
	DWORD n=GetEnvironmentVariableW(key,nullptr,0);
	if (!n) return L"";
	std::wstring s(n,L'\0');
	GetEnvironmentVariableW(key,s.data(),n);
	s.resize(n-1);
	return s;
}

static std::wstring trim(std::wstring s) {
	auto a=s.find_first_not_of(L" \t\r\n\"");
	if (a==std::wstring::npos) return L"";
	auto b=s.find_last_not_of(L" \t\r\n\"");
	return s.substr(a,b-a+1);
}

static std::wstring iniRead(const fs::path& p,const wchar_t* sec,const wchar_t* key,const wchar_t* def=L"") {
	wchar_t buf[32768];
	GetPrivateProfileStringW(sec,key,def,buf,32768,p.c_str());
	return buf;
}

static std::wstring utf8ToWide(const std::string& bytes) {
	if(bytes.empty()) return {};
	int n=MultiByteToWideChar(CP_UTF8,0,bytes.data(),(int)bytes.size(),nullptr,0);
	if(n<=0) return {};
	std::wstring result(n,L'\0');
	MultiByteToWideChar(CP_UTF8,0,bytes.data(),(int)bytes.size(),result.data(),n);
	return result;
}

static std::wstring cfgRead(const fs::path& path,const std::wstring& key,const std::wstring& fallback=L"") {
	std::ifstream stream(path,std::ios::binary);
	std::string line;
	while(std::getline(stream,line)) {
		auto pos=line.find('=');
		if(pos==std::string::npos) continue;
		auto name=trim(utf8ToWide(line.substr(0,pos)));
		if(name==key) return trim(utf8ToWide(line.substr(pos+1)));
	}
	return fallback;
}

static void iniWrite(const wchar_t* key,const std::wstring& val) {
	WritePrivateProfileStringW(L"PrismSync",key,val.c_str(),(settingsDir/L"settings.ini").c_str());
}

static std::wstring tr(const wchar_t* en,const wchar_t* kr,const wchar_t* zh=L"",const wchar_t* ja=L"") {
	if(language==L"ko") return kr;
	if(language==L"zh" && *zh) return zh;
	if(language==L"ja" && *ja) return ja;
	return en;
}

static void persist() {
	iniWrite(L"prism_path",prismDir.wstring());
	iniWrite(L"main_id",mainId);
	iniWrite(L"language",language);
	for(int i=0;i<7;i++) iniWrite((L"copy_"+std::to_wstring(i)).c_str(),enabled[i]?L"1":L"0");
}

static void load() {
	settingsDir=fs::path(getEnv(L"APPDATA"))/L"PrismSync";
	sharedFontDir=fs::path(getEnv(L"APPDATA"))/L"Lucent"/L"resources"/L"fonts";
	std::error_code ec;
	fs::create_directories(settingsDir,ec);
	auto p=settingsDir/L"settings.ini";
	prismDir=iniRead(p,L"PrismSync",L"prism_path",(fs::path(getEnv(L"APPDATA"))/L"PrismLauncher").c_str());
	mainId=iniRead(p,L"PrismSync",L"main_id");
	WORD lang=PRIMARYLANGID(GetUserDefaultUILanguage());
	std::wstring systemLang=lang==LANG_KOREAN?L"ko":lang==LANG_CHINESE?L"zh":lang==LANG_JAPANESE?L"ja":L"en";
	language=iniRead(p,L"PrismSync",L"language",systemLang.c_str());
	if (language!=L"ko" && language!=L"en" && language!=L"zh" && language!=L"ja") language=L"en";
	for(int i=0;i<7;i++) enabled[i]=iniRead(p,L"PrismSync",(L"copy_"+std::to_wstring(i)).c_str(),enabled[i]?L"1":L"0")==L"1";
}

static bool safeId(const std::wstring& id) {
	return !id.empty() && id!=L"." && id!=L".." && id.find_first_of(L"/\\:")==std::wstring::npos;
}

static void refresh() {
	instances.clear();
	iconCache.clear();
	std::error_code ec;
	auto base=prismDir/L"instances";
	if(fs::exists(base,ec)) {
		for(auto& entry:fs::directory_iterator(base,fs::directory_options::skip_permission_denied,ec)) {
			if(!entry.is_directory(ec)) continue;
			Instance in;
			in.id=entry.path().filename().wstring();
			if(!safeId(in.id)) continue;
			auto cfg=entry.path()/L"instance.cfg";
			if(!fs::exists(cfg,ec)) continue;
			in.name=cfgRead(cfg,L"name",in.id);
			in.icon=cfgRead(cfg,L"iconKey");
			if(in.icon.empty()) in.icon=cfgRead(cfg,L"IconKey");
			in.version=cfgRead(cfg,L"IntendedVersion");
			if(in.version.empty()) {
				std::ifstream pack(entry.path()/L"mmc-pack.json");
				std::string json((std::istreambuf_iterator<char>(pack)),{});
				std::smatch match;
				std::regex uidPattern(R"("uid"\s*:\s*"net\.minecraft")");
				if(std::regex_search(json,match,uidPattern)) {
					std::string tail=json.substr(match.position()+match.length());
					tail=tail.substr(0,tail.find('}'));
					std::regex versionPattern("\"version\"\\s*:\\s*\"([^\"]+)\"");
					if(std::regex_search(tail,match,versionPattern)) in.version=utf8ToWide(match[1]);
				}
			}
			if(in.version.empty()) in.version=L"Minecraft";
			instances.push_back(std::move(in));
		}
	}
	std::sort(instances.begin(),instances.end(),[](const Instance& a,const Instance& b){return a.name<b.name;});
	if(!instances.empty() && std::none_of(instances.begin(),instances.end(),[](const Instance& i){return i.id==mainId;})) mainId=instances.front().id;
	if(instances.empty()) mainId.clear();
	selectedId.clear();
	scrollX=0;
	choosingMain=false;
	persist();
}

static const Instance* lookup(const std::wstring& id) {
	for(const auto& i:instances) if(i.id==id) return &i;
	return nullptr;
}

static fs::path imageFor(const Instance& in) {
	std::wstring key=safeId(in.icon)?in.icon:L"grass";
	if(key==L"default" || key.empty()) key=L"grass";
	std::error_code ec;

	auto instDir=prismDir/L"instances"/in.id;
	for(auto ext:{L".png",L".jpg",L".jpeg",L".ico",L".bmp"}) {
		auto path=instDir/(key+ext);
		if(fs::is_regular_file(path,ec)) return path;
	}
	if(fs::is_regular_file(instDir/L"icon.png",ec)) return instDir/L"icon.png";

	std::wstring iconsDirSetting=cfgRead(prismDir/L"prismlauncher.cfg",L"IconsDir",L"icons");
	fs::path custom=iconsDirSetting;
	if(custom.is_relative()) custom=prismDir/custom;
	for(auto ext:{L".png",L".jpg",L".jpeg",L".ico",L".bmp"}) {
		auto path=custom/(key+ext);
		if(fs::is_regular_file(path,ec)) return path;
	}
	if(fs::is_directory(custom,ec)) {
		for(auto& folder:fs::directory_iterator(custom,fs::directory_options::skip_permission_denied,ec)) {
			if(!folder.is_directory(ec)) continue;
			for(auto ext:{L".png",L".jpg",L".jpeg",L".ico",L".bmp"}) {
				auto path=folder.path()/(key+ext);
				if(fs::is_regular_file(path,ec)) return path;
			}
		}
	}

	std::wstring iconTheme=cfgRead(prismDir/L"prismlauncher.cfg",L"IconTheme",L"");
	if(!iconTheme.empty()) {
		fs::path themeDir=prismDir/L"iconthemes"/iconTheme;
		for(auto ext:{L".png",L".jpg",L".jpeg",L".ico",L".bmp"}) {
			auto path=themeDir/(key+ext);
			if(fs::is_regular_file(path,ec)) return path;
		}
	}

	auto builtin=assetDir/L"prism-icons"/(key+L".png");
	if(fs::is_regular_file(builtin,ec)) return builtin;
	auto legacy=assetDir/L"prism-icons"/(key+L"_legacy.png");
	if(fs::is_regular_file(legacy,ec)) return legacy;

	if(key!=L"grass") {
		Instance fallback=in;
		fallback.icon=L"grass";
		return imageFor(fallback);
	}
	return builtin;
}

static void ensureFontFile(const fs::path& target,const std::wstring& relUrl) {
	std::error_code ec;
	if(fs::is_regular_file(target,ec) && fs::file_size(target,ec)>0) return;
	fs::create_directories(target.parent_path(),ec);
	std::wstring fullUrl=L"https://raw.githubusercontent.com/SILENCE-SIMSOOL/FontManager/main/"+relUrl;
	URLDownloadToFileW(nullptr,fullUrl.c_str(),target.c_str(),0,nullptr);
}

static std::unique_ptr<FontFamily> loadFamilyFrom(std::unique_ptr<PrivateFontCollection>& coll, const fs::path& path) {
	std::error_code ec;
	if(!fs::exists(path, ec) || fs::file_size(path, ec) == 0) return nullptr;
	coll = std::make_unique<PrivateFontCollection>();
	if(coll->AddFontFile(path.c_str()) != Ok) return nullptr;
	int count = coll->GetFamilyCount();
	if(count <= 0) return nullptr;
	std::vector<FontFamily> fams(count);
	coll->GetFamilies(count, fams.data(), &count);
	if(count > 0) return std::unique_ptr<FontFamily>(fams[0].Clone());
	return nullptr;
}

static void fontSetup() {
	wchar_t exe[32768];
	GetModuleFileNameW(nullptr,exe,32768);
	assetDir=fs::path(exe).parent_path()/L"assets";
	if(!fs::exists(assetDir)) assetDir=fs::path(exe).parent_path().parent_path()/L"assets";

	std::error_code ec;
	fs::create_directories(sharedFontDir,ec);

	// Ensure Pretendard fonts exist at shared Lucent font path
	ensureFontFile(sharedFontDir/L"pretendard.ttf",L"pretendard/pretendard.ttf");
	ensureFontFile(sharedFontDir/L"pretendard_medium.ttf",L"pretendard/pretendard_medium.ttf");
	ensureFontFile(sharedFontDir/L"pretendard_semibold.ttf",L"pretendard/pretendard_semibold.ttf");
	ensureFontFile(sharedFontDir/L"pretendard_bold.ttf",L"pretendard/pretendard_bold.ttf");

	// Font Awesome Icon Font
	auto faShared=sharedFontDir/L"fa-solid-900.ttf";
	auto faAsset=assetDir/L"fonts"/L"fa-solid-900.ttf";
	if(!fs::exists(faShared,ec) && fs::exists(faAsset,ec)) {
		fs::copy_file(faAsset,faShared,fs::copy_options::overwrite_existing,ec);
	}

	fs::path fontBase=fs::exists(sharedFontDir/L"pretendard.ttf",ec)?sharedFontDir:(assetDir/L"fonts");

	regularFamily=loadFamilyFrom(privateFonts,fontBase/L"pretendard.ttf");
	if(!regularFamily) regularFamily=loadFamilyFrom(privateFonts,assetDir/L"fonts"/L"Pretendard-Regular.ttf");

	mediumFamily=loadFamilyFrom(medColl,fontBase/L"pretendard_medium.ttf");
	semiboldFamily=loadFamilyFrom(semiColl,fontBase/L"pretendard_semibold.ttf");
	boldFamily=loadFamilyFrom(boldColl,fontBase/L"pretendard_bold.ttf");
	if(!boldFamily) boldFamily=loadFamilyFrom(boldColl,assetDir/L"fonts"/L"Pretendard-Bold.ttf");

	if(!privateFonts) privateFonts=std::make_unique<PrivateFontCollection>();

	iconFonts=std::make_unique<PrivateFontCollection>();
	if(fs::exists(faShared,ec)) iconFonts->AddFontFile(faShared.c_str());
	else if(fs::exists(faAsset,ec)) iconFonts->AddFontFile(faAsset.c_str());

	int iconCount=iconFonts->GetFamilyCount();
	if(iconCount>0) {
		std::vector<FontFamily> families(iconCount);
		iconFonts->GetFamilies(iconCount,families.data(),&iconCount);
		if(iconCount>0) {
			iconFamily.reset(families[0].Clone());
		}
	}
}

static Color rgb(int r,int g,int b,int a=255) { return Color(a,r,g,b); }

static void roundedPath(GraphicsPath& path,float x,float y,float w,float h,float radius) {
	if(radius<=0.f) {
		path.AddRectangle(RectF(x,y,w,h));
		return;
	}
	float d=radius*2.f;
	if(d>w) d=w;
	if(d>h) d=h;
	path.AddArc(x,y,d,d,180.f,90.f);
	path.AddArc(x+w-d,y,d,d,270.f,90.f);
	path.AddArc(x+w-d,y+h-d,d,d,0.f,90.f);
	path.AddArc(x,y+h-d,d,d,90.f,90.f);
	path.CloseFigure();
}

static void rect(Graphics& g,float x,float y,float w,float h,Color color,int radius=0) {
	SolidBrush brush(color);
	if(radius<=0) {
		g.FillRectangle(&brush,x,y,w,h);
		return;
	}
	GraphicsPath path;
	roundedPath(path,x,y,w,h,(float)radius);
	g.FillPath(&brush,&path);
}

static void line(Graphics& g,float x,float y,float w,Color color) {
	Pen pen(color,1.f);
	g.DrawLine(&pen,x,y,x+w,y);
}

static void vline(Graphics& g,float x,float y,float h,Color color) {
	Pen pen(color,1.f);
	g.DrawLine(&pen,x,y,x,y+h);
}

static void drawChevron(Graphics& g,float cx,float cy,bool right,Color color) {
	Pen pen(color,2.0f);
	pen.SetStartCap(LineCapRound);
	pen.SetEndCap(LineCapRound);
	pen.SetLineJoin(LineJoinRound);
	float dir=right?1.f:-1.f;
	PointF pts[3]={
		{cx-2.5f*dir,cy-4.5f},
		{cx+2.0f*dir,cy},
		{cx-2.5f*dir,cy+4.5f}
	};
	g.DrawLines(&pen,pts,3);
}

static void label(Graphics& g,const std::wstring& s,float x,float y,float w,float h,int size=14,Color c=Color(255,31,41,55),FontWeight weight=FontWeight::Regular,int align=0) {
	FontFamily* family=nullptr;
	INT style=FontStyleRegular;
	if(language!=L"zh" && language!=L"ja") {
		switch(weight) {
		case FontWeight::Bold:
			if(boldFamily) family=boldFamily.get();
			else if(semiboldFamily) family=semiboldFamily.get();
			else { family=regularFamily.get(); style=FontStyleBold; }
			break;
		case FontWeight::SemiBold:
			if(semiboldFamily) family=semiboldFamily.get();
			else if(boldFamily) family=boldFamily.get();
			else { family=regularFamily.get(); style=FontStyleBold; }
			break;
		case FontWeight::Medium:
			if(mediumFamily) family=mediumFamily.get();
			else family=regularFamily.get();
			break;
		case FontWeight::Regular:
		default:
			family=regularFamily.get();
			break;
		}
	}
	const wchar_t* fallbackName=language==L"zh"?L"Microsoft YaHei UI":language==L"ja"?L"Yu Gothic UI":language==L"ko"?L"Malgun Gothic":L"Segoe UI";
	FontFamily fallback(fallbackName);
	if(!family) {
		family=&fallback;
		if(weight==FontWeight::Bold || weight==FontWeight::SemiBold) style=FontStyleBold;
	}
	Font font(family,(REAL)size,style,UnitPixel);
	SolidBrush brush(c);
	StringFormat format(StringFormat::GenericTypographic());
	format.SetFormatFlags(StringFormatFlagsNoWrap|StringFormatFlagsMeasureTrailingSpaces);
	format.SetTrimming(StringTrimmingEllipsisCharacter);
	if(align==1) format.SetAlignment(StringAlignmentCenter);
	else if(align==2) format.SetAlignment(StringAlignmentFar);
	format.SetLineAlignment(StringAlignmentCenter);
	g.DrawString(s.c_str(),-1,&font,RectF(x,y,w,h),&format,&brush);
}

static void label(Graphics& g,const std::wstring& s,float x,float y,float w,float h,int size,Color c,bool bold,int align=0) {
	label(g,s,x,y,w,h,size,c,bold?FontWeight::Bold:FontWeight::Regular,align);
}

static void glyph(Graphics& g,wchar_t code,int x,int y,int size,Color color) {
	if(!iconFamily) return;
	Font font(iconFamily.get(),(REAL)size,FontStyleRegular,UnitPixel);
	SolidBrush brush(color);
	StringFormat format(StringFormat::GenericTypographic());
	format.SetFormatFlags(StringFormatFlagsNoWrap|StringFormatFlagsMeasureTrailingSpaces);
	format.SetAlignment(StringAlignmentCenter);
	format.SetLineAlignment(StringAlignmentCenter);
	wchar_t text[]={code,0};
	g.DrawString(text,1,&font,RectF((REAL)x,(REAL)y,(REAL)size,(REAL)size),&format,&brush);
}

static void panel(Graphics& g,float x,float y,float w,float h,Color fill,Color edge,float radius=8.f,float strokeWidth=1.f) {
	GraphicsPath path;
	roundedPath(path,x,y,w,h,radius);
	SolidBrush brush(fill);
	g.FillPath(&brush,&path);
	if(edge.GetA()>0 && strokeWidth>0.f) {
		Pen pen(edge,strokeWidth);
		pen.SetAlignment(PenAlignmentInset);
		g.DrawPath(&pen,&path);
	}
}

static void hit(int x,int y,int w,int h,int kind,int index=0) {
	clicks.push_back({Rect(x,y,w,h),kind,index});
}

static void button(Graphics& g,int x,int y,int w,int h,const std::wstring& text,int kind,int index,bool active=false,wchar_t icon=0,bool available=true,int radius=8) {
	bool hover=available && hoveredKind==kind && hoveredIndex==index;
	Color bg=active?(hover?rgb(0,98,233):rgb(20,117,255)):(hover?rgb(241,245,249):rgb(255,255,255));
	Color border=active?bg:rgb(203,213,225);
	if(!available) {
		bg=rgb(226,232,240);
		border=bg;
	}
	panel(g,(float)x,(float)y,(float)w,(float)h,bg,border,(float)radius);
	Color fg=active&&available?rgb(255,255,255):available?rgb(30,41,59):rgb(148,163,184);
	if(icon) {
		glyph(g,icon,x+14,y+(h-18)/2,18,fg);
		label(g,text,(float)x+38,(float)y,(float)w-46,(float)h,14,fg,active);
	} else {
		label(g,text,(float)x,(float)y,(float)w,(float)h,14,fg,active,1);
	}
	if(available) hit(x,y,w,h,kind,index);
}

static void instanceIcon(Graphics& g,const Instance& in,int x,int y,int size,int cornerRadius=8) {
	auto path=imageFor(in);
	auto key=path.wstring();
	auto found=iconCache.find(key);
	if(found==iconCache.end()) found=iconCache.emplace(key,std::make_unique<Bitmap>(path.c_str())).first;
	auto& bmp=*found->second;

	GraphicsPath clip;
	roundedPath(clip,(float)x,(float)y,(float)size,(float)size,(float)cornerRadius);

	if(bmp.GetLastStatus()==Ok && bmp.GetWidth() && bmp.GetHeight()) {
		float scale=std::max((float)size/bmp.GetWidth(),(float)size/bmp.GetHeight());
		float w=bmp.GetWidth()*scale, h=bmp.GetHeight()*scale;
		float ox=x+(size-w)/2.f, oy=y+(size-h)/2.f;
		GraphicsState state=g.Save();
		g.SetClip(&clip,CombineModeIntersect);
		g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
		g.DrawImage(&bmp,ox,oy,w,h);
		g.Restore(state);
	} else {
		SolidBrush bgBrush(rgb(226,232,240));
		g.FillPath(&bgBrush,&clip);
		glyph(g,0xF11B,x+size/4,y+size/4,size/2,rgb(82,124,178));
	}
	Pen borderPen(rgb(226,232,240),1.f);
	borderPen.SetAlignment(PenAlignmentInset);
	g.DrawPath(&borderPen,&clip);
}

static int maxScrollFor(int width) {
	return std::max(0,contentWidth-(width-48));
}

static void scrollGeometry(int width,int& thumbWidth,int& thumbX) {
	int area=width-48, maxScroll=maxScrollFor(width);
	thumbWidth=std::max(38,area*area/std::max(area,contentWidth));
	thumbX=24+(maxScroll?(int)std::lround(scrollPosition*(area-thumbWidth)/maxScroll):0);
}

static void paint(Graphics& g,int width,int height) {
	clicks.clear();
	g.SetSmoothingMode(SmoothingModeAntiAlias);
	g.SetTextRenderingHint(TextRenderingHintAntiAlias);

	rect(g,0,0,(float)width,(float)height,rgb(255,255,255));

	Color blue=rgb(20,117,255);
	Color gold=rgb(245,158,11);
	Color goldHover=rgb(217,119,6);
	Color muted=rgb(100,116,139);
	Color border=rgb(226,232,240);
	Color textDark=rgb(15,23,42);

	// Top Navigation Bar - Smooth Sliding Active Pill
	float pillX=24.f+tabPosition*100.f;
	panel(g,pillX,12,94,34,rgb(239,246,255),Color(0,0,0,0),8);

	Color t0Col=rgb((int)(20+(100-20)*tabPosition),(int)(117+(116-117)*tabPosition),(int)(255+(139-255)*tabPosition));
	glyph(g,0xF015,38,21,15,t0Col);
	label(g,tr(L"Home",L"메인",L"主页",L"メイン"),56,12,54,34,14,t0Col,tabPosition<0.5f?FontWeight::SemiBold:FontWeight::Medium);
	hit(24,12,94,34,1,0);

	Color t1Col=rgb((int)(100+(20-100)*tabPosition),(int)(116+(117-116)*tabPosition),(int)(139+(255-139)*tabPosition));
	glyph(g,0xF013,138,21,15,t1Col);
	label(g,tr(L"Options",L"옵션",L"设置",L"設定"),156,12,54,34,14,t1Col,tabPosition>=0.5f?FontWeight::SemiBold:FontWeight::Medium);
	hit(124,12,94,34,1,1);

	// Update Check Button (Top Right) with smooth hover
	float upHov=btnUpdateHover;
	Color upBg=rgb((int)(255-14*upHov),(int)(255-10*upHov),(int)(255-6*upHov));
	panel(g,(float)width-146,12,122,34,upBg,border,8);
	glyph(g,0xF021,width-136,21,14,muted);
	label(g,tr(L"Check updates",L"업데이트 확인",L"检查更新",L"更新を確認"),(float)width-118,12,86,34,12,rgb(71,85,105),FontWeight::Medium,1);
	hit(width-146,12,122,34,8);

	line(g,0,52,(float)width,rgb(241,245,249));

	if(optionsPage) {
		// Options Page
		label(g,tr(L"General settings",L"일반 설정",L"常规设置",L"一般設定"),24,68,400,24,18,textDark,FontWeight::Bold);

		glyph(g,0xF0AC,24,103,18,muted);
		label(g,tr(L"Language",L"언어",L"语言",L"言語"),52,99,300,24,14,rgb(51,65,85),FontWeight::SemiBold);

		std::wstring lang=language==L"ko"?L"한국어":language==L"zh"?L"简体中文":language==L"ja"?L"日本語":L"English";
		bool langHover=hoveredKind==2 && hoveredIndex==0;
		panel(g,(float)width-244,94,220,34,langHover?rgb(248,250,252):rgb(255,255,255),langDropdownOpen?blue:rgb(203,213,225),6);
		label(g,lang,(float)width-232,94,170,34,13,textDark,FontWeight::Medium);
		glyph(g,langDropdownOpen?0xF077:0xF078,width-48,105,11,langDropdownOpen?blue:muted);
		hit(width-244,94,220,34,2,0);

		glyph(g,0xF07B,24,144,18,muted);
		label(g,tr(L"PrismSync settings directory",L"PrismSync 설정 경로",L"PrismSync 设置目录",L"PrismSync 設定フォルダー"),52,140,(float)width-96,22,13,rgb(71,85,105),FontWeight::SemiBold);

		panel(g,52,164,(float)width-76,34,rgb(248,250,252),border,6);
		label(g,settingsDir.wstring(),64,164,(float)width-130,34,12,muted,FontWeight::Regular);
		glyph(g,0xF07B,width-48,173,15,muted);
		hit(width-60,164,36,34,11);

		line(g,24,210,(float)width-48,border);

		label(g,tr(L"Choose items to copy",L"복사할 항목 선택",L"选择复制项目",L"コピーする項目の選択"),24,222,(float)width-48,24,17,textDark,FontWeight::Bold);

		const std::wstring names[]={
			tr(L"Minecraft settings",L"마인크래프트 설정",L"Minecraft 设置",L"Minecraft 設定"),
			tr(L"Sodium settings",L"소듐 설정",L"Sodium 设置",L"Sodium 設定"),
			tr(L"Mod configuration folder",L"모드 콘피그 폴더",L"模组配置文件夹",L"MOD 設定フォルダー"),
			tr(L"Resource packs",L"리소스팩",L"资源包",L"リソースパック"),
			tr(L"Shaders",L"셰이더",L"光影包",L"シェーダー"),
			tr(L"Saved worlds",L"세이브 월드",L"存档世界",L"ワールド"),
			tr(L"Screenshots",L"스크린샷",L"截图",L"スクリーンショット")
		};
		const std::wstring details[]={
			tr(L"options.txt and other Minecraft configuration files",L"options.txt 등 마인크래프트 설정 파일",L"options.txt 等 Minecraft 配置文件",L"options.txt などの設定ファイル"),
			tr(L"sodium configuration files",L"sodium 설정 파일",L"sodium 配置文件",L"sodium 設定ファイル"),
			tr(L"config folder (mod settings)",L"config 폴더 (모드 설정)",L"config 文件夹 (模组设置)",L"config フォルダー (MOD 設定)"),
			tr(L"resourcepacks folder",L"resourcepacks 폴더",L"resourcepacks 文件夹",L"resourcepacks フォルダー"),
			tr(L"shaderpacks folder",L"shaderpacks 폴더",L"shaderpacks 文件夹",L"shaderpacks フォルダー"),
			tr(L"saves folder",L"saves 폴더",L"saves 文件夹",L"saves フォルダー"),
			tr(L"screenshots folder",L"screenshots 폴더",L"screenshots 文件夹",L"screenshots フォルダー")
		};
		const wchar_t icons[]={0xF11B,0xF1B2,0xF07B,0xF03E,0xF6FC,0xF0C7,0xF030};

		int rowHeight=38;
		for(int i=0;i<7;i++) {
			int y=252+i*rowHeight;
			glyph(g,icons[i],24,y+9,16,muted);
			label(g,names[i],52,(float)y+2,(float)width-160,18,14,rgb(30,41,59),FontWeight::SemiBold);
			label(g,details[i],52,(float)y+20,(float)width-160,16,12,muted,FontWeight::Regular);

			float t=togglePosition[i];
			float trackX=(float)width-72, trackY_=(float)y+6, trackW=46.f, trackH=26.f;
			Color trackFill=rgb((int)(226+(37-226)*t),(int)(232+(99-232)*t),(int)(240+(235-240)*t));
			Color trackEdge=rgb((int)(203+(30-203)*t),(int)(213+(85-213)*t),(int)(225+(210-225)*t));
			panel(g,trackX,trackY_,trackW,trackH,trackFill,trackEdge,13.f);

			float knobX=trackX+3.f+20.f*t;
			float knobY=trackY_+3.f;
			panel(g,knobX,knobY+1.f,20.f,20.f,rgb(0,0,0,25),Color(0,0,0,0),10.f);
			panel(g,knobX,knobY,20.f,20.f,rgb(255,255,255),Color(0,0,0,0),10.f);

			hit(width-80,y,64,rowHeight-1,3,i);
			line(g,24,(float)y+rowHeight-1,(float)width-48,rgb(241,245,249));
		}

		// Floating Custom Language Dropdown Menu
		if(langDropdownOpen || langDropdownAnim>0.01f) {
			float dropX=(float)width-244, dropY=132.f, dropW=220.f, dropH=148.f*langDropdownAnim;
			GraphicsState dropState=g.Save();
			g.SetClip(RectF(dropX-4,dropY-4,dropW+8,dropH+8));
			panel(g,dropX+2,dropY+2,dropW,dropH,rgb(0,0,0,25),Color(0,0,0,0),8);
			panel(g,dropX,dropY,dropW,dropH,rgb(255,255,255),rgb(203,213,225),8);
			const wchar_t* langNames[]={L"English",L"한국어",L"简体中文",L"日本語"};
			const wchar_t* langCodes[]={L"en",L"ko",L"zh",L"ja"};
			for(int i=0;i<4;i++) {
				float itemY=dropY+i*36.f;
				bool itHover=hoveredKind==12 && hoveredIndex==i;
				bool itActive=language==langCodes[i];
				if(itHover) panel(g,dropX+4,itemY+2,dropW-8,32,rgb(241,245,249),Color(0,0,0,0),6);
				label(g,langNames[i],dropX+14,itemY,dropW-48,36,13,itActive?blue:textDark,itActive?FontWeight::SemiBold:FontWeight::Regular);
				if(itActive) glyph(g,0xF00C,width-52,(int)itemY+11,13,blue);
				hit((int)dropX,(int)itemY,(int)dropW,36,12,i);
			}
			g.Restore(dropState);
		}
		return;
	}

	// Prism Launcher Path Field
	label(g,tr(L"Prism Launcher directory",L"Prism Launcher 경로",L"Prism Launcher 目录",L"Prism Launcher フォルダー"),24,68,(float)width-48,20,13,rgb(30,41,59),FontWeight::SemiBold);
	panel(g,24,90,(float)width-48,38,rgb(255,255,255),border,8);
	glyph(g,0xF07B,38,100,15,muted);
	label(g,prismDir.wstring(),58,90,(float)width-110,38,12,muted,FontWeight::Regular);
	bool fldHover=hoveredKind==4;
	panel(g,(float)width-56,94,30,30,fldHover?rgb(241,245,249):rgb(255,255,255),fldHover?border:Color(0,0,0,0),6);
	glyph(g,0xF07B,width-49,101,15,rgb(51,65,85));
	hit(width-60,90,36,38,4);

	// Main Instance Card
	const Instance* main=lookup(mainId);
	if(main) {
		panel(g,24,140,(float)width-48,102,rgb(240,247,255),rgb(203,228,255),12);
		instanceIcon(g,*main,38,151,80,10);

		// Measure Name
		FontFamily* nameFam=boldFamily?boldFamily.get():(semiboldFamily?semiboldFamily.get():regularFamily.get());
		Font font(nameFam?nameFam:regularFamily.get(),20.f,FontStyleRegular,UnitPixel);
		RectF layoutRect(132.f,151.f,(float)width-360.f,26.f);
		RectF bounds;
		StringFormat format(StringFormat::GenericTypographic());
		format.SetFormatFlags(StringFormatFlagsNoWrap|StringFormatFlagsMeasureTrailingSpaces);
		g.MeasureString(main->name.c_str(),-1,&font,layoutRect,&format,&bounds);

		label(g,main->name,132,151,bounds.Width+2,26,20,textDark,FontWeight::Bold);

		// Pill Badge with Crown
		float badgeX=132+bounds.Width+8.f;
		float badgeW=90.f;
		panel(g,badgeX,154,badgeW,20,rgb(219,234,254),rgb(191,219,254),10);
		glyph(g,0xF521,(int)badgeX+8,158,11,blue);
		label(g,tr(L"Main instance",L"메인 인스턴스",L"主实例",L"メインインスタンス"),badgeX+20,154,badgeW-24,20,11,blue,FontWeight::SemiBold,1);

		// Version
		label(g,main->version,132,179,(float)width-360,18,13,rgb(51,65,85),FontWeight::Regular);

		// Path with document icon
		std::wstring instPath=(prismDir/L"instances"/main->id).wstring();
		glyph(g,0xF15B,132,202,11,rgb(148,163,184));
		label(g,instPath,146,199,(float)width-370,18,12,rgb(148,163,184),FontWeight::Regular);

		// Change Main Button with smooth hover
		float chHov=btnChangeHover;
		Color chBg=choosingMain?rgb(254,243,199):rgb((int)(255-14*chHov),(int)(255-10*chHov),(int)(255-6*chHov));
		Color chEdge=choosingMain?gold:rgb((int)(226-23*chHov),(int)(232-19*chHov),(int)(240-15*chHov));
		panel(g,(float)width-212,171,176,40,chBg,chEdge,8);
		glyph(g,choosingMain?0xF00D:0xF362,width-198,183,15,choosingMain?goldHover:rgb(51,65,85));
		label(g,choosingMain?tr(L"Cancel",L"선택 취소",L"取消",L"キャンセル"):tr(L"Change main instance",L"다른 인스턴스로 변경",L"更改主实例",L"メインを変更"),(float)width-176,171,132,40,13,choosingMain?goldHover:rgb(51,65,85),FontWeight::Medium,1);
		hit(width-212,171,176,40,5);
	} else {
		panel(g,24,140,(float)width-48,102,rgb(248,250,252),border,12);
		label(g,tr(L"No instances found. Choose your Prism Launcher directory.",L"인스턴스를 찾지 못했어. Prism Launcher 경로를 확인해.",L"未找到实例，请选择 Prism Launcher 目录。",L"インスタンスが見つかりません。フォルダーを確認してください。"),40,176,(float)width-80,30,14,muted,FontWeight::Regular,1);
	}

	// Other Instances Section Header
	int count=0;
	for(auto& i:instances) if(i.id!=mainId) count++;
	std::wstring otherTitle=choosingMain?tr(L"Choose a new main instance (Click to switch)",L"새 메인 인스턴스 선택 (클릭하면 바로 변경됩니다)",L"选择新的主实例（点击直接切换）",L"새로운 메인 선택 (클릭으로 즉시 변경)"):
		(tr(L"Other instances",L"다른 인스턴스들",L"其他实例",L"他のインスタンス")+L" ("+std::to_wstring(count)+L")");
	label(g,otherTitle,24,254,(float)width-136,24,16,choosingMain?goldHover:textDark,FontWeight::Bold);

	// Navigation Arrows (< >) with vector chevrons and smooth hover
	float lHov=navHoverLeft;
	Color lBg=rgb((int)(255-14*lHov),(int)(255-10*lHov),(int)(255-6*lHov));
	Color lEdge=rgb((int)(226-23*lHov),(int)(232-19*lHov),(int)(240-15*lHov));
	Color lIcon=rgb((int)(100-85*lHov),(int)(116-93*lHov),(int)(139-97*lHov));
	panel(g,(float)width-96,250,32,32,lBg,lEdge,8);
	drawChevron(g,(float)width-80,266,false,lIcon);
	hit(width-96,250,32,32,10,-1);

	float rHov=navHoverRight;
	Color rBg=rgb((int)(255-14*rHov),(int)(255-10*rHov),(int)(255-6*rHov));
	Color rEdge=rgb((int)(226-23*rHov),(int)(232-19*rHov),(int)(240-15*rHov));
	Color rIcon=rgb((int)(100-85*rHov),(int)(116-93*rHov),(int)(139-97*rHov));
	panel(g,(float)width-56,250,32,32,rBg,rEdge,8);
	drawChevron(g,(float)width-40,266,true,rIcon);
	hit(width-56,250,32,32,10,1);

	// Instances Grid Area
	contentWidth=std::max(0,count*(cardWidth+cardGap)-cardGap);
	scrollX=std::clamp(scrollX,0,maxScrollFor(width));
	scrollPosition=std::clamp(scrollPosition,0.f,(float)maxScrollFor(width));

	GraphicsState saved=g.Save();
	g.SetClip(Rect(24,listTop-4,width-48,listHeight+8));
	int index=0;
	for(auto& in:instances) {
		if(in.id==mainId) continue;
		int x=24+index*(cardWidth+cardGap)-(int)std::lround(scrollPosition);
		bool selected=selectedId==in.id;
		float hov=cardHover[index<32?index:0];
		if(x+cardWidth>24 && x<width-24) {
			float cardY=(float)listTop-2.f*hov;
			if(hov>0.02f) panel(g,(float)x,cardY+3.f,(float)cardWidth,(float)cardHeight,rgb(0,0,0,(int)(18*hov)),Color(0,0,0,0),10.f);

			Color cardBg=choosingMain?rgb(254,(int)(243+8*(1.f-hov)),(int)(199+36*(1.f-hov))):(selected?rgb(239,246,255):rgb((int)(255-7*hov),(int)(255-5*hov),(int)(255-3*hov)));
			Color cardEdge=choosingMain?(hov>0.5f?goldHover:gold):(selected?blue:rgb((int)(226-23*hov),(int)(232-19*hov),(int)(240-15*hov)));
			float strokeW=(choosingMain || selected)?2.f:1.f;
			panel(g,(float)x,cardY,(float)cardWidth,(float)cardHeight,cardBg,cardEdge,10.f,strokeW);

			instanceIcon(g,in,x+20,(int)cardY+10,68,9);
			label(g,in.name,(float)x+4,cardY+82,(float)cardWidth-8,18,13,textDark,FontWeight::SemiBold,1);

			// Version with cube glyph centered
			FontFamily* vFam=mediumFamily?mediumFamily.get():(regularFamily?regularFamily.get():nullptr);
			const wchar_t* fallbackName=language==L"zh"?L"Microsoft YaHei UI":language==L"ja"?L"Yu Gothic UI":language==L"ko"?L"Malgun Gothic":L"Segoe UI";
			FontFamily fallback(fallbackName);
			Font vFont(vFam?vFam:&fallback,12.f,FontStyleRegular,UnitPixel);
			RectF vBounds;
			StringFormat vFmt(StringFormat::GenericTypographic());
			vFmt.SetFormatFlags(StringFormatFlagsNoWrap|StringFormatFlagsMeasureTrailingSpaces);
			g.MeasureString(in.version.c_str(),-1,&vFont,RectF(0,0,200,20),&vFmt,&vBounds);
			float totW=14.f+vBounds.Width;
			float sX=(float)x+((float)cardWidth-totW)/2.f;
			glyph(g,0xF1B2,(int)sX,(int)cardY+105,10,rgb(148,163,184));
			label(g,in.version,sX+14.f,cardY+102,vBounds.Width+4.f,18.f,12,rgb(100,116,139),FontWeight::Medium);

			if(selected && !choosingMain) {
				panel(g,(float)x+6,cardY+6,18,18,blue,blue,9);
				glyph(g,0xF00C,x+8,(int)cardY+8,10,rgb(255,255,255));
			} else if(choosingMain) {
				panel(g,(float)x+6,cardY+6,18,18,gold,gold,9);
				glyph(g,0xF00C,x+8,(int)cardY+8,10,rgb(255,255,255));
			}

			int left=std::max(24,x), right=std::min(width-24,x+cardWidth);
			hit(left,listTop,right-left,cardHeight,6,index);
		}
		index++;
	}
	g.Restore(saved);

	// Horizontal Scrollbar
	if(maxScrollFor(width)>0) {
		panel(g,24,(float)trackY,(float)width-48,4,rgb(241,245,249),Color(0,0,0,0),2);
		int tw,tx;
		scrollGeometry(width,tw,tx);
		panel(g,(float)tx,(float)trackY,(float)tw,4,rgb(203,213,225),Color(0,0,0,0),2);
		hit(24,trackY-3,width-48,12,7);
	}

	// Bottom Action Bar
	line(g,24,438,(float)width-48,border);

	if(choosingMain) {
		button(g,24,450,142,40,tr(L"Cancel",L"선택 취소",L"取消",L"キャンセル"),5,0,false,0xF00D,true,8);
		vline(g,178,454,32,rgb(226,232,240));
		glyph(g,0xF05A,192,460,18,goldHover);
		label(g,tr(L"Click any instance card above to immediately set it as main.",L"위 목록에서 인스턴스를 클릭하면 즉시 메인 인스턴스로 변경됩니다.",L"点击上方任意实例卡片即可直接设为主实例。",L"上のインスタンスをクリックすると即座にメインに設定されます。"),216,450,(float)width-236,40,13,goldHover,FontWeight::Medium);
	} else {
		bool hasTarget=lookup(selectedId)!=nullptr;
		float apHov=btnApplyHover;
		Color apBg=hasTarget?(apHov>0.5f?rgb(0,98,233):blue):rgb(226,232,240);
		panel(g,24,450,142,40,apBg,apBg,8);
		Color apFg=hasTarget?rgb(255,255,255):rgb(148,163,184);
		glyph(g,0xF0C5,38,461,16,apFg);
		label(g,tr(L"Apply settings",L"설정 적용",L"应用设置",L"設定を適用"),58,450,98,40,14,apFg,FontWeight::SemiBold);
		hit(24,450,142,40,9,0);

		vline(g,178,454,32,rgb(226,232,240));
		const Instance* targetInst=lookup(selectedId);
		std::wstring desc;
		if(targetInst && main) {
			desc=tr((L"Copy settings from main instance ("+main->name+L") to selected instance ("+targetInst->name+L").").c_str(),
					(L"선택한 인스턴스("+targetInst->name+L")에 메인 인스턴스("+main->name+L")의 설정을 복사합니다.").c_str(),
					(L"将主实例 ("+main->name+L") 的设置复制到选定实例 ("+targetInst->name+L")。").c_str(),
					(L"メイン ("+main->name+L") の設定を選択したインスタンス ("+targetInst->name+L") へコピーします。").c_str());
		} else {
			desc=tr(L"Select an instance below to apply settings from the main instance.",L"메인 인스턴스의 설정을 적용할 대상 인스턴스를 아래에서 선택하세요.",L"请在下方选择要应用主实例设置的目标实例。",L"メインインスタンスの設定を適用するインスタンスを以下から選択してください。");
		}
		glyph(g,0xF05A,192,460,18,rgb(100,116,139));
		label(g,desc,216,450,(float)width-236,40,13,rgb(71,85,105),FontWeight::Regular);
	}

	if(!status.empty()) {
		label(g,status,24,496,(float)width-48,20,12,blue,FontWeight::Medium);
	}
}

static bool chooseFolder(fs::path& path) {
	BROWSEINFOW bi{};
	bi.hwndOwner=windowHandle;
	std::wstring title=tr(L"Choose Prism Launcher directory",L"Prism Launcher 폴더 선택",L"选择 Prism Launcher 目录",L"Prism Launcher フォルダーを選択");
	bi.lpszTitle=title.c_str();
	bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
	PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);
	if(!pid) return false;
	wchar_t buf[MAX_PATH];
	bool ok=SHGetPathFromIDListW(pid,buf);
	CoTaskMemFree(pid);
	if(ok) path=buf;
	return ok;
}

static fs::path gameDir(const Instance& i) {
	auto root=prismDir/L"instances"/i.id;
	return fs::is_directory(root/L"minecraft")?root/L"minecraft":root/L".minecraft";
}

static void copyOne(const fs::path& source,const fs::path& target,int& copied,int& errors) {
	std::error_code ec;
	if(!fs::exists(source,ec)) return;
	if(fs::is_directory(source,ec)) {
		fs::create_directories(target,ec);
		if(ec) { errors++; return; }
		for(auto it=fs::recursive_directory_iterator(source,fs::directory_options::skip_permission_denied,ec);it!=fs::recursive_directory_iterator();it.increment(ec)) {
			if(ec) { errors++; ec.clear(); continue; }
			auto rel=it->path().lexically_relative(source);
			auto dst=target/rel;
			if(it->is_directory(ec)) {
				fs::create_directories(dst,ec);
				if(ec) { errors++; ec.clear(); }
			} else if(it->is_regular_file(ec)) {
				fs::create_directories(dst.parent_path(),ec);
				fs::copy_file(it->path(),dst,fs::copy_options::overwrite_existing,ec);
				if(ec) { errors++; ec.clear(); }
				else copied++;
			}
		}
	} else {
		fs::create_directories(target.parent_path(),ec);
		fs::copy_file(source,target,fs::copy_options::overwrite_existing,ec);
		if(ec) errors++;
		else copied++;
	}
}

static void copySelected(const fs::path& src,const fs::path& dst,int& copied,int& errors) {
	if(enabled[0]) for(auto name:{L"options.txt",L"command_history.txt",L"servers.dat"}) copyOne(src/name,dst/name,copied,errors);
	if(enabled[1] && !enabled[2]) for(auto name:{L"sodium-extra.properties",L"sodium-extra-options.json",L"sodium-fingerprint.json",L"sodium-mixins.properties",L"sodium-options.json"}) copyOne(src/L"config"/name,dst/L"config"/name,copied,errors);
	if(enabled[2]) copyOne(src/L"config",dst/L"config",copied,errors);
	if(enabled[3]) for(auto name:{L"resourcepacks",L"texturepacks"}) copyOne(src/name,dst/name,copied,errors);
	if(enabled[4]) copyOne(src/L"shaderpacks",dst/L"shaderpacks",copied,errors);
	if(enabled[5]) copyOne(src/L"saves",dst/L"saves",copied,errors);
	if(enabled[6]) copyOne(src/L"screenshots",dst/L"screenshots",copied,errors);
}

static void applyTo(const Instance& target) {
	const Instance* main=lookup(mainId);
	if(!main || main->id==target.id) return;
	std::wstring message=main->name+L"  →  "+target.name+L"\n\n"+tr(L"Copy the enabled categories in this direction? Existing files will be overwritten. Close Minecraft first.",L"메인 인스턴스에서 선택한 인스턴스로 설정을 복사할까? 기존 파일은 덮어써. 먼저 마인크래프트를 종료해.",L"按此方向复制已启用的项目？现有文件将被覆盖。请先关闭 Minecraft。",L"この方向に有効な項目をコピーしますか？既存のファイルを上書きします。Minecraft を終了してください。");
	if(MessageBoxW(windowHandle,message.c_str(),L"PrismSync",MB_YESNO|MB_ICONWARNING)!=IDYES) return;
	fs::path src=gameDir(*main), dst=gameDir(target);
	if(!fs::is_directory(src) || !fs::is_directory(dst)) {
		MessageBoxW(windowHandle,tr(L"The instance game directory was not found.",L"인스턴스의 게임 폴더를 찾지 못했어.",L"未找到实例游戏目录。",L"ゲームフォルダーが見つかりません。").c_str(),L"PrismSync",MB_OK|MB_ICONERROR);
		return;
	}
	int copied=0,errors=0;
	copySelected(src,dst,copied,errors);
	status=tr(L"Files copied: ",L"복사한 파일: ",L"已复制文件：",L"コピーしたファイル：")+std::to_wstring(copied)+tr(L" · Errors: ",L" · 오류: ",L" · 错误：",L" · エラー：")+std::to_wstring(errors);
	MessageBoxW(windowHandle,status.c_str(),L"PrismSync",MB_OK|(errors?MB_ICONWARNING:MB_ICONINFORMATION));
}

static std::atomic<bool> updateChecking{false};

static void checkUpdates() {
	if(updateChecking.exchange(true)) return;

	std::thread([]() {
		struct ScopeExit {
			~ScopeExit() { updateChecking=false; }
		} guard;

		HINTERNET hNet=InternetOpenW(L"PrismSync",INTERNET_OPEN_TYPE_PRECONFIG,nullptr,nullptr,0);
		if(!hNet) {
			MessageBoxW(windowHandle,tr(L"Failed to connect to the network.",L"네트워크에 연결할 수 없어.",L"无法连接网络。",L"ネットワークに接続できません。").c_str(),L"PrismSync",MB_OK|MB_ICONWARNING);
			return;
		}

		const WCHAR* headers=L"User-Agent: PrismSync\r\nAccept: application/vnd.github.v3+json\r\n";
		HINTERNET hUrl=InternetOpenUrlW(hNet,L"https://api.github.com/repos/SILENCE-SIMSOOL/PrisimSync/releases/latest",headers,(DWORD)-1,INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_SECURE,0);
		if(!hUrl) {
			InternetCloseHandle(hNet);
			MessageBoxW(windowHandle,tr(L"Failed to check for updates.",L"업데이트를 확인하지 못했어.",L"检查更新失败。",L"アップデートの確認に失敗しました。").c_str(),L"PrismSync",MB_OK|MB_ICONWARNING);
			return;
		}

		std::string json;
		char buf[4096];
		DWORD readBytes=0;
		while(InternetReadFile(hUrl,buf,sizeof(buf),&readBytes) && readBytes>0) {
			json.append(buf,readBytes);
		}
		InternetCloseHandle(hUrl);

		std::wstring tag;
		std::wstring downloadUrl;

		std::regex tagRegex("\"tag_name\"\\s*:\\s*\"([^\"]+)\"");
		std::smatch match;
		if(std::regex_search(json,match,tagRegex) && match.size()>1) {
			tag=utf8ToWide(match[1].str());
			if(!tag.empty() && (tag[0]==L'v' || tag[0]==L'V')) {
				tag=tag.substr(1);
			}
			tag=trim(tag);
		}

		if(tag.empty()) {
			InternetCloseHandle(hNet);
			MessageBoxW(windowHandle,tr(L"Failed to retrieve version information.",L"버전 정보를 가져오지 못했어.",L"无法获取版本信息。",L"バージョン情報を取得できませんでした。").c_str(),L"PrismSync",MB_OK|MB_ICONWARNING);
			return;
		}

		if(tag==APP_VERSION) {
			InternetCloseHandle(hNet);
			std::wstring okMsg=tr((L"PrismSync is up to date (v"+APP_VERSION+L").").c_str(),
								  (L"현재 최신 버전(v"+APP_VERSION+L")을 사용 중이야.").c_str(),
								  (L"当前已是最新版本 (v"+APP_VERSION+L")。").c_str(),
								  (L"最新バージョン (v"+APP_VERSION+L") を使用しています。").c_str());
			MessageBoxW(windowHandle,okMsg.c_str(),L"PrismSync",MB_OK|MB_ICONINFORMATION);
			return;
		}

		std::regex dlRegex("\"browser_download_url\"\\s*:\\s*\"([^\"]+\\.exe)\"");
		if(std::regex_search(json,match,dlRegex) && match.size()>1) {
			downloadUrl=utf8ToWide(match[1].str());
		}
		if(downloadUrl.empty()) {
			downloadUrl=L"https://github.com/SILENCE-SIMSOOL/PrisimSync/releases/latest/download/PrismSync-Setup.exe";
		}

		std::wstring promptMsg=tr((L"New version available: v"+tag+L"\nCurrent version: v"+APP_VERSION+L"\n\nDownload and install update now?").c_str(),
								  (L"새로운 버전이 있어: v"+tag+L"\n현재 버전: v"+APP_VERSION+L"\n\n지금 다운로드하여 업데이트할까?").c_str(),
								  (L"发现新版本: v"+tag+L"\n当前版本: v"+APP_VERSION+L"\n\n立即下载并更新？").c_str(),
								  (L"新しいバージョンがあります: v"+tag+L"\n現在のバージョン: v"+APP_VERSION+L"\n\n今すぐダウンロードして更新しますか？").c_str());

		if(MessageBoxW(windowHandle,promptMsg.c_str(),L"PrismSync",MB_YESNO|MB_ICONQUESTION)!=IDYES) {
			InternetCloseHandle(hNet);
			return;
		}

		SetWindowTextW(windowHandle,tr(L"PrismSync - Downloading update...",
										L"PrismSync - 업데이트 다운로드 중...",
										L"PrismSync - 正在下载更新...",
										L"PrismSync - アップデートをダウンロード中...").c_str());

		WCHAR tempPath[MAX_PATH];
		GetTempPathW(MAX_PATH,tempPath);
		std::wstring setupExe=std::wstring(tempPath)+L"PrismSync-Setup.exe";

		HINTERNET hDl=InternetOpenUrlW(hNet,downloadUrl.c_str(),nullptr,0,INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_SECURE,0);
		if(!hDl) {
			InternetCloseHandle(hNet);
			SetWindowTextW(windowHandle,L"PrismSync");
			MessageBoxW(windowHandle,tr(L"Failed to download the update.",L"업데이트 파일을 다운로드하지 못했어.",L"下载更新失败。",L"アップデートのダウンロードに失敗しました。").c_str(),L"PrismSync",MB_OK|MB_ICONWARNING);
			return;
		}

		std::ofstream out(setupExe,std::ios::binary);
		if(!out) {
			InternetCloseHandle(hDl);
			InternetCloseHandle(hNet);
			SetWindowTextW(windowHandle,L"PrismSync");
			MessageBoxW(windowHandle,tr(L"Failed to save the installer.",L"설치 파일을 저장하지 못했어.",L"保存安装程序失败。",L"インストーラーの保存に失敗しました。").c_str(),L"PrismSync",MB_OK|MB_ICONWARNING);
			return;
		}

		char dlBuf[16384];
		DWORD dlRead=0;
		while(InternetReadFile(hDl,dlBuf,sizeof(dlBuf),&dlRead) && dlRead>0) {
			out.write(dlBuf,dlRead);
		}
		out.close();
		InternetCloseHandle(hDl);
		InternetCloseHandle(hNet);

		HINSTANCE hInst=ShellExecuteW(nullptr,L"open",setupExe.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
		if((INT_PTR)hInst>32) {
			ExitProcess(0);
		} else {
			SetWindowTextW(windowHandle,L"PrismSync");
			MessageBoxW(windowHandle,tr(L"Failed to start installer.",L"설치 프로그램을 실행하지 못했어.",L"启动安装程序失败。",L"インストーラーの起動に失敗しました。").c_str(),L"PrismSync",MB_OK|MB_ICONWARNING);
		}
	}).detach();
}

static void setScroll(int value,int width) {
	scrollX=std::clamp(value,0,maxScrollFor(width));
	if(!animations || draggingScroll) scrollPosition=(float)scrollX;
}

static void interact(int x,int y,int width) {
	if(langDropdownOpen && !(x>=width-244 && x<=width-24 && y>=96 && y<=280)) {
		langDropdownOpen=false;
		InvalidateRect(windowHandle,nullptr,FALSE);
	}

	for(auto it=clicks.rbegin();it!=clicks.rend();++it) {
		if(!it->r.Contains(x,y)) continue;
		int kind=it->kind, index=it->index;
		switch(kind) {
		case 1: optionsPage=index==1; status.clear(); langDropdownOpen=false; break;
		case 2: langDropdownOpen=!langDropdownOpen; break;
		case 3: enabled[index]=!enabled[index]; persist(); break;
		case 4: { fs::path p; if(chooseFolder(p)){ prismDir=p; mainId.clear(); refresh(); scrollPosition=0; } } break;
		case 5: choosingMain=!choosingMain; selectedId.clear(); break;
		case 6: {
			int i=0;
			for(auto& in:instances) if(in.id!=mainId) {
				if(i++==index) {
					if(choosingMain) {
						mainId=in.id;
						selectedId.clear();
						choosingMain=false;
						scrollX=0;
						persist();
					} else {
						selectedId=in.id;
					}
					break;
				}
			}
		}break;
		case 7: {
			int tw,tx;
			scrollGeometry(width,tw,tx);
			draggingScroll=true;
			dragOffset=x>=tx && x<=tx+tw?x-tx:tw/2;
			setScroll((x-24-dragOffset)*maxScrollFor(width)/std::max(1,width-48-tw),width);
			SetCapture(windowHandle);
		}break;
		case 8: checkUpdates(); break;
		case 9: if(!choosingMain && lookup(selectedId)) applyTo(*lookup(selectedId)); break;
		case 10: setScroll(scrollX+index*(cardWidth+cardGap)*2,width); break;
		case 11: ShellExecuteW(windowHandle,L"open",settingsDir.c_str(),nullptr,nullptr,SW_SHOWNORMAL); break;
		case 12: {
			const wchar_t* codes[]={L"en",L"ko",L"zh",L"ja"};
			if(index>=0 && index<4) {
				language=codes[index];
				langDropdownOpen=false;
				persist();
			}
		}break;
		}
		InvalidateRect(windowHandle,nullptr,FALSE);
		return;
	}
}

static LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam) {
	switch(msg) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc=BeginPaint(hwnd,&ps);
		RECT rc;
		GetClientRect(hwnd,&rc);
		if(rc.right>0 && rc.bottom>0) {
			Bitmap buffer(rc.right,rc.bottom,PixelFormat32bppRGB);
			Graphics back(&buffer);
			paint(back,rc.right,rc.bottom);
			Graphics g(hdc);
			g.DrawImage(&buffer,0,0);
		}
		EndPaint(hwnd,&ps);
		return 0;
	}
	case WM_ERASEBKGND: return 1;
	case WM_GETMINMAXINFO: {
		auto info=(MINMAXINFO*)lParam;
		info->ptMinTrackSize={820,540};
		return 0;
	}
	case WM_LBUTTONDOWN: {
		RECT rc;
		GetClientRect(hwnd,&rc);
		interact(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam),rc.right);
		return 0;
	}
	case WM_LBUTTONUP:
		if(draggingScroll) { draggingScroll=false; ReleaseCapture(); }
		return 0;
	case WM_CAPTURECHANGED:
		draggingScroll=false;
		return 0;
	case WM_MOUSEMOVE: {
		int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
		if(draggingScroll) {
			RECT rc;
			GetClientRect(hwnd,&rc);
			int tw,tx;
			scrollGeometry(rc.right,tw,tx);
			setScroll((x-24-dragOffset)*maxScrollFor(rc.right)/std::max(1,(int)rc.right-48-tw),rc.right);
			InvalidateRect(hwnd,nullptr,FALSE);
			return 0;
		}
		int kind=-1,index=-1;
		for(auto it=clicks.rbegin();it!=clicks.rend();++it) {
			if(it->r.Contains(x,y)) { kind=it->kind; index=it->index; break; }
		}
		SetCursor(LoadCursor(nullptr,kind<0?IDC_ARROW:IDC_HAND));
		if(kind!=hoveredKind || index!=hoveredIndex) {
			hoveredKind=kind;
			hoveredIndex=index;
			InvalidateRect(hwnd,nullptr,FALSE);
		}
		TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};
		TrackMouseEvent(&track);
		return 0;
	}
	case WM_MOUSELEAVE:
		hoveredKind=-1;
		hoveredIndex=-1;
		InvalidateRect(hwnd,nullptr,FALSE);
		return 0;
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL: {
		POINT pt{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
		ScreenToClient(hwnd,&pt);
		if(!optionsPage && pt.y>=listTop && pt.y<trackY+24) {
			RECT rc;
			GetClientRect(hwnd,&rc);
			int delta=GET_WHEEL_DELTA_WPARAM(wParam);
			setScroll(scrollX+(msg==WM_MOUSEHWHEEL?delta:-delta)*90/WHEEL_DELTA,rc.right);
			InvalidateRect(hwnd,nullptr,FALSE);
		}
		return 0;
	}
	case WM_KEYDOWN:
		if(!optionsPage && (wParam==VK_LEFT || wParam==VK_RIGHT)) {
			RECT rc;
			GetClientRect(hwnd,&rc);
			setScroll(scrollX+(wParam==VK_LEFT?-116:116),rc.right);
			return 0;
		}
		break;
	case WM_TIMER: {
		ULONGLONG now=GetTickCount64();
		float dt=(lastFrame>0 && now>=lastFrame)?(float)(now-lastFrame):10.f;
		if(dt>100.f) dt=10.f;
		lastFrame=now;
		bool changed=false;
		auto move=[&](float& value,float target,float speed) {
			if(std::abs(value-target)>.002f) {
				value+=(target-value)*(1.f-std::exp(-dt/speed));
				if(std::abs(value-target)<.004f) value=target;
				changed=true;
			}
		};
		move(tabPosition,optionsPage?1.f:0.f,70.f);
		move(scrollPosition,(float)scrollX,55.f);
		move(choosingMainAnim,choosingMain?1.f:0.f,70.f);
		move(langDropdownAnim,langDropdownOpen?1.f:0.f,50.f);
		for(int i=0;i<7;i++) move(togglePosition[i],enabled[i]?1.f:0.f,50.f);

		bool isHoverLeft=(hoveredKind==10 && hoveredIndex==-1);
		bool isHoverRight=(hoveredKind==10 && hoveredIndex==1);
		move(navHoverLeft,isHoverLeft?1.f:0.f,40.f);
		move(navHoverRight,isHoverRight?1.f:0.f,40.f);
		move(btnApplyHover,(hoveredKind==9)?1.f:0.f,40.f);
		move(btnChangeHover,(hoveredKind==5)?1.f:0.f,40.f);
		move(btnUpdateHover,(hoveredKind==8)?1.f:0.f,40.f);

		for(int i=0;i<32;i++) {
			bool h=(hoveredKind==6 && hoveredIndex==i);
			move(cardHover[i],h?1.f:0.f,45.f);
		}

		if(changed) InvalidateRect(hwnd,nullptr,FALSE);
		return 0;
	}
	case WM_SIZE:
		InvalidateRect(hwnd,nullptr,FALSE);
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd,1);
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd,msg,wParam,lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance,HINSTANCE,LPWSTR,int nCmdShow) {
	CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
	GdiplusStartupInput si;
	GdiplusStartup(&gdiplusToken,&si,nullptr);
	load();
	fontSetup();
	refresh();
	for(int i=0;i<7;i++) togglePosition[i]=enabled[i]?1.f:0.f;
	animations=true;
	WNDCLASSEXW wc{sizeof(wc)};
	wc.lpfnWndProc=wndProc;
	wc.hInstance=hInstance;
	wc.lpszClassName=L"PrismSyncWindow";
	wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
	wc.hIcon=LoadIconW(hInstance,MAKEINTRESOURCEW(1));
	wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
	RegisterClassExW(&wc);
	windowHandle=CreateWindowExW(0,wc.lpszClassName,L"PrismSync",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,900,560,nullptr,nullptr,hInstance,nullptr);
	lastFrame=GetTickCount64();
	SetTimer(windowHandle,1,10,nullptr);
	ShowWindow(windowHandle,nCmdShow);
	UpdateWindow(windowHandle);
	MSG msg;
	while(GetMessageW(&msg,nullptr,0,0)>0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	iconCache.clear();
	iconFamily.reset();
	regularFamily.reset();
	mediumFamily.reset();
	semiboldFamily.reset();
	boldFamily.reset();
	iconFonts.reset();
	boldColl.reset();
	semiColl.reset();
	medColl.reset();
	privateFonts.reset();
	GdiplusShutdown(gdiplusToken);
	CoUninitialize();
	return 0;
}