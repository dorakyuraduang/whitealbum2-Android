#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace wa2::game {
enum class UserCommand : std::uint8_t {settings,save,load,quick_save,quick_load,backlog,hide,automatic,skip,fullscreen,title,quit,movie_high,movie_low,window_hd,window_medium,window_small,about,environment,count};
static_assert(unsigned(UserCommand::count)<32);
inline constexpr std::array<std::string_view,unsigned(UserCommand::count)> user_command_labels{
    "游戏设置","保存进度","读取进度","快速保存","快速读取","对话记录","隐藏对话框","自动播放","快进","全屏显示","返回标题","退出游戏","视频高画质","视频低画质","窗口 1280×720","窗口 1024×576","窗口 800×450","版本信息","运行环境"
};
inline constexpr bool valid(UserCommand value){return unsigned(value)<unsigned(UserCommand::count);}
inline constexpr std::uint32_t command_bit(UserCommand value){return valid(value)?1u<<unsigned(value):0;}
struct UserMenuState {
    std::uint32_t enabled{},checked{};
    std::uint64_t packed()const noexcept{return enabled|(std::uint64_t(checked)<<32);}
};
inline constexpr std::size_t maximum_user_commands=16;
}
