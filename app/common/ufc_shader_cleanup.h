#pragma once
#include "ufc_library.h"
#ifndef _WIN32
#include <sys/file.h>
#endif
namespace ufc {
// This is the sole permanent-removal operation requested by the user. Restrict it
// to shader subdirectories; user saves, DLC, metadata and other caches are excluded.
inline bool ShaderPathIsLink(const fs::path& p){
#ifdef _WIN32
 const DWORD attrs=GetFileAttributesW(p.c_str());
 return attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT);
#else
 return fs::is_symlink(fs::symlink_status(p));
#endif
}
inline void ValidateShaderPath(const fs::path& absolute){
 if(!absolute.is_absolute())throw std::runtime_error("Caminho de shaders deve ser absoluto.");
 fs::path walk;
 for(const auto& part:absolute){walk/=part;if(ShaderPathIsLink(walk))throw std::runtime_error("Limpeza cancelada: link ou juncao no caminho de shaders.");}
}
inline uintmax_t ClearShaderCachePermanently(const fs::path& game){
 const auto storage=fs::absolute(Storage(game)).lexically_normal();
 ValidateShaderPath(storage);
 if(!fs::is_directory(storage))return 0;
 // Same cross-process lock used by Session, including Steam/direct launcher use.
#ifdef _WIN32
 HANDLE lock=CreateFileW((storage/"game.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Feche o jogo antes de limpar os shaders.");
 struct Lock{HANDLE h;~Lock(){CloseHandle(h);}} scoped{lock};
#else
 int lock=open((storage/"game.lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
 if(lock<0)throw std::runtime_error("Nao foi possivel verificar a sessao do jogo.");
 struct Lock{int fd;~Lock(){close(fd);}} scoped{lock};
 if(flock(lock,LOCK_EX|LOCK_NB))throw std::runtime_error("Feche o jogo antes de limpar os shaders.");
#endif
 std::vector<fs::path> targets;
 for(const auto& relative:{fs::path("cache/windows-vulkan/shaders"),fs::path("cache/linux-vulkan/shaders"),fs::path("cache/shaders")}){
  const auto target=(storage/relative).lexically_normal();
  if(target.lexically_relative(storage)!=relative||target.filename()!="shaders")throw std::runtime_error("Alvo de shaders invalido.");
  ValidateShaderPath(target);
  if(!fs::exists(target))continue;
  if(!fs::is_directory(target))throw std::runtime_error("Alvo de shaders nao e uma pasta.");
  for(const auto& entry:fs::recursive_directory_iterator(target))if(ShaderPathIsLink(entry.path()))throw std::runtime_error("Limpeza cancelada: link ou juncao dentro dos shaders.");
  targets.push_back(target);
 }
 uintmax_t removed=0;
 for(const auto& target:targets){ValidateShaderPath(target);removed+=fs::remove_all(target);}
 return removed;
}
}
