#pragma once
#include <filesystem>
#include <chrono>
#include <atomic>
#include <stdexcept>
namespace ufc {
// Preserve removed cache/DLC beside the live storage on both platforms.
inline std::filesystem::path PreserveForRemoval(const std::filesystem::path& input){
 namespace fs=std::filesystem;
 const auto source=fs::absolute(input).lexically_normal();
 if(!fs::exists(fs::symlink_status(source)))return {};
 auto storage=source.parent_path();
 while(!storage.empty()&&storage.filename()!=".ufc-reborn"){
  auto parent=storage.parent_path();if(parent==storage)break;storage=parent;
 }
 if(storage.filename()!=".ufc-reborn"||source==storage)throw std::runtime_error("Arquivo fora da pasta de dados do jogo.");
 auto relative=source.lexically_relative(storage);
 if(relative.empty()||*relative.begin()==".."||*relative.begin()=="_to_delete")throw std::runtime_error("Caminho de preservacao invalido.");
 static std::atomic<unsigned long long> sequence{0};
 const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
 auto target=storage/"_to_delete"/(std::to_string(stamp)+"-"+std::to_string(sequence.fetch_add(1)))/relative;
 if(fs::exists(fs::symlink_status(target)))throw std::runtime_error("Destino de preservacao ja existe.");
 fs::create_directories(target.parent_path());
 fs::rename(source,target);
 return target;
}
}
