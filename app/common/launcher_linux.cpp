#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <ctime>
#include "session_recorder.h"
namespace fs=std::filesystem;
static volatile sig_atomic_t interrupted=0;
static volatile sig_atomic_t child_pid=0;
static void Signal(int value){interrupted=value;if(child_pid>0)kill(child_pid,value);}
static std::string ChooseFolder(){
 void* lib=dlopen("libgtk-3.so.0",RTLD_NOW);if(!lib)return {};
 auto init=(int(*)(int*,char***))dlsym(lib,"gtk_init_check");
 if(!init||!init(nullptr,nullptr))return {};
 auto create=(void*(*)(const char*,void*,int,const char*,...))dlsym(lib,"gtk_file_chooser_dialog_new");
 auto run=(int(*)(void*))dlsym(lib,"gtk_dialog_run");
 auto filename=(char*(*)(void*))dlsym(lib,"gtk_file_chooser_get_filename");
 auto destroy=(void(*)(void*))dlsym(lib,"gtk_widget_destroy");
 auto release=(void(*)(void*))dlsym(lib,"g_free");
 void* dialog=create("UFC Reborn v29 — selecione a pasta com default.xex",nullptr,2,"Cancelar",-6,"Jogar",-3,nullptr);
 std::string result;if(run(dialog)==-3){char* selected=filename(dialog);if(selected){result=selected;release(selected);}}destroy(dialog);return result;
}
int main(int argc,char** argv){try{
 fs::path root=fs::canonical("/proc/self/exe").parent_path(),data=root/"data";fs::create_directories(data);
 std::string game;bool direct=false,optimized=false;
 for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--game"&&i+1<argc)game=argv[++i];else if(a=="--play")direct=true;else if(a=="--optimized")optimized=true;else if(a=="--help"){std::cout<<"UFC Launcher v29 [--game PASTA] [--play] [--optimized]\nCada execução registra FPS/CPU/RAM/GPU e erros em data/logs. Steam Game Mode: configure a pasta no desktop; depois use --play.\n";return 0;}else{std::cerr<<"Opção desconhecida: "<<a<<'\n';return 2;}}
 if(game.empty()&&direct)std::getline(std::ifstream(data/"game-path.txt"),game);
 if(game.empty())game=ChooseFolder();if(game.empty()){std::cerr<<"Escolha a pasta no desktop ou use --game /pasta/do/jogo.\n";return 2;}
 fs::path folder=fs::absolute(game);
 auto hash=ufc_record::Command({"sha256sum",(folder/"default.xex").string()});
 if(!hash.starts_with("1af0740ac854705aac209405129af6917efb03276330376b93350cfb220782e1")){std::cerr<<"default.xex ausente ou incompatível com Reborn v26.\n";return 3;}
 std::ofstream(data/"game-path.txt")<<folder.string()<<'\n';
 auto now=std::time(nullptr);std::ostringstream name;name<<"linux-v29-"<<std::put_time(std::localtime(&now),"%Y%m%d-%H%M%S")<<'-'<<getpid();
 auto run=data/"logs"/name.str();fs::create_directories(run);
 fs::path exe=data/"ufc3-v29";
 std::vector<std::string> args={exe.string(),"--gpu_plugin=xenos","--game_data_root="+folder.string(),"--log_file="+(run/"ufc3.log").string(),"--log_level=info","--ufc_show_fps=true","--mnk_mode=true",optimized?"--ufc_cp_fast_registers=true":"--ufc_cp_fast_registers=false"};
 std::ofstream manifest(run/"launch.txt");manifest<<"version=v29\nplatform=Linux x86_64\noptimization="<<optimized<<"\ngame="<<folder.string()<<"\nxex="<<hash<<"\n";
 for(auto& a:args)manifest<<a<<'\n';manifest<<ufc_record::Command({"uname","-a"});
 for(auto& f:fs::directory_iterator(data))if(f.is_regular_file()&&(f.path().filename()=="ufc3-v29"||f.path().extension()==".so"))manifest<<ufc_record::Command({"sha256sum",f.path().string()});manifest.close();
 std::string libraries=data.string();if(auto* old=getenv("LD_LIBRARY_PATH"))libraries+=std::string(":")+old;setenv("LD_LIBRARY_PATH",libraries.c_str(),1);
 chdir(data.c_str());std::vector<char*> av;for(auto& a:args)av.push_back(a.data());av.push_back(nullptr);
 posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
 posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,(run/"stdout.txt").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
 posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,(run/"stderr.txt").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
 pid_t pid=-1;int error=posix_spawn(&pid,exe.c_str(),&actions,nullptr,av.data(),environ);posix_spawn_file_actions_destroy(&actions);
 if(error)throw std::runtime_error("Falha ao iniciar jogo: "+std::to_string(error));
 child_pid=pid;signal(SIGTERM,Signal);signal(SIGINT,Signal);ufc_record::Start(pid,run);int status=0;
 while(waitpid(pid,&status,0)<0){if(errno!=EINTR)break;}
 child_pid=0;
 int code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);ufc_record::Finish(run,code);
 std::ofstream(run/"exit.txt")<<code<<'\n';std::cout<<"Relatório: "<<(run/"RELATORIO.md")<<'\n';return code;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
