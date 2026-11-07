#include "service_lifecycle.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace a=wa2::android;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
struct Environment {
    std::array<bool,4> owners{};std::array<int,32> events{};unsigned count{};int failure{-1};
    void note(int event)noexcept{if(count<events.size())events[count++]=event;}
};
template<unsigned N>bool initialize(Environment* env)noexcept{env->note(int(N));env->owners[N]=true;return env->failure!=int(N);}
template<unsigned N>void release(Environment* env)noexcept{env->note(10+int(N));env->owners[N]=false;}
constexpr std::array<a::JniService<Environment>,4> definitions{{{initialize<0>,release<0>},{initialize<1>,release<1>},{initialize<2>,release<2>},{initialize<3>,release<3>}}};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};
    for(const unsigned count:{2,4}){
        const auto services=std::span(definitions).first(count);Environment env;
        CHECK(a::initialize_services<Environment>(&env,services) && env.count==count);
        for(unsigned n=0;n<count;++n)CHECK(env.owners[n] && env.events[n]==int(n));
        a::release_services<Environment>(&env,services);CHECK(env.count==2*count);
        for(unsigned n=0;n<count;++n)CHECK(!env.owners[n] && env.events[count+n]==int(10+count-1-n));++cases;
        for(unsigned failed=0;failed<count;++failed){
            env={};env.failure=int(failed);CHECK(!a::initialize_services<Environment>(&env,services));
            CHECK(env.count==2*(failed+1));for(bool owner:env.owners)CHECK(!owner);
            for(unsigned n=0;n<=failed;++n){CHECK(env.events[n]==int(n));CHECK(env.events[failed+1+n]==int(10+failed-n));}++cases;
            env={};CHECK(a::initialize_services<Environment>(&env,services));a::release_services<Environment>(&env,services);for(bool owner:env.owners)CHECK(!owner);++cases;
        }
    }
    Environment env;CHECK(!a::initialize_services<Environment>(nullptr,definitions) && !env.count);++guards;++cases;
    for(unsigned missing=0;missing<2;++missing){auto invalid=definitions;if(missing)invalid[3].release=nullptr;else invalid[3].initialize=nullptr;CHECK(!a::initialize_services<Environment>(&env,invalid) && !env.count);++guards;++cases;}
    std::array<a::JniService<Environment>,9> excessive;excessive.fill(definitions[0]);CHECK(!a::initialize_services<Environment>(&env,excessive) && !env.count);++guards;++cases;
    CHECK(a::initialize_services<Environment>(&env,{}) && !env.count);a::release_services<Environment>(&env,{});CHECK(!env.count);++cases;
    a::release_services<Environment>(nullptr,definitions);CHECK(!env.count);++guards;++cases;
    auto invalid=definitions;invalid[3].release=nullptr;env.owners={true,true,true,false};a::release_services<Environment>(&env,invalid);CHECK(env.count==3 && env.events[0]==12 && env.events[1]==11 && env.events[2]==10);for(bool owner:env.owners)CHECK(!owner);++guards;++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.android-jni-services.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"native_and_retained_service_sets\":true,\"partial_registration_unwound\":true,\"reverse_release_order\":true,\"retry_after_failure\":true,\"fake_services_only\":true,\"java_vm_created\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native and retained JNI service registration, partial failure cleanup and retry.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
