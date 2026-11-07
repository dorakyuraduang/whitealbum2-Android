// Parse original resource records only. No script is executed, scene created,
// game started, image decoded, device opened or source executable inspected.
#include "script_runtime.hpp"
#include "file_io.hpp"
#include "resource_catalog.hpp"
#include "sprite_animation.hpp"
#include "image_colours.hpp"
#include "native_title_atlases.hpp"
#include "native_options_atlases.hpp"
#include "native_save_atlases.hpp"
#include "scene_calendar.hpp"
#include "text_layout.hpp"
#include "font_glyphs.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace text_metadata {
#include "dialogue_constants.inc"
}

// A bounded walk over literal records only. It never starts Runtime/Session,
// reads variables, follows branches or invokes a script command.
std::optional<std::int32_t> integer_literal(const wa2::script::Program& program,std::uint32_t& cursor,unsigned depth=0){
    if(depth>8)return {};std::optional<std::uint32_t> previous;
    for(unsigned bytes=4;bytes<=12 && bytes<=cursor;bytes+=4)if(program.instruction_offset(cursor-bytes)){previous=cursor-bytes;break;}
    if(!previous)return {};cursor=*previous;const auto kind=program.word(cursor),type=program.word(cursor+4);
    if(kind==5 && type>=1 && type<=3)return std::bit_cast<std::int32_t>(program.word(cursor+8));
    if(kind==6 && type==23){if(auto value=integer_literal(program,cursor,depth+1))return std::bit_cast<std::int32_t>(0u-std::uint32_t(*value));}return {};
}
std::optional<wa2::script::Value> value_literal(const wa2::script::Program& program,const wa2::script::Functions& functions,std::uint32_t& cursor,unsigned depth=0){
    if(depth>64){cursor=0;return {};}
    const auto end=cursor;std::optional<std::uint32_t> previous;
    for(unsigned bytes=4;bytes<=12 && bytes<=cursor;bytes+=4)if(program.instruction_offset(cursor-bytes)){previous=cursor-bytes;break;}
    if(!previous){cursor=0;return {};}const auto at=*previous,kind=program.word(at),value=program.word(at+4);
    if(kind==3){if(value>=program.strings().size())throw std::runtime_error("Invalid literal text index");cursor=at;return wa2::script::Value(program.strings()[value]);}
    if(kind==5 && value==5 && program.word(at+8)==0xffffffffu){cursor=at;return wa2::script::Value::default_text();}
    if(kind==5 && value==4){cursor=at;return wa2::script::Value(std::bit_cast<float>(program.word(at+8)));}
    // Unknown variable/expression values still have a known record shape.
    // Consume that shape, so a later integer flag cannot conceal the earlier
    // literal text argument. No variable is read or command executed.
    if(kind==1 || kind==2){cursor=at;return {};}
    if(kind==4){cursor=at;for(std::size_t n=functions.at(value).parameters.size();n>0;--n)value_literal(program,functions,cursor,depth+1);return {};}
    if(kind==6 && value!=23){
        cursor=at;
        if(value==28 || value==29)return value_literal(program,functions,cursor,depth+1);
        const auto operands=value<=22 || value==27?2u:value>=24 && value<=26?1u:0u;
        if(!operands){cursor=0;return {};}for(unsigned n=0;n<operands;++n)value_literal(program,functions,cursor,depth+1);return {};
    }
    cursor=end;if(const auto integer=integer_literal(program,cursor))return wa2::script::Value(*integer);cursor=0;return {};
}

int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("usage: native_script_catalogue resource-directory report.json");
    const auto path=std::filesystem::path(argv[1])/"ck-gal.pak";
    auto archive=wa2::resource::Archive::open(wa2::platform::open_resource_file(path));wa2::script::Library library(archive);
    unsigned scenes{},labels{},calls{},calendar_calls{},calendar_literals{},calendar_unknown{},calendar_201{},text_calls{},text_literals{},text_unknown{};std::uint64_t text_letters{};std::set<std::uint32_t> commands;std::set<std::pair<std::uint16_t,bool>> glyphs;
    for(const auto& member:archive.members()){
        if(!member.name.ends_with(".bnr"))continue;
        const auto name=member.name.substr(0,member.name.size()-4);const auto program=library.load(name);++scenes;
        labels+=unsigned(program->labels().size());
        for(const auto& label:program->labels())if(!program->instruction_offset(label.offset))throw std::runtime_error("Original script label boundary mismatch");
        for(const auto& call:program->commands()){
            library.functions().at(call.index);commands.insert(call.index);++calls;
            if(!program->instruction_offset(call.offset))throw std::runtime_error("Original script command boundary mismatch");
            if(call.index==203){
                ++calendar_calls;auto cursor=call.offset;std::array<std::int32_t,5> args{};bool known=true;
                for(unsigned n=5;n>0;--n){const auto value=integer_literal(*program,cursor);if(!value){known=false;break;}args[n-1]=*value;}
                if(!known){++calendar_unknown;continue;}++calendar_literals;
                const auto date=wa2::script::SceneCalendar::date(args[0],args[1],args[2],args[3]);
                if(args[4] && !wa2::script::SceneCalendar::supports_display(date))throw std::runtime_error("Unsupported literal calendar display in "+name+" at record "+std::to_string(call.offset));
                if(date.year==201)++calendar_201;
            }
            if(call.index==130 || call.index==131 || call.index==133 || call.index==208){
                ++text_calls;const auto& function=library.functions().at(call.index);auto cursor=call.offset;std::optional<wa2::script::Value> raw;
                for(std::size_t n=function.parameters.size();n>0;--n){auto value=value_literal(*program,library.functions(),cursor);if(n==1)raw=std::move(value);}
                if(!raw){++text_unknown;continue;}++text_literals;
                const auto& bytes=raw->uses_default_text()?function.default_text.at(0):raw->text();
                // Validate encoding/control syntax without pretending a
                // static walk knows the active normal/novel window geometry.
                wa2::text::TextStyle syntax;syntax.columns=syntax.rows=127;
                try{const auto layout=wa2::text::layout(bytes,syntax);text_letters+=layout.letters.size();for(const auto& letter:layout.letters)glyphs.emplace(letter.code,letter.small);}catch(const std::exception& error){throw std::runtime_error("Original literal dialogue in "+name+" at record "+std::to_string(call.offset)+": "+error.what());}
            }
        }
    }
    if(!scenes || !calls)throw std::runtime_error("Missing original script catalogue");
    wa2::resource::Catalog resources([&](std::string_view name)->std::shared_ptr<wa2::resource::Source>{
        const auto file=std::filesystem::path(argv[1])/std::filesystem::path(name);
        if(!std::filesystem::exists(file))return {};return wa2::platform::open_resource_file(file);
    });
    unsigned menu_atlases{};
    const auto menu_atlas=[&](const auto& atlas){
        const auto header=wa2::image::inspect_game_tga(resources.data("grp",atlas.name));
        if(header.width<atlas.w || header.height<atlas.h)throw std::runtime_error("Original menu crop exceeds image dimensions: "+std::string(atlas.name)+" expected "+std::to_string(atlas.w)+"x"+std::to_string(atlas.h)+" actual "+std::to_string(header.width)+"x"+std::to_string(header.height));++menu_atlases;
    };
    for(const auto& atlas:wa2::test::title_atlases)menu_atlas(atlas);
    for(const auto& atlas:wa2::test::options_atlases)menu_atlas(atlas);
    for(const auto& atlas:wa2::test::save_atlases)menu_atlas(atlas);
    struct MenuAtlas {const char* name;unsigned w,h;};
    constexpr MenuAtlas extras[]{
        {"sys_04000.tga",1280,40},{"sys_04100.tga",400,128},{"sys_04101.tga",344,1512},{"sys_04021.tga",288,40},{"sys_04030.tga",1024,56},{"sys_04031.tga",512,1512},
        {"sys_05000.tga",1280,36},{"sys_06000.tga",1280,36},{"sys_05100.tga",272,160},{"sys_05110.tga",272,160},{"sys_05020.tga",144,128},{"sys_05201.tga",840,96},{"sys_05021.tga",288,40},{"sys_06021.tga",288,40},{"sys_05900.tga",272,160},{"sys_05901.tga",272,160},{"sys_05902.tga",272,160},{"sys_05200.tga",1000,24},
        {"sys_07000.tga",1280,40},{"sys_07100.tga",1280,720},{"sys_07020.tga",160,120},{"sys_07021.tga",288,40},
        {"sys_07200.tga",1280,720},{"sys_07201.tga",1280,720},{"sys_07202.tga",1280,720},{"sys_07203.tga",1280,720},{"sys_07204.tga",1280,720}
    };
    for(const auto& atlas:extras)menu_atlas(atlas);
    unsigned font_headers{},glyph_crops{},fallback_glyphs{};std::set<std::uint16_t> fallback_codes;const auto& fonts=resources.required("fon");
    for(unsigned index=0;index<text_metadata::font_names.size();++index){
        const auto member=fonts.find_first(text_metadata::font_names[index]);if(member==wa2::resource::Archive::missing)throw std::runtime_error("Missing original dialogue font sheet");
        const auto bytes=fonts.load(member,16u*1024*1024);const auto header=wa2::image::inspect_game_tga(bytes);const auto cell=index<2?40u:24u;
        if(header.bits!=8 || header.width!=80*cell || header.height<cell*(wa2::text::glyph_count(true)/80+1))throw std::runtime_error("Original dialogue font sheet dimensions do not match native glyph cells");++font_headers;
        for(const auto [code,small]:glyphs)if(small==(index>=2)){
            const auto box=wa2::text::glyph_box(code,true,cell,0,cell);if(box.x+box.width>header.width || box.y+box.height>header.height)throw std::runtime_error("Original dialogue glyph crop exceeds font sheet");++glyph_crops;
            if(wa2::text::glyph_index(code,true)==wa2::text::glyph_count(true)){++fallback_glyphs;fallback_codes.insert(code);}
        }
    }
    unsigned calendar_atlases{};
    for(unsigned n=0;n<4;++n){
        constexpr std::string_view names[]{"calender_00.tga","calender_01.tga","calender_10.tga","calender_11.tga"};
        const auto bytes=resources.data("grp",names[n]);const auto header=wa2::image::inspect_game_tga(bytes);
        const unsigned width=n<2?1280:n==2?472:728,height=n<2?720:n==2?48:32;
        std::cout<<names[n]<<' '<<header.width<<'x'<<header.height<<'\n';
        if(header.width<width || header.height<height)throw std::runtime_error("Original calendar atlas crop exceeds image dimensions: "+std::string(names[n]));++calendar_atlases;
    }
    unsigned animations{},animation_frames{},colour_maps{},selection_atlases{};
    for(unsigned n=0;n<2;++n){const auto bytes=resources.data("grp",n?"sys_00011.tga":"sys_00200.tga");const auto header=wa2::image::inspect_game_tga(bytes);
        if(header.width<(n?248u:784u) || header.height<(n?104u:216u))throw std::runtime_error("Original selection atlas crop exceeds image dimensions");++selection_atlases;}
    for(const auto name:{"nega.amp","sepia.amp"}){const auto bytes=resources.data("grp",name);wa2::image::parse_colour_map(bytes);++colour_maps;}
    for(unsigned n=0;n<3;++n){constexpr std::string_view names[]{"sakura1_2.tga","sakura2.tga","sakura3.tga"};constexpr unsigned cell[]{128,32,16};
        const auto header=wa2::image::inspect_game_tga(resources.data("grp",names[n]));if(header.width<12*cell[n] || header.height<2*cell[n])throw std::runtime_error("Original sakura atlas crop exceeds image dimensions");}
    for(bool introductory:{false,true})if(const auto* bank=resources.optional("grp",introductory))for(const auto& member:bank->members())if(member.name.ends_with(".ani")){
        const auto data=wa2::image::Animation::parse(bank->load(bank->find_first(member.name)));++animations;animation_frames+=unsigned(data->frames().size());
        std::vector<wa2::image::TgaLayout> headers;for(const auto& texture:data->textures())headers.push_back(wa2::image::inspect_game_tga(resources.data("grp",texture)));
        for(const auto& frame:data->frames())for(const auto& piece:frame.pieces){const auto& header=headers.at(piece.texture);
            if(unsigned(piece.source_x)+unsigned(piece.width)>header.width || unsigned(piece.source_y)+unsigned(piece.height)>header.height)throw std::runtime_error("Original animation source crop exceeds texture dimensions");}
    }
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-script-catalogue.v1\",\"scenes\":"<<scenes<<",\"labels\":"<<labels<<",\"command_sites\":"<<calls<<",\"command_indices\":[";
    bool first=true;for(auto id:commands){if(!first)report<<',';first=false;report<<id;}
    report<<"],\"menu_atlas_headers\":"<<menu_atlases<<",\"calendar_literal_calls\":"<<calendar_calls<<",\"calendar_literals_validated\":"<<calendar_literals<<",\"calendar_unknown_arguments\":"<<calendar_unknown<<",\"calendar_literal_year_201\":"<<calendar_201;
    report<<",\"dialogue_text_calls\":"<<text_calls<<",\"dialogue_literal_texts\":"<<text_literals<<",\"dialogue_unknown_arguments\":"<<text_unknown<<",\"dialogue_parsed_letters\":"<<text_letters;
    report<<",\"dialogue_font_headers\":"<<font_headers<<",\"dialogue_unique_glyphs\":"<<glyphs.size()<<",\"dialogue_glyph_crops\":"<<glyph_crops<<",\"dialogue_fallback_glyph_crops\":"<<fallback_glyphs<<",\"dialogue_geometry_verified\":false";
    report<<",\"dialogue_fallback_codes\":[";first=true;for(auto code:fallback_codes){if(!first)report<<',';first=false;report<<code;}report<<']';
    report<<",\"calendar_atlas_headers\":"<<calendar_atlases<<",\"selection_atlas_headers\":"<<selection_atlases<<",\"sprite_animations\":"<<animations<<",\"animation_frames\":"<<animation_frames<<",\"colour_maps_parsed\":"<<colour_maps<<",\"animation_sequences_executed\":false,\"resource_metadata_and_script_data_only\":true,\"script_executed\":false,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"whole_game_verified\":false,\"passed\":true}\n";
    report.close();if(!report)throw std::runtime_error("Cannot write original script structure report");
    std::cout<<"PASS static resource script structure: "<<scenes<<" scripts, "<<labels<<" labels, "<<calls<<" command sites; no gameplay executed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
