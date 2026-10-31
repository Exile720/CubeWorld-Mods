// Offline probe for the verified 2013 Alpha item generators.
// Reads Cube.exe; executes only audited function copies inside this disposable
// process. Never opens the game process, writes game files, or loads game DLLs.
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

struct Item { unsigned char b[0x118]{}; };
struct Vec { Item* begin; Item* end; Item* cap; };
static uint32_t nativeState, nativeCalls, cookie = 0x13579bdf;
static std::vector<Item> trace;
static uint32_t nextValue(uint32_t& state) {
    state = state * 214013u + 2531011u;
    return (state >> 16) & 32767u;
}
static int __cdecl randShim() { ++nativeCalls; return int(nextValue(nativeState)); }
static auto randPointer = &randShim;
static void __fastcall appendShim(Vec* v, void*, const Item* item) {
    trace.push_back(*item);
    size_t count = v->begin ? size_t(v->end - v->begin) : 0;
    Item* p = static_cast<Item*>(std::realloc(v->begin, (count + 1) * sizeof(Item)));
    if (!p) std::abort();
    v->begin = p; v->end = p + count + 1; v->cap = v->end;
    std::memcpy(p + count, item, sizeof(Item));
}
static Item* __fastcall copyShim(Item* out, void*, const Item* from) {
    std::memcpy(out, from, sizeof(Item)); return out;
}
static void __fastcall cookieShim(uint32_t, void*) {}
static void fail(const char* reason) { std::fprintf(stderr, "%s\n", reason); std::exit(2); }

struct Image {
    std::vector<unsigned char> bytes;
    IMAGE_NT_HEADERS32* nt;
    explicit Image(const char* path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) fail("Cannot read original image");
        bytes.assign(std::istreambuf_iterator<char>(f), {});
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        unsigned char digest[32];
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            fail("Cannot initialize hash check");
        NTSTATUS status = BCryptHash(algorithm, nullptr, 0, bytes.data(), (ULONG)bytes.size(), digest, 32);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if (status < 0) fail("Cannot hash original image");
        char hash[65]{};
        for (unsigned i = 0; i < 32; ++i) std::sprintf(hash + i * 2, "%02x", digest[i]);
        if (std::strcmp(hash, "84a7a132a84d4282338e7ea45a1940d32066d64e39cbafed8f2418d5a6dc30bf"))
            fail("Unsupported image hash");
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
        if (bytes.size() < 0x1000 || dos->e_magic != IMAGE_DOS_SIGNATURE) fail("Bad PE");
        nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.ImageBase != 0x400000 || nt->OptionalHeader.SizeOfImage != 0x3bc000)
            fail("Unsupported image");
    }
    const unsigned char* at(uint32_t va, size_t n) const {
        uint32_t rva = va - nt->OptionalHeader.ImageBase;
        auto sec = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (rva >= sec[i].VirtualAddress && uint64_t(rva) + n <=
                    uint64_t(sec[i].VirtualAddress) + sec[i].SizeOfRawData) {
                size_t p = sec[i].PointerToRawData + rva - sec[i].VirtualAddress;
                if (p + n <= bytes.size()) return bytes.data() + p;
            }
        }
        fail("Unmapped image address"); return nullptr;
    }
};

static void* relocate(const Image& img, uint32_t va, size_t length, const char* listing, bool withCookie = true) {
    auto code = static_cast<unsigned char*>(VirtualAlloc(nullptr, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!code) fail("Probe allocation failed");
    std::memcpy(code, img.at(va, length), length);
    const std::map<uint32_t, uintptr_t> targets = {
        {0x4c7830, reinterpret_cast<uintptr_t>(&appendShim)},
        {0x40ee70, reinterpret_cast<uintptr_t>(&copyShim)},
        {0x68d7b0, reinterpret_cast<uintptr_t>(&std::memset)},
        {0x68d66e, reinterpret_cast<uintptr_t>(&std::free)},
        {0x68d788, reinterpret_cast<uintptr_t>(&cookieShim)}
    };
    std::ifstream f(listing);
    if (!f) fail("Missing disassembly listing");
    std::string line;
    std::regex instruction(R"(^\s*([0-9A-F]{8}):.*? {2,}([a-z][a-z0-9]*)\s*(.*)$)");
    unsigned patchedCalls = 0, patchedRand = 0, patchedCookie = 0;
    while (std::getline(f, line)) {
        std::smatch m;
        if (!std::regex_match(line, m, instruction)) continue;
        uint32_t a = std::stoul(m[1], nullptr, 16);
        if (a < va || a >= va + length) continue;
        size_t p = a - va;
        std::string op = m[2], args = m[3];
        if (op == "call" && code[p] == 0xe8) {
            uint32_t target = a + 5 + *reinterpret_cast<int32_t*>(code + p + 1);
            if (target >= va && target < va + length) continue;
            auto it = targets.find(target);
            if (it == targets.end()) fail("Unexpected external call in audited copy");
            *reinterpret_cast<uint32_t*>(code + p + 1) = uint32_t(it->second - uintptr_t(code + p + 5));
            ++patchedCalls;
        }
        if (op == "mov" && code[p] == 0x8b && code[p + 1] == 0x3d &&
            *reinterpret_cast<uint32_t*>(code + p + 2) == 0x6fc468) {
            *reinterpret_cast<uint32_t*>(code + p + 2) = uint32_t(reinterpret_cast<uintptr_t>(&randPointer));
            ++patchedRand;
        }
        if (op == "mov" && code[p] == 0xa1 &&
            *reinterpret_cast<uint32_t*>(code + p + 1) == 0x76aa78) {
            *reinterpret_cast<uint32_t*>(code + p + 1) = uint32_t(reinterpret_cast<uintptr_t>(&cookie));
            ++patchedCookie;
        }
    }
    if (patchedRand != 1 || patchedCookie != unsigned(withCookie) ||
        (withCookie && patchedCalls < 3) || (!withCookie && patchedCalls)) fail("Audit patch count mismatch");
    DWORD old;
    if (!VirtualProtect(code, length, PAGE_EXECUTE_READ, &old)) fail("Probe protection failed");
    FlushInstructionCache(GetCurrentProcess(), code, length);
    return code;
}

static Item makeItem(int type, int subtype, int material, int seed, int level, int quality) {
    Item i;
    i.b[0] = (unsigned char)type; i.b[1] = (unsigned char)subtype;
    std::memcpy(i.b + 4, &seed, 4);
    i.b[12] = (unsigned char)quality; i.b[13] = (unsigned char)material;
    int16_t l = (int16_t)level; std::memcpy(i.b + 16, &l, 2);
    return i;
}
static bool equal(const Item& a, const Item& b) {
    // The original struct's padding bytes are deliberately excluded.
    return a.b[0] == b.b[0] && a.b[1] == b.b[1] &&
        std::memcmp(a.b + 4, b.b + 4, 11) == 0 &&
        std::memcmp(a.b + 16, b.b + 16, 2) == 0 &&
        std::memcmp(a.b + 20, b.b + 20, 260) == 0;
}
struct Reference {
    uint32_t state, calls = 0;
    std::vector<Item> pool;
    uint32_t rnd() { ++calls; return nextValue(state); }
    Item generate(bool weapon, int level, int quality, int cl) {
        auto add = [&](int type, int subtype, int material) {
            int seed = int(rnd() % 100);
            pool.push_back(makeItem(type, subtype, material, seed, level, quality));
        };
        if (weapon) {
            if (cl == 1 || cl < 0) {
                int sub = int(rnd() % 3); add(3, sub, 1);
                sub = int(rnd() % 3) + 15; add(3, sub, 1); add(3, 13, 1);
            }
            if (cl == 4 || cl < 0) { add(3, 3, 1); add(3, 5, 1); add(3, 4, 1); }
            if (cl == 2 || cl < 0) { add(3, 6, 2); rnd(); add(3, 8, 2); }
            if (cl == 3 || cl < 0) {
                add(3, 10, 2); add(3, 11, 2);
                int material = 12 - (rnd() % 2 != 0); add(3, 12, material);
            }
        } else {
            for (auto pair : {std::pair<int,int>{1,1}, {3,25}, {2,26}, {4,27}}) {
                if (cl != pair.first && cl >= 0) continue;
                add(7, 0, pair.second);
                if (pair.first != 1) add(7, 0, pair.second); // Original duplicate entry.
                for (int type : {4,5,6}) add(type, 0, pair.second);
            }
            for (int type : {8,9}) {
                int material = 11 + int(rnd() % 2 != 0); add(type, 0, material);
            }
        }
        if (pool.empty()) fail("Unsupported reference class");
        return pool[rnd() % pool.size()];
    }
};

// Exact boost-joint counts under a uniform 31-bit effective CRT state.
// This is a mathematical state-space result, not observed boss-kill statistics.
static void exactQualityCounts() {
    constexpr uint64_t domain = uint64_t(1) << 31;
    constexpr uint32_t mask = 0x7fffffff;
    // Modular inverse of 214013, obtained by Newton iteration modulo 2^32.
    uint32_t inverse = 1;
    for (int i = 0; i < 5; ++i) inverse *= 2u - 214013u * inverse;
    auto prev = [&](uint32_t s) { return ((s - 2531011u) * inverse) & mask; };
    uint64_t counts[5][5]{};
    for (int r = 0; r < 5; ++r)
        for (uint32_t out = 0; out < 32768; ++out) counts[r][out % (r + 1)] += 65536;
    uint64_t boosts[4]{};
    auto adjust = [&](uint32_t baseState, int n) {
        if (!n) return;
        ++boosts[n];
        uint32_t base = (baseState >> 16) & 32767u;
        for (int r = 0; r < 5; ++r) {
            int q = int(base % (r + 1));
            --counts[r][q]; ++counts[r][std::min(4, q + n)];
        }
    };
    for (uint32_t out = 0; out < 32768; out += 100) {
        for (uint32_t low = 0; low < 65536; ++low) {
            uint32_t first = (out << 16) | low, s = first;
            bool b = nextValue(s) % 1000 == 0, c = nextValue(s) % 10000 == 0;
            adjust(prev(first), 1 + int(b) + int(c));
        }
    }
    for (uint32_t out = 0; out < 32768; out += 1000) {
        for (uint32_t low = 0; low < 65536; ++low) {
            uint32_t second = (out << 16) | low, first = prev(second), s = second;
            if (((first >> 16) & 32767u) % 100 == 0) continue;
            bool c = nextValue(s) % 10000 == 0;
            adjust(prev(first), 1 + int(c));
        }
    }
    for (uint32_t out = 0; out < 32768; out += 10000) {
        for (uint32_t low = 0; low < 65536; ++low) {
            uint32_t third = (out << 16) | low, second = prev(third), first = prev(second);
            if (((first >> 16) & 32767u) % 100 == 0 || ((second >> 16) & 32767u) % 1000 == 0) continue;
            adjust(prev(first), 1);
        }
    }
    boosts[0] = domain - boosts[1] - boosts[2] - boosts[3];
    std::printf("\"uniformEffectiveStateDomain\":%llu,\"boostCounts\":[%llu,%llu,%llu,%llu],\"qualityCounts\":[",
        domain, boosts[0], boosts[1], boosts[2], boosts[3]);
    for (int r = 0; r < 5; ++r) {
        uint64_t sum = 0;
        for (int q = 0; q < 5; ++q) sum += counts[r][q];
        if (sum != domain) fail("Probability count invariant failed");
        std::printf("%s[%llu,%llu,%llu,%llu,%llu]", r ? "," : "",
            counts[r][0], counts[r][1], counts[r][2], counts[r][3], counts[r][4]);
    }
    std::printf("]");
}

int main(int argc, char** argv) {
    static_assert(sizeof(void*) == 4, "This probe must be compiled for x86");
    if (argc != 3) fail("Usage: loot_native_probe Cube.exe evidence-directory");
    Image image(argv[1]);
    std::string root = argv[2];
    using Generate = Item* (__stdcall *)(Item*, int, int, int);
    auto armor = reinterpret_cast<Generate>(relocate(image, 0x5f51e0, 0x4d8, (root + "/armor.asm.txt").c_str()));
    auto weapon = reinterpret_cast<Generate>(relocate(image, 0x5f8ad0, 0x340, (root + "/weapon.asm.txt").c_str()));
    using Quality = int (__stdcall *)(int, char);
    auto qualityFn = reinterpret_cast<Quality>(relocate(image, 0x5f8530, 0x60,
        (root + "/quality.asm.txt").c_str(), false));
    unsigned comparisons = 0, candidateComparisons = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        uint32_t seed = i * 2654435761u;
        for (int cl : {-1,1,2,3,4}) for (int w = 0; w < 2; ++w) {
            nativeState = seed; nativeCalls = 0; trace.clear();
            Item actual; int level = 1 + int(i % 200), quality = int(i % 5);
            Item* returned = (w ? weapon : armor)(&actual, level, quality, cl);
            Reference reference{seed}; Item expected = reference.generate(w != 0, level, quality, cl);
            if (returned != &actual || !equal(actual, expected) || reference.state != nativeState ||
                reference.calls != nativeCalls || reference.pool.size() != trace.size())
                fail("Native generation differs from reconstructed algorithm");
            for (size_t p = 0; p < trace.size(); ++p) {
                if (!equal(trace[p], reference.pool[p])) fail("Candidate pool mismatch");
                ++candidateComparisons;
            }
            ++comparisons;
        }
    }
    unsigned qualityComparisons = 0;
    for (uint32_t i = 0; i < 10000; ++i) for (int rank = 0; rank <= 5; ++rank) for (char force : {char(0),char(1)}) {
        uint32_t seed = i * 2654435761u, state = seed;
        int expected = int(nextValue(state) % (rank + 1));
        expected += nextValue(state) % 100 == 0;
        expected += nextValue(state) % 1000 == 0;
        expected += nextValue(state) % 10000 == 0;
        if (force) expected = rank + 1;
        expected = std::min(4, expected);
        nativeState = seed; nativeCalls = 0;
        int actual = qualityFn(rank, force);
        if (actual != expected || nativeState != state || nativeCalls != 4)
            fail("Native quality differs from reconstructed algorithm");
        ++qualityComparisons;
    }
    std::printf("{\"nativeGeneratorCases\":%u,\"nativeCandidateComparisons\":%u,\"nativeQualityCases\":%u,",
        comparisons, candidateComparisons, qualityComparisons);
    exactQualityCounts();
    std::printf("}\n");
    VirtualFree(reinterpret_cast<void*>(armor), 0, MEM_RELEASE);
    VirtualFree(reinterpret_cast<void*>(weapon), 0, MEM_RELEASE);
    VirtualFree(reinterpret_cast<void*>(qualityFn), 0, MEM_RELEASE);
    return 0;
}
