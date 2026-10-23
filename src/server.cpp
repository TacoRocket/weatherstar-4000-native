#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <spawn.h>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
using json=nlohmann::json;
static std::atomic<bool> stopping{false};
static void signal_stop(int){stopping.store(true);}
static std::string read_file(const fs::path& p){
 std::ifstream in(p,std::ios::binary);
 if(!in) throw std::runtime_error("Cannot read "+p.string());
 return std::string(std::istreambuf_iterator<char>(in),{});
}
static pid_t spawn_process(std::vector<std::string> args,int input,int output,int other){
 std::vector<char*> argv; for(auto& s:args) argv.push_back(s.data()); argv.push_back(nullptr);
 posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
 if(input>=0) posix_spawn_file_actions_adddup2(&actions,input,STDIN_FILENO);
 if(output>=0) posix_spawn_file_actions_adddup2(&actions,output,STDOUT_FILENO);
 if(input>2) posix_spawn_file_actions_addclose(&actions,input);
 if(output>2) posix_spawn_file_actions_addclose(&actions,output);
 if(other>2 && other!=input && other!=output) posix_spawn_file_actions_addclose(&actions,other);
 pid_t pid=-1; int rc=posix_spawnp(&pid,argv[0],&actions,nullptr,argv.data(),environ);
 posix_spawn_file_actions_destroy(&actions);
 if(rc) throw std::runtime_error("Cannot spawn "+args[0]+": "+std::to_string(rc));
 return pid;
}
static void terminate(pid_t& pid){
 if(pid<=0) return;
 kill(pid,SIGTERM);
 for(int i=0;i<30;++i){
  if(waitpid(pid,nullptr,WNOHANG)==pid){pid=-1;return;}
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
 }
 kill(pid,SIGKILL); waitpid(pid,nullptr,0); pid=-1;
}
struct Producer {
 std::mutex mutex;
 pid_t renderer=-1,encoder=-1;
 Clock::time_point last_request=Clock::now();
 unsigned starts=0;
 std::string last_error;
 int idle_seconds=120;
 bool vaapi=false;
 std::string vaapi_device="/dev/dri/renderD128";
 bool active_locked(){return renderer>0 && encoder>0;}
 void stop_locked(){terminate(encoder);terminate(renderer);}
 bool ensure(){
  std::lock_guard<std::mutex> lock(mutex);
  last_request=Clock::now();
  if(active_locked()) return true;
  try {
   fs::create_directories("/stream/hls");
   // Only our disposable HLS output is retired between producer generations.
   for(const auto& entry:fs::directory_iterator("/stream/hls")) {
    auto n=entry.path().filename().string();
    if(entry.is_regular_file() && n.rfind("stream",0)==0 &&
       (entry.path().extension()==".ts" || entry.path().extension()==".m3u8" || entry.path().extension()==".tmp")) fs::remove(entry.path());
   }
   const char* music_setting=getenv("WEATHERSTAR_MUSIC_FILE");
   std::string music=music_setting?music_setting:"";
   if(!music.empty() && !fs::is_regular_file(music)) throw std::runtime_error("WEATHERSTAR_MUSIC_FILE is not an existing file: "+music);
   int pipes[2]; if(pipe2(pipes,O_CLOEXEC)!=0) throw std::runtime_error("raw-frame pipe failed");
   try {
    renderer=spawn_process({"weather-render","--snapshot","/data/snapshot.json","--assets","/app/weatherstar_assets"},-1,pipes[1],pipes[0]);
    std::vector<std::string> encode_args={"ffmpeg","-nostdin","-hide_banner","-loglevel","warning","-y","-threads","2"};
    if(vaapi) encode_args.insert(encode_args.end(),{"-vaapi_device",vaapi_device});
    encode_args.insert(encode_args.end(),{"-f","rawvideo","-pixel_format","rgb24","-video_size","640x480","-framerate","30","-i","pipe:0"});
    if(music.empty()) encode_args.insert(encode_args.end(),{"-f","lavfi","-i","anullsrc=channel_layout=stereo:sample_rate=48000"});
    else encode_args.insert(encode_args.end(),{"-stream_loop","-1","-i",music});
    encode_args.insert(encode_args.end(),{"-map","0:v","-map","1:a"});
    if(vaapi) {
      // VAAPI's level option takes level_idc: 30 means H.264 level 3.0.
      encode_args.insert(encode_args.end(),{"-c:v","h264_vaapi","-low_power","1","-vf","format=nv12,hwupload",
        "-profile:v","constrained_baseline","-level:v","30","-bf","0","-coder:v","cavlc"});
    } else {
      encode_args.insert(encode_args.end(),{"-c:v","libx264","-threads","2","-preset","ultrafast","-pix_fmt","yuv420p","-sc_threshold","0"});
    }
    encode_args.insert(encode_args.end(),{
      "-b:v","1000k","-r","30","-g","60","-af","volume=0.3","-c:a","aac","-b:a","128k",
      "-f","hls","-hls_time","2","-hls_list_size","10","-hls_delete_threshold","5","-hls_start_number_source","epoch",
      "-hls_flags","delete_segments+temp_file+independent_segments","/stream/hls/stream.m3u8"});
    encoder=spawn_process(encode_args,pipes[0],-1,pipes[1]);
   } catch(...) {close(pipes[0]);close(pipes[1]);stop_locked();throw;}
   close(pipes[0]);close(pipes[1]);++starts;last_error.clear();
   std::cerr<<"producer started generation="<<starts<<"\n";
   return true;
  } catch(const std::exception& e){last_error=e.what();std::cerr<<last_error<<"\n";return false;}
 }
 void touch(){std::lock_guard<std::mutex> lock(mutex);last_request=Clock::now();}
 void check(){
  std::lock_guard<std::mutex> lock(mutex);
  for(auto child:{renderer,encoder}) if(child>0){
   int result=0;
   if(waitpid(child,&result,WNOHANG)==child){
    if(child==renderer) renderer=-1; else encoder=-1;
    last_error="producer exited status="+std::to_string(result);
    std::cerr<<last_error<<"\n";stop_locked();return;
   }
  }
  if(active_locked() && std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-last_request).count()>=idle_seconds){
   std::cerr<<"producer idle; stopping render/encode\n";stop_locked();
  }
 }
 json status(){
  std::lock_guard<std::mutex> lock(mutex);
  return {{"streaming",active_locked()},{"renderer_pid",renderer},{"encoder_pid",encoder},{"encoder",vaapi?"vaapi-low-power":"software"},{"starts",starts},{"idle_timeout_seconds",idle_seconds},{"last_error",last_error},{"snapshot_available",fs::exists("/data/snapshot.json")}};
 }
 ~Producer(){std::lock_guard<std::mutex> lock(mutex);stop_locked();}
};
int main(){
 signal(SIGTERM,signal_stop);signal(SIGINT,signal_stop);signal(SIGPIPE,SIG_IGN);
 Producer producer;
 if(const char* mode=getenv("WEATHERSTAR_ENCODER")) {
  if(std::string(mode)=="vaapi-low-power") producer.vaapi=true;
  else if(std::string(mode)!="software") {std::cerr<<"WEATHERSTAR_ENCODER must be software or vaapi-low-power\n";return 1;}
 }
 if(const char* device=getenv("WEATHERSTAR_VAAPI_DEVICE")) producer.vaapi_device=device;
 if(producer.vaapi && access(producer.vaapi_device.c_str(),R_OK|W_OK)!=0) {
  std::cerr<<"VAAPI render device is not readable/writable: "<<producer.vaapi_device<<"\n";return 1;
 }
 if(const char* idle=getenv("WEATHERSTAR_IDLE_SECONDS")) producer.idle_seconds=std::max(5,std::stoi(idle));
 httplib::Server server;
 server.new_task_queue=[] {return new httplib::ThreadPool(6);};
 server.Get("/health",[&](const auto&,auto& res){res.set_content(producer.status().dump(),"application/json");});
 server.Get("/weather.m3u",[](const auto&,auto& res){
  const char* public_url=getenv("WEATHERSTAR_PUBLIC_URL");
  std::string url=public_url?public_url:"http://localhost:8080";
  res.set_content("#EXTM3U\n#EXTINF:-1 tvg-id=\"weatherstar-native\" group-title=\"Weather\",WeatherStar 4000 Native\n"+url+"/hls/stream.m3u8\n","application/vnd.apple.mpegurl");
 });
 server.Get(R"(/hls/(stream\.m3u8|stream[0-9]+\.ts))",[&](const httplib::Request& req,httplib::Response& res){
  // FFmpeg can reuse an HLS connection after the server's keep-alive timeout.
  res.set_header("Connection","close");
  std::string name=req.matches[1];
  bool playlist=name=="stream.m3u8";
  if(playlist && !producer.ensure()){res.status=503;res.set_content("producer failed; retry","text/plain");return;}
  producer.touch();
  fs::path file=fs::path("/stream/hls")/name;
  if(playlist){
   for(int i=0;i<200 && !fs::exists(file) && !stopping.load();++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if(!fs::exists(file)){res.status=playlist?503:404;res.set_header("Retry-After","2");return;}
  try {res.set_content(read_file(file),playlist?"application/vnd.apple.mpegurl":"video/mp2t");res.set_header("Cache-Control","no-store");}
  catch(const std::exception& e){std::cerr<<e.what()<<"\n";res.status=503;}
 });
 std::thread monitor([&]{while(!stopping.load()){producer.check();std::this_thread::sleep_for(std::chrono::seconds(1));}server.stop();});
 bool ok=server.listen("0.0.0.0",8080);
 stopping=true;monitor.join();
 if(!ok){std::cerr<<"HTTP listener failed\n";return 1;}
 return 0;
}
