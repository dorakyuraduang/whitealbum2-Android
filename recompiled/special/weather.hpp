#pragma once
#include <string>
namespace wr {struct Cpu;class Memory;}
namespace wa2::special {bool update_weather(wr::Cpu&,wr::Memory&);std::string weather_report();}
