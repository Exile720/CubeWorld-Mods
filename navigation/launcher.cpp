#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs=std::filesystem;
namespace {
int fail(const std::wstring& message,DWORD error=0) {
    const auto text=message+(error?L"\nWindows 错误码："+std::to_wstring(error):L"");
    MessageBoxW(nullptr,text.c_str(),L"魔方世界 · 导航",MB_OK|MB_ICONERROR);return 1;
}
bool requireFile(const fs::path& p){return fs::is_regular_file(p);}
int retryElevated(const wchar_t* launcher,const fs::path& dir) {
    // Ask Windows for normal UAC consent. Relaunch this exact launcher so the
    // elevated instance still creates and owns the exact child used for loading.
    SHELLEXECUTEINFOW request{};request.cbSize=sizeof(request);
    request.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;
    request.lpVerb=L"runas";request.lpFile=launcher;
    request.lpParameters=L"--elevated-retry";request.lpDirectory=dir.c_str();
    request.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&request)) {
        const auto error=GetLastError();
        if(error==ERROR_CANCELLED) return fail(L"已取消管理员授权，本次没有启动游戏。");
        return fail(L"无法请求管理员授权。可右键“导航启动”，选择“以管理员身份运行”。",error);
    }
    if(request.hProcess) CloseHandle(request.hProcess);
    return 0;
}
// Only inspect existing processes to protect this directory's Save. Never inject into them.
int alreadyRunning(const fs::path& game) {
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);int result=0;
    for(BOOL ok=Process32FirstW(snapshot,&entry);ok;ok=Process32NextW(snapshot,&entry)) {
        if(_wcsicmp(entry.szExeFile,L"Cube.exe")!=0) continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if(!process){result=-1;break;}
        wchar_t path[32768]{};DWORD length=32768;
        const BOOL queried=QueryFullProcessImageNameW(process,0,path,&length);
        CloseHandle(process);
        if(!queried){result=-1;break;}
        if(_wcsicmp(path,game.c_str())==0){result=1;break;}
    }
    CloseHandle(snapshot);return result;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR arguments,int) {
    wchar_t ownPath[32768]{};
    const DWORD len=GetModuleFileNameW(nullptr,ownPath,32768);
    if(!len||len>=32768) return fail(L"无法确定游戏目录。");
    const fs::path dir=fs::path(ownPath).parent_path();
    const auto game=dir/L"Cube.exe",dll=dir/L"cube_mod.dll";
    // Historical marker names identify the prepared game directory, not a dependency on a mod.
    if((!requireFile(dir/L".navigation-sandbox.json")&&!requireFile(dir/L".navigation-installed.json"))||!requireFile(game)||!requireFile(dll))
        return fail(L"请从已准备模组框架的游戏目录启动，缺少必要文件。");
    // Prevent a second launcher writing to this directory's Save concurrently.
    const std::wstring mutexName=L"Local\\CubeWaypoint_"+std::to_wstring(std::hash<std::wstring>{}(dir.wstring()));
    HANDLE mutex=CreateMutexW(nullptr,FALSE,mutexName.c_str());
    if(!mutex) return fail(L"无法锁定游戏目录。",GetLastError());
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mutex);return fail(L"这份游戏的导航启动器已在运行。");}
    const int running=alreadyRunning(game);
    if(running!=0){CloseHandle(mutex);return fail(running>0?L"这份游戏已在运行，请正常退出后再用导航启动器打开。":L"无法确认游戏是否已运行，本次没有启动。请关闭其他 Cube 游戏后重试。");}
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    std::wstring command=L"\""+game.wstring()+L"\"";
    std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
    if(!CreateProcessW(game.c_str(),buffer.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,dir.c_str(),&si,&pi)) {
        const auto e=GetLastError();CloseHandle(mutex);
        if(e==ERROR_ELEVATION_REQUIRED) {
            // Retry only once, and only for Windows error 740; never loop on denial/failure.
            if(arguments&&std::wstring(arguments)==L"--elevated-retry")
                return fail(L"管理员启动后游戏仍要求提升权限，请检查游戏的兼容性设置。",e);
            return retryElevated(ownPath,dir);
        }
        return fail(L"启动游戏失败。",e);
    }
    auto stop=[&]{TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(mutex);};
    const std::wstring dllPath=dll.wstring();const SIZE_T bytes=(dllPath.size()+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(pi.hProcess,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    SIZE_T written=0;
    if(!remote||!WriteProcessMemory(pi.hProcess,remote,dllPath.c_str(),bytes,&written)||written!=bytes) {
        const auto e=GetLastError();stop();return fail(L"无法向本次启动的游戏加载导航框架。",e);
    }
    // Let Windows finish process/DLL initialization before the remote loader thread.
    // Injection still happens at startup, before the user can enter a world.
    if(ResumeThread(pi.hThread)==static_cast<DWORD>(-1)){const auto e=GetLastError();stop();return fail(L"无法继续启动游戏。",e);}
    auto load=reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"));
    HANDLE thread=load?CreateRemoteThread(pi.hProcess,nullptr,0,load,remote,0,nullptr):nullptr;
    if(!thread){const auto e=GetLastError();stop();return fail(L"加载导航框架失败。",e);}
    const auto wait=WaitForSingleObject(thread,15000);DWORD loaded=0;
    const bool ok=wait==WAIT_OBJECT_0&&GetExitCodeThread(thread,&loaded)&&loaded!=0;
    CloseHandle(thread);
    if(!ok){stop();return fail(L"导航框架加载未完成，已关闭本次启动的游戏。");}
    VirtualFreeEx(pi.hProcess,remote,0,MEM_RELEASE);
    CloseHandle(pi.hThread);
    std::ofstream log(dir/L"navigation-launch.txt",std::ios::trunc);
    log<<"PID="<<pi.dwProcessId<<"\nExact child handle was used; no process-name lookup.\n";
    log.close();
    // Keeping the mutex alive prevents two launchers sharing this Save. No other processes are touched.
    WaitForSingleObject(pi.hProcess,INFINITE);
    CloseHandle(pi.hProcess);CloseHandle(mutex);return 0;
}
