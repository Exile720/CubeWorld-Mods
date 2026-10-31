// Offline helper for the one explicitly authorized 2026-10-29 missed kill.
// Writes only a new candidate file. The guarded PowerShell script owns commit.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "loot.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
constexpr std::uint64_t bossId=1234ull;
constexpr const char* compensationName="漏记补偿 · 示例地标";
loot::State load(const wchar_t* path){
    loot::State s;if(std::wcscmp(path,L"--empty")==0)return s;
    const std::filesystem::path file(path);const auto n=std::filesystem::file_size(file);
    if(n<40||n>loot::maxBytes)throw std::runtime_error("Invalid ledger size; no output written");
    std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read ledger");
    const std::string bytes{std::istreambuf_iterator<char>(stream),{}};
    if(bytes.size()!=n||!loot::decode(bytes,s))throw std::runtime_error("Corrupt ledger; no output written");
    return s;
}
void inspect(const loot::State& s){
    unsigned compensationCount=0;std::uint64_t serial=0;
    for(const auto& t:s.tickets){if(t.cl!=4)throw std::runtime_error("Unexpected character class; refusing mixed scope");
        if(t.bossId==bossId&&t.bossName==compensationName){++compensationCount;serial=t.serial;}}
    std::printf("{\"pending\":%u,\"misses\":%u,\"nextSerial\":%llu,\"compensationCount\":%u,\"compensationSerial\":%llu}\n",
        unsigned(s.tickets.size()),s.misses,s.nextSerial,compensationCount,serial);
}
}
int wmain(int argc,wchar_t** argv){
    try{
        if(argc==3&&std::wcscmp(argv[1],L"--inspect")==0){inspect(load(argv[2]));return 0;}
        if(argc!=4||std::wcscmp(argv[1],L"--prepare")!=0)throw std::runtime_error("Use --inspect input or --prepare input output");
        auto s=load(argv[2]);const unsigned before=unsigned(s.tickets.size()),misses=s.misses;
        for(const auto& t:s.tickets){
            if(t.cl!=4||t.stage==loot::Stage::Claiming)throw std::runtime_error("Wrong class or ambiguous claim; no compensation written");
            if(t.bossId==bossId&&t.bossName==compensationName)throw std::runtime_error("Compensation already pending; refusing a duplicate");
        }
        if(std::wcscmp(argv[2],L"--empty")==0){FILETIME time{};GetSystemTimeAsFileTime(&time);
            s.rng=((std::uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime)^GetTickCount64()^GetCurrentProcessId();}
        loot::Ticket ticket;ticket.cl=4;ticket.bossId=bossId;ticket.bossName=compensationName;
        // Compensation parameters, not recovered historical Boss level/rank.
        ticket.bossLevel=45;ticket.rank=4;
        if(!loot::credit(s,ticket)||s.tickets.size()!=before+1||s.misses!=misses)throw std::runtime_error("Cannot append one sealed compensation");
        const auto bytes=loot::encode(s);loot::State check;
        if(!loot::decode(bytes,check)||loot::encode(check)!=bytes)throw std::runtime_error("Candidate round-trip failed");
        const auto file=CreateFileW(argv[3],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Output already exists or cannot be created");
        DWORD written=0;const bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
        CloseHandle(file);if(!ok)throw std::runtime_error("Candidate write failed; game data was not touched");
        inspect(check);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
