#include "native_application.hpp"
#include "preference_storage.hpp"
#include "progress_storage.hpp"
#include "save_storage.hpp"

namespace wa2::platform {
NativeStartup native_startup(std::span<const std::string_view> arguments){
    if(arguments.size()>32)throw std::invalid_argument("Too many native startup arguments");NativeStartup result;
    for(const auto argument:arguments){if(argument=="--test-mode"){if(result.test_mode)throw std::invalid_argument("Duplicate native test-mode argument");result.test_mode=true;}
        else throw std::invalid_argument("Unsupported native startup argument: "+std::string(argument.substr(0,128)));}
    return result;
}
std::unique_ptr<game::Application> native_application(resource::BankOpener opener,const std::filesystem::path& directory,NativeStartup startup,game::SceneServices scene,game::TitleServices title){
    if(!opener || startup.title.movie_suffix>2 || startup.image_timeout.count()<1 || startup.image_timeout.count()>60000)
        throw std::invalid_argument("Invalid native application startup configuration");
    const auto content=startup.content?*startup.content:game::installed_title_content(opener);
    auto progress=std::make_shared<progress::Store>(progress_storage(directory));
    if(startup.test_mode)progress->state().unlock_all_for_testing();
    game::Scene::SavedImageResolver resolver=[timeout=startup.image_timeout](resource::Catalog& resources)->image::Resolve{
        return [&resources,timeout](const image::Reference& reference){auto request=resources.image(reference);auto pixels=request->wait_for(timeout);
            if(!pixels)throw resource::Error("Native saved-image load timed out: "+reference.archive+"/"+reference.member);return pixels;};
    };
    auto application=std::make_unique<game::Application>(opener,progress,preference_storage(directory),save_storage(directory),std::move(resolver),startup.seed,std::move(scene));
    application->prepare_title(content,startup.title,std::move(title));
    auto menu_resources=std::make_shared<resource::Catalog>(opener);
    application->prepare_options([menu_resources](std::string_view name){return menu_resources->image("grp",name);},{},
        [owner=application.get()](std::string_view prompt){
            text::TextStyle style;style.size=28;style.columns=15;style.rows=2;style.line_gap=0;return owner->menu_text(prompt,style);});
    // A failed title/provider preparation must not persist test-mode changes.
    if(startup.test_mode)progress->flush();return application;
}
}
