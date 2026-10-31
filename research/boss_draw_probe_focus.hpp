#pragma once
// Offline probe only. Patches two import slots in its own loaded Mod mapping;
// never opens another process, changes DLL files, or takes desktop focus.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstring>

namespace probe_focus {
struct ImportPatch {DWORD* slot=nullptr;DWORD original=0;};
inline std::array<ImportPatch,2> patches{};
inline HWND WINAPI foreground(){return reinterpret_cast<HWND>(1);}
inline DWORD WINAPI windowOwner(HWND window,DWORD* process){
    if(process)*process=window==reinterpret_cast<HWND>(1)?GetCurrentProcessId():0;
    return window==reinterpret_cast<HWND>(1)?GetCurrentThreadId():0;
}
inline bool replace(ImportPatch& patch,DWORD value){
    DWORD old=0;if(!patch.slot||!VirtualProtect(patch.slot,sizeof(DWORD),PAGE_READWRITE,&old))return false;
    *patch.slot=value;DWORD unused=0;return VirtualProtect(patch.slot,sizeof(DWORD),old,&unused)!=0;
}
inline void restore(){for(auto& patch:patches){if(patch.slot)replace(patch,patch.original);patch={};}}
inline bool install(HMODULE module){
    if(!module||module==GetModuleHandleW(nullptr)||patches[0].slot||patches[1].slot)return false;
    auto* base=reinterpret_cast<unsigned char*>(module);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0x40||dos->e_lfanew>0x1000)return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    const DWORD size=nt->OptionalHeader.SizeOfImage;
    const auto inside=[&](DWORD rva,DWORD bytes){return rva<size&&bytes<=size-rva;};
    const auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress||!inside(dir.VirtualAddress,dir.Size))return false;
    std::array<ImportPatch,2> found{};
    for(DWORD off=0;off+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=dir.Size;off+=sizeof(IMAGE_IMPORT_DESCRIPTOR)){
        const auto* entry=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress+off);
        if(!entry->Name)break;
        if(!entry->OriginalFirstThunk||!entry->FirstThunk)continue;
        for(DWORD n=0;n<4096;++n){
            const DWORD src=entry->OriginalFirstThunk+n*sizeof(IMAGE_THUNK_DATA32),dst=entry->FirstThunk+n*sizeof(IMAGE_THUNK_DATA32);
            if(!inside(src,sizeof(IMAGE_THUNK_DATA32))||!inside(dst,sizeof(IMAGE_THUNK_DATA32)))return false;
            const auto nameRva=reinterpret_cast<const IMAGE_THUNK_DATA32*>(base+src)->u1.AddressOfData;
            if(!nameRva)break;
            if(IMAGE_SNAP_BY_ORDINAL32(nameRva))continue;
            if(!inside(nameRva,3))return false;
            const auto* name=reinterpret_cast<const char*>(base+nameRva+2);
            if(!std::memchr(name,0,size-nameRva-2))return false;
            const unsigned index=std::strcmp(name,"GetForegroundWindow")==0?0u:(std::strcmp(name,"GetWindowThreadProcessId")==0?1u:2u);
            if(index<2){if(found[index].slot)return false;auto* slot=reinterpret_cast<DWORD*>(base+dst);found[index]={slot,*slot};}
        }
    }
    if(!found[0].slot||!found[1].slot)return false;
    patches=found;
    if(!replace(patches[0],reinterpret_cast<DWORD>(&foreground))||!replace(patches[1],reinterpret_cast<DWORD>(&windowOwner))){restore();return false;}
    return true;
}
}
