// One-file independent multi-worker engine. Workers run this same executable;
// no teacher engine or external network is involved.
#define V4_MULTI_WORKER
#define main v4_single_worker_main
#include "v4_parallel_core.inc"
#undef main
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>
#include <future>
#include <climits>
#include <map>

struct WorkerAnswer {std::string move;int depth=0,score=0;uint64_t nodes=0;std::map<int,std::pair<std::string,int>> iterations;};
class V4Worker {
    HANDLE input=nullptr,output=nullptr,process=nullptr;
    std::thread reader;
    std::mutex mutex;std::condition_variable changed;
    std::deque<std::string> lines;bool ended=false;
public:
    explicit V4Worker(int id){
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
        HANDLE childInput=nullptr,childOutput=nullptr;
        if(!CreatePipe(&childInput,&input,&attributes,0)||!CreatePipe(&output,&childOutput,&attributes,0))throw std::runtime_error("Worker pipe failed");
        SetHandleInformation(input,HANDLE_FLAG_INHERIT,0);SetHandleInformation(output,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        startup.hStdInput=childInput;startup.hStdOutput=childOutput;startup.hStdError=childOutput;
        wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);
        std::wstring command=L"\""+std::wstring(exe)+L"\" --worker "+std::to_wstring(id);
        PROCESS_INFORMATION info{};
        const bool ok=CreateProcessW(exe,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info);
        CloseHandle(childInput);CloseHandle(childOutput);
        if(!ok)throw std::runtime_error("Cannot start own V4 worker");
        process=info.hProcess;CloseHandle(info.hThread);
        reader=std::thread([this]{
            char buffer[4096];DWORD count;std::string pending;
            while(ReadFile(output,buffer,sizeof(buffer),&count,nullptr)&&count){
                pending.append(buffer,count);size_t newline;
                while((newline=pending.find('\n'))!=std::string::npos){
                    std::string line=pending.substr(0,newline);pending.erase(0,newline+1);
                    if(!line.empty()&&line.back()=='\r')line.pop_back();
                    {std::lock_guard<std::mutex> lock(mutex);lines.push_back(std::move(line));}changed.notify_one();
                }
            }
            {std::lock_guard<std::mutex> lock(mutex);ended=true;}changed.notify_one();
        });
        request("uci\nisready","readyok");
    }
    void send(const std::string& command){
        const auto text=command+'\n';DWORD sent;
        if(!WriteFile(input,text.data(),DWORD(text.size()),&sent,nullptr)||sent!=text.size())throw std::runtime_error("Worker input failed");
    }
    std::vector<std::string> request(const std::string& command,const std::string& end){
        {std::lock_guard<std::mutex> lock(mutex);lines.clear();}
        send(command);std::vector<std::string> answer;
        for(;;){
            std::unique_lock<std::mutex> lock(mutex);
            if(!changed.wait_for(lock,std::chrono::seconds(35),[&]{return !lines.empty()||ended;}))throw std::runtime_error("Worker timeout");
            if(lines.empty()&&ended)throw std::runtime_error("Worker exited");
            auto line=std::move(lines.front());lines.pop_front();lock.unlock();answer.push_back(line);
            if(line==end || line.rfind(end+' ',0)==0)return answer;
        }
    }
    WorkerAnswer go(const std::string& command){
        WorkerAnswer a;
        for(const auto& line:request(command,"bestmove")){
            std::istringstream stream(line);std::string word;stream>>word;
            if(word=="bestmove")stream>>a.move;
            else if(word=="info"){std::string pv;while(stream>>word){
                if(word=="depth")stream>>a.depth;
                else if(word=="score"){std::string value;stream>>value;if(value=="cp"||value=="mate")stream>>value;try{a.score=std::stoi(value);}catch(...) {}}
                else if(word=="nodes")stream>>a.nodes;
                else if(word=="pv")stream>>pv;
            }if(!pv.empty())a.iterations[a.depth]={pv,a.score};}
        }
        return a;
    }
    ~V4Worker(){
        if(input){try{send("quit");}catch(...){}CloseHandle(input);input=nullptr;}
        if(process){if(WaitForSingleObject(process,1000)==WAIT_TIMEOUT)TerminateProcess(process,0);CloseHandle(process);}
        if(reader.joinable())reader.join();if(output)CloseHandle(output);
    }
};

int main(int argc,char** argv){
    if(argc==3 && std::string(argv[1])=="--worker"){
        v4WorkerId=std::atoi(argv[2]);return v4_single_worker_main(argc,argv);
    }
    try{
        constexpr int Workers=3;
        std::vector<std::unique_ptr<V4Worker>> workers;
        for(int i=0;i<Workers;++i)workers.emplace_back(std::make_unique<V4Worker>(i));
        std::string line;
        while(std::getline(std::cin,line)){
            std::istringstream stream(line);std::string command;stream>>command;
            if(command=="quit")break;
            if(command=="uci"){
                std::cout<<"id name CatChess-V4-pro\nid author GeoLite / CatChess\ninfo string independent geometry ensemble, three own search workers\nuciok"<<std::endl;
            }else if(command=="isready"){
                for(auto& w:workers)w->request("isready","readyok");std::cout<<"readyok"<<std::endl;
            }else if(command=="go"){
                auto before=std::chrono::steady_clock::now();
                std::istringstream limits(line);std::ostringstream normalized;std::string field;
                while(limits>>field){normalized<<field<<' ';if(field=="nodes"){long long n;limits>>n;normalized<<std::max(1LL,n/Workers)<<' ';}}
                const std::string workerCommand=normalized.str();
                std::vector<std::future<WorkerAnswer>> tasks;
                for(auto& w:workers)tasks.emplace_back(std::async(std::launch::async,[worker=w.get(),workerCommand]{return worker->go(workerCommand);}));
                std::vector<WorkerAnswer> a;for(auto& t:tasks)a.push_back(t.get());
                int choice=0;long long best=LLONG_MIN;uint64_t nodes=0;
                for(const auto& x:a)nodes+=x.nodes;
                for(int i=0;i<Workers;++i){
                    long long vote=0;
                    for(int j=0;j<Workers;++j)if(a[j].move==a[i].move)vote+=100+12*a[j].depth;
                    vote+=std::clamp(a[i].score,-1200,1200)/12;
                    if(abs(a[i].score)>29000)vote=a[i].score>0?1000000+a[i].score:-1000000+a[i].score;
                    if(vote>best){best=vote;choice=i;}
                }
#ifdef V4_SPLIT_ROOT
                int common=MAXPLY;
                for(const auto& x:a)if(!x.iterations.empty())common=std::min(common,x.iterations.rbegin()->first);
                int selectedScore=-MATE;std::string selectedMove;
                if(common<MAXPLY)for(int i=0;i<Workers;++i){
                    const auto it=a[i].iterations.find(common);
                    if(it!=a[i].iterations.end()&&it->second.second>selectedScore){choice=i;selectedScore=it->second.second;selectedMove=it->second.first;}
                }
                if(!selectedMove.empty()){a[choice].move=selectedMove;a[choice].score=selectedScore;a[choice].depth=common;}
#endif
                auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count();
                std::cerr<<"info depth "<<a[choice].depth<<" score "<<a[choice].score<<" nodes "<<nodes<<" time "<<ms<<" nps "<<(ms?nodes*1000/ms:0)<<std::endl;
                std::cout<<"bestmove "<<a[choice].move<<std::endl;
            }else if(command=="eval"||command=="perft"||command=="fen"||command=="d"){
                if(command=="eval" || command=="perft"){
                    auto reply=workers[0]->request(line+"\nisready","readyok");
                    for(const auto& x:reply)if(x!="readyok")std::cout<<x<<std::endl;
                }else{auto reply=workers[0]->request(line+"\nisready","readyok");for(const auto& x:reply)if(x!="readyok")std::cout<<x<<std::endl;}
            }else for(auto& w:workers)w->send(line);
        }
    }catch(const std::exception& e){std::cerr<<"info string V4 worker failure: "<<e.what()<<std::endl;return 1;}
}
