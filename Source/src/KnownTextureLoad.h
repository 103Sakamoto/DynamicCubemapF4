#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>

// Exact installed Addictol 1.7.0 profile; unknown builds remain unverified.
// Source review and disassembly evidence: ADDICTOL-COMPATIBILITY.txt.
inline bool ValidateAddictolLoadImage(std::uintptr_t module,std::uintptr_t function,std::uintptr_t game)
{
    if(function!=module+0x110900 || !IsReadableSpan(module,sizeof(IMAGE_DOS_HEADER))) return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x1000) return false;
    const auto ntAddress=module+static_cast<std::uintptr_t>(dos->e_lfanew);
    if(!IsReadableSpan(ntAddress,sizeof(IMAGE_NT_HEADERS64))) return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(ntAddress);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.TimeDateStamp!=0x6ac1a644u || nt->OptionalHeader.SizeOfImage!=0x613000u) return false;
    if(!IsReadableSpan(function,0x1d6) || !IsReadableSpan(module+0x5c57c0,5*sizeof(std::uintptr_t))) return false;
    constexpr std::uint8_t expected[]={0xef,0x54,0xdb,0xfe,0x7a,0xe5,0x1a,0x1a,0x63,0x4d,0xbb,0xd0,0x25,0xd0,0xaa,0xa3,0x53,0x89,0x49,0xd4,0xfe,0x60,0xa7,0xe4,0xc6,0x82,0x10,0xda,0xb4,0x6c,0xb2,0x46};
    std::uint8_t actual[32]{};
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(function),0x1d6,actual,sizeof(actual));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0 || std::memcmp(expected,actual,sizeof(actual))!=0) { Log("TINT CACHE VERIFY: Addictol replacement SHA256 mismatch or hash failure."); return false; }
    // Verify the reviewed implementation still calls the native renderer helpers.
    // RUNTIMECOMPAT BEGIN
    constexpr std::uintptr_t aeTargets[]={0x16b9d40,0x183cf10,0x181bb30,0x183dab0,0x3a25d00};
    constexpr std::uintptr_t ogTargets[]={0x1b93840,0x1d35ac0,0x1d0ec70,0x1d363a0,0x61e0900};
    constexpr std::uintptr_t ngTargets[]={0x159ec90,0x1721bf0,0x1700850,0x1722790,0x3769600};
    const auto* targets=g_runtimeVersion=="1.11.240.0"?aeTargets:
        g_runtimeVersion=="1.10.163.0"?ogTargets:g_runtimeVersion=="1.10.984.0"?ngTargets:nullptr;
    if (!targets) return false;
    // RUNTIMECOMPAT END
    const auto* helpers=reinterpret_cast<const std::uintptr_t*>(module+0x5c57c0);
    for(unsigned i=0;i<5;++i) if(helpers[i]!=game+targets[i]) {
        Log("TINT CACHE VERIFY: Addictol renderer helper index="+std::to_string(i)+" actual="+Ptr(reinterpret_cast<const void*>(helpers[i]))+" expected="+Ptr(reinterpret_cast<const void*>(game+targets[i])));
        return false;
    }
    return true;
}
inline bool IsKnownAddictolLoad(std::uintptr_t function,std::uintptr_t game)
{
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<const void*>(function),&info,sizeof(info)) || info.Type!=MEM_IMAGE || !info.AllocationBase) return false;
    wchar_t path[32768]{};
    const auto length=GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase),path,32768);
    if(!length || length>=32768) return false;
    const auto* name=wcsrchr(path,L'\\'); name=name?name+1:path;
    if(_wcsicmp(name,L"Addictol.dll")!=0) return false;
    return ValidateAddictolLoadImage(reinterpret_cast<std::uintptr_t>(info.AllocationBase),function,game);
}
