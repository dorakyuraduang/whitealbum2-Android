#include "locale.hpp"
#include "codepage.hpp"
#include "../../port/guest_api.hpp"
#include <array>
#include <chrono>
#include <optional>
namespace wa2::platform::compat {
namespace {
using namespace wr;using namespace wr::port;
struct Locale {U32 id,cp,country;std::u16string_view language,abbreviation,native,country_name,currency,symbol,short_date,long_date;};
constexpr Locale locales[]={
    {0x409,1252,1,u"English",u"ENU",u"English",u"United States",u"USD",u"$",u"M/d/yyyy",u"dddd, MMMM d, yyyy"},
    {0x411,932,81,u"Japanese",u"JPN",u"\u65e5\u672c\u8a9e",u"Japan",u"JPY",u"\u00a5",u"yyyy/MM/dd",u"yyyy\u5e74M\u6708d\u65e5"},
    {0x804,936,86,u"Chinese (Simplified)",u"CHS",u"\u4e2d\u6587(\u7b80\u4f53)",u"China",u"CNY",u"\u00a5",u"yyyy/M/d",u"yyyy\u5e74M\u6708d\u65e5"}
};
const Locale* locale(U32 id){if(id==0x400 || id==0x800 || id==0)id=0x804;if(id==0x7f)id=0x409;for(const auto& l:locales)if(l.id==id)return &l;return nullptr;}
U32 fail(Process& p,U32 code){p.set_error(code);return 0;}
std::u16string number(U32 n,unsigned width=0){const auto s=std::to_string(n);std::u16string result(s.begin(),s.end());if(width>result.size())result.insert(0,width-result.size(),u'0');return result;}
std::u16string month_name(const Locale& l,unsigned month,bool abbreviated){
    constexpr std::u16string_view english[]={u"January",u"February",u"March",u"April",u"May",u"June",u"July",u"August",u"September",u"October",u"November",u"December"};
    if(month<1 || month>12)return {};if(l.id==0x409)return std::u16string(abbreviated?english[month-1].substr(0,3):english[month-1]);return number(month)+u"\u6708";
}
std::u16string day_name(const Locale& l,unsigned monday_index,bool abbreviated){
    constexpr std::u16string_view english[]={u"Monday",u"Tuesday",u"Wednesday",u"Thursday",u"Friday",u"Saturday",u"Sunday"};
    constexpr char16_t cjk[]={u'\u4e00',u'\u4e8c',u'\u4e09',u'\u56db',u'\u4e94',u'\u516d',u'\u65e5'},japanese[]={u'\u6708',u'\u706b',u'\u6c34',u'\u6728',u'\u91d1',u'\u571f',u'\u65e5'};
    if(monday_index>=7)return {};if(l.id==0x409)return std::u16string(abbreviated?english[monday_index].substr(0,3):english[monday_index]);
    if(l.id==0x411)return std::u16string(1,japanese[monday_index])+(abbreviated?u"":u"\u66dc\u65e5");return std::u16string(abbreviated?u"\u5468":u"\u661f\u671f")+cjk[monday_index];
}
std::optional<std::u16string> info(const Locale& l,U32 type){
    if(type>=0x2a && type<=0x30)return day_name(l,type-0x2a,false);if(type>=0x31 && type<=0x37)return day_name(l,type-0x31,true);
    if(type>=0x38 && type<=0x43)return month_name(l,type-0x38+1,false);if(type>=0x44 && type<=0x4f)return month_name(l,type-0x44+1,true);
    switch(type){case 1:case 9:return l.id==0x409?u"0409":l.id==0x411?u"0411":u"0804";
    case 2:return l.id==0x804?u"Chinese (Simplified, China)":std::u16string(l.language)+u" ("+std::u16string(l.country_name)+u")";case 3:return std::u16string(l.abbreviation);case 4:return std::u16string(l.native);
    case 5:case 10:return number(l.country);case 6:case 8:case 0x1002:return std::u16string(l.country_name);case 7:return l.id==0x409?u"USA":l.id==0x411?u"JPN":u"CHN";
    case 11:case 0x1004:return number(l.cp);case 12:return u",";case 13:return l.id==0x409?u"1":u"0";case 14:case 22:return u".";case 15:case 23:return u",";
    case 16:case 24:return u"3;0";case 17:case 25:case 26:return l.id==0x411 && type!=17?u"0":u"2";case 18:return u"1";case 19:return u"0123456789";
    case 20:return std::u16string(l.symbol);case 21:return std::u16string(l.currency);case 27:case 28:return u"0";case 29:return u"/";case 30:return u":";
    case 31:return std::u16string(l.short_date);case 32:return std::u16string(l.long_date);case 33:case 34:return l.id==0x409?u"0":u"2";case 35:return l.id==0x409?u"0":u"1";
    case 36:return u"1";case 37:case 38:case 39:return u"0";case 40:return l.id==0x804?u"\u4e0a\u5348":l.id==0x411?u"\u5348\u524d":u"AM";
    case 41:return l.id==0x804?u"\u4e0b\u5348":l.id==0x411?u"\u5348\u5f8c":u"PM";case 80:return u"";case 81:return u"-";
    case 0x1001:return std::u16string(l.language);case 0x1003:return l.id==0x409?u"h:mm:ss tt":u"H:mm:ss";
    case 0x1005:return u"0";case 0x1009:return u"1";case 0x100c:return u"0";case 0x100d:return u"0";case 0x100e:case 0x100f:return u"";default:return {};
    }
}
std::u16string ascii_text(Process& p,U32 at,U32 cp){const auto bytes=p.read_string(at,65536);return decode_text(cp,{reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});}
U32 output(Process& p,U32 at,U32 capacity,std::u16string_view value,bool unicode,U32 cp){
    if(unicode){if(!capacity)return U32(value.size()+1);if(capacity<value.size()+1)return fail(p,122);p.memory.check(at,(value.size()+1)*2,Memory::Write);
        for(unsigned n=0;n<value.size();++n)p.memory.store(at+n*2,value[n],16);p.memory.store(at+U32(value.size()*2),0,16);return U32(value.size()+1);}
    auto bytes=encode_text(cp,value).bytes;bytes.push_back(0);if(!capacity)return U32(bytes.size());if(capacity<bytes.size())return fail(p,122);p.memory.copy_in(at,bytes);return U32(bytes.size());
}
std::u16string calendar(const Locale& l,const std::array<std::uint16_t,8>& t,std::u16string_view pattern){
    std::u16string result;
    for(std::size_t at=0;at<pattern.size();){const auto c=pattern[at++];if(c==u'\''){while(at<pattern.size()){if(pattern[at]==u'\''){++at;if(at<pattern.size() && pattern[at]==u'\''){result+=u'\'';++at;continue;}break;}result+=pattern[at++];}continue;}
        unsigned length=1;while(at<pattern.size() && pattern[at]==c){++at;++length;}
        switch(c){case u'y':result+=number(length<3?t[0]%100:t[0],length==2?2:length>=4?4:0);break;
        case u'M':if(length<=2)result+=number(t[1],length);else result+=month_name(l,t[1],length==3);break;
        case u'd':if(length<=2)result+=number(t[3],length);else{const auto date=std::chrono::year(int(t[0]))/t[1]/t[3];const auto sunday=std::chrono::weekday(std::chrono::sys_days(date)).c_encoding();result+=day_name(l,(sunday+6)%7,length==3);}break;
        case u'H':result+=number(t[4],length>1?2:0);break;case u'h':result+=number(t[4]%12?t[4]%12:12,length>1?2:0);break;
        case u'm':result+=number(t[5],length>1?2:0);break;case u's':result+=number(t[6],length>1?2:0);break;
        case u't':{const auto period=*info(l,t[4]<12?40:41);result+=length==1?period.substr(0,1):period;break;}
        default:result.append(length,c);break;}
        if(result.size()>65536)throw EncodingError("calendar output limit");
    }return result;
}
}
void install_locale(wr::Process& p){
    auto bind=[&](const char* name,unsigned count,std::function<U32(Process&,Args)> fn){p.replace_api("kernel32.dll",name,count,[count,fn=std::move(fn)](Process& q){std::array<U32,6> args{};for(unsigned n=0;n<count;++n)args[n]=q.argument(n);return fn(q,std::span(args).first(count));});};
    for(bool unicode:{false,true})bind(unicode?"GetLocaleInfoW":"GetLocaleInfoA",4,[unicode](Process& q,Args a){const auto* l=locale(a[0]);if(!l || a[3]>1048576)return fail(q,87);const auto type=a[1]&0xffff;const auto value=info(*l,type);if(!value)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"locale field outside common locale contract: "+hex(type));
        if(a[1]&0x20000000){U32 number{};for(auto c:*value){unsigned digit=c>=u'0' && c<=u'9'?c-u'0':16;if(digit>=((type==1 || type==9)?16u:10u))return fail(q,1004);number=number*((type==1 || type==9)?16:10)+digit;}
            const U32 required=unicode?2:4;if(!a[3])return required;if(a[3]<required)return fail(q,122);q.memory.store(a[2],number,32);return required;}
        return output(q,a[2],a[3],*value,unicode,a[1]&0x40000000?936:l->cp);});
    bind("EnumSystemLocalesA",2,[](Process& q,Args a){if(a[1]!=1 && a[1]!=2)return fail(q,1004);q.memory.check(a[0],1,Memory::Execute);
        for(const auto* text:{"00000409","00000411","00000804"}){const auto at=q.put_string(text);U32 again{};try{again=q.callback(a[0],std::array<U32,1>{at});}catch(...){q.memory.release(at);throw;}q.memory.release(at);if(!again)break;}return 1u;});
    for(bool date:{false,true})bind(date?"GetDateFormatA":"GetTimeFormatA",6,[date](Process& q,Args a){const auto* l=locale(a[0]);if(!l || a[5]>1048576)return fail(q,87);
        if(a[1]&~(date?0x80000003u:0x80000000u))throw GuestFault(FaultKind::unsupported,q.cpu.eip,"calendar flags require an explicit portable format contract");
        std::array<std::uint16_t,8> time{};if(a[2]){for(unsigned n=0;n<8;++n)time[n]=std::uint16_t(q.memory.load(a[2]+n*2,16));}
        else{const auto at=q.allocate_bytes(16);try{q.callback(q.resolve("kernel32.dll","GetLocalTime"),std::array<U32,1>{at});for(unsigned n=0;n<8;++n)time[n]=std::uint16_t(q.memory.load(at+n*2,16));}catch(...){q.memory.release(at);throw;}q.memory.release(at);}
        if(date?!(std::chrono::year(int(time[0]))/time[1]/time[3]).ok():time[4]>23 || time[5]>59 || time[6]>59)return fail(q,87);
        const auto pattern=a[3]?ascii_text(q,a[3],l->cp):date?std::u16string(a[1]&2?l->long_date:l->short_date):*info(*l,0x1003);
        try{return output(q,a[4],a[5],calendar(*l,time,pattern),false,l->cp);}catch(const EncodingError& e){throw GuestFault(FaultKind::unsupported,q.cpu.eip,e.what());}
    });
}
}
