/*
 * v67suite â€” Roblox External Cheat
 * ESP | Aimbot | Offset Dumper | Custom Keybinds
 * Built for GitHub Actions CI â†’ .exe
 */

#include <windows.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <winhttp.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <functional>
#include <map>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  MEMORY PRIMITIVES
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static HANDLE g_proc = nullptr;
static DWORD  g_pid  = 0;

template<typename T>
static T RPM(uintptr_t addr) {
    T v{};
    ReadProcessMemory(g_proc, (LPCVOID)addr, &v, sizeof(T), nullptr);
    return v;
}

static std::string RPM_str(uintptr_t addr, size_t maxLen = 64) {
    char buf[256]{};
    ReadProcessMemory(g_proc, (LPCVOID)addr, buf, (maxLen < sizeof(buf)-1 ? maxLen : sizeof(buf)-1), nullptr);
    return std::string(buf);
}

static uintptr_t RPM_chain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
    uintptr_t cur = base;
    for (auto off : offsets) {
        cur = RPM<uintptr_t>(cur);
        if (!cur) return 0;
        cur += off;
    }
    return cur;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  PROCESS / MODULE HELPERS
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static DWORD FindPID(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe))
        do { if (!_wcsicmp(pe.szExeFile, name)) { pid = pe.th32ProcessID; break; } }
        while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return pid;
}

static uintptr_t GetModuleBase(DWORD pid, const wchar_t* mod) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me{ sizeof(me) };
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me))
        do { if (!_wcsicmp(me.szModule, mod)) { base = (uintptr_t)me.modBaseAddr; break; } }
        while (Module32NextW(snap, &me));
    CloseHandle(snap);
    return base;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  ROBLOX OFFSETS â€” auto-dumped at runtime
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct Offsets {
    // DataModel
    uintptr_t DataModel        = 0;
    // Workspace
    uintptr_t Workspace        = 0;
    // Camera
    uintptr_t Camera           = 0;
    // Primitive field offsets within Part/Humanoid/etc.
    uintptr_t oName            = 0x68;  // Instance::Name
    uintptr_t oChildren        = 0x70;  // Instance::Children (RBX::vector)
    uintptr_t oParent          = 0x60;  // Instance::Parent
    uintptr_t oClassName       = 0x18;  // Instance::ClassName
    // Humanoid
    uintptr_t oHealth          = 0x148;
    uintptr_t oMaxHealth       = 0x150;
    // BasePart CFrame / size
    uintptr_t oCFrame          = 0x110; // BasePart::CFrame (4x4 float matrix)
    // Camera ViewMatrix
    uintptr_t oViewMatrix      = 0x1F8;
    // Primitive / RootPart ptr in Model
    uintptr_t oPrimaryPart     = 0x100;
    // LocalPlayer ptr chain from DataModel
    uintptr_t oPlayers         = 0;
    uintptr_t oLocalPlayer     = 0;

    bool dumped = false;
    std::string log;
};
static Offsets g_off;

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  OFFSET DUMPER â€” pattern scan + heuristic
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct ScanRegion { uintptr_t base; SIZE_T size; std::vector<uint8_t> buf; };

static std::vector<ScanRegion> SnapshotMemory(HANDLE proc) {
    std::vector<ScanRegion> regions;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0;
    while (VirtualQueryEx(proc, (LPCVOID)addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & PAGE_GUARD) &&
            mbi.RegionSize <= 0x4000000)
        {
            ScanRegion r;
            r.base = (uintptr_t)mbi.BaseAddress;
            r.size = mbi.RegionSize;
            r.buf.resize(mbi.RegionSize);
            SIZE_T read = 0;
            if (ReadProcessMemory(proc, mbi.BaseAddress, r.buf.data(), mbi.RegionSize, &read))
                r.buf.resize(read);
            if (!r.buf.empty())
                regions.push_back(std::move(r));
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (addr == 0) break;
    }
    return regions;
}

// Pattern-match helper
static uintptr_t PatternScan(const std::vector<ScanRegion>& regions,
                              const std::vector<int>& pat) {  // -1 = wildcard
    for (auto& r : regions) {
        if (r.buf.size() < pat.size()) continue;
        for (size_t i = 0; i <= r.buf.size() - pat.size(); ++i) {
            bool ok = true;
            for (size_t j = 0; j < pat.size(); ++j) {
                if (pat[j] != -1 && r.buf[i+j] != (uint8_t)pat[j]) { ok = false; break; }
            }
            if (ok) return r.base + i;
        }
    }
    return 0;
}

// Brute-force DataModel by scanning for "rbxasset" / "RobloxGame" string refs
static uintptr_t BruteDataModel(const std::vector<ScanRegion>& regions) {
    // look for the string "DataModel" in memory, then back-trace to the Instance header
    const char* needle = "DataModel";
    size_t nlen = strlen(needle);
    std::vector<uintptr_t> candidates;
    for (auto& r : regions) {
        for (size_t i = 0; i + nlen <= r.buf.size(); ++i) {
            if (memcmp(&r.buf[i], needle, nlen) == 0 && r.buf[i+nlen] == 0) {
                candidates.push_back(r.base + i);
            }
        }
    }
    // For each string address, look for a pointer to it in the region
    // (ClassName ptr, typically at offset 0x18 of an Instance)
    for (uintptr_t saddr : candidates) {
        for (auto& r : regions) {
            for (size_t i = 0; i + 8 <= r.buf.size(); i += 8) {
                uintptr_t ptr;
                memcpy(&ptr, &r.buf[i], 8);
                if (ptr == saddr) {
                    // candidate Instance base = r.base+i - 0x18
                    uintptr_t inst = r.base + i - 0x18;
                    // sanity: inst+0x18 should equal saddr when read live
                    uintptr_t check = RPM<uintptr_t>(inst + 0x18);
                    if (check == saddr) return inst;
                }
            }
        }
    }
    return 0;
}

// Enumerate children of an Instance node
struct ChildList { uintptr_t data; uint32_t size; };
static std::vector<uintptr_t> GetChildren(uintptr_t inst) {
    std::vector<uintptr_t> out;
    if (!inst) return out;
    uintptr_t childVec = RPM<uintptr_t>(inst + g_off.oChildren);
    if (!childVec) return out;
    uintptr_t dataPtr  = RPM<uintptr_t>(childVec);
    uint32_t  count    = RPM<uint32_t> (childVec + 0x10);
    if (!dataPtr || count > 4096) return out;
    for (uint32_t i = 0; i < count; ++i)
        out.push_back(RPM<uintptr_t>(dataPtr + i * sizeof(uintptr_t)));
    return out;
}

static std::string GetName(uintptr_t inst) {
    if (!inst) return "";
    uintptr_t namePtr = RPM<uintptr_t>(inst + g_off.oName);
    if (!namePtr) return "";
    // RBX::String: first 8 bytes = size, then chars inline or heap
    uint32_t sz = RPM<uint32_t>(namePtr);
    if (sz == 0 || sz > 128) return "";
    char buf[129]{};
    ReadProcessMemory(g_proc, (LPCVOID)(namePtr + 0x10), buf, (sz < 128u ? sz : 128u), nullptr);
    return std::string(buf, (sz < 128u ? sz : 128u));
}

static std::string GetClassName(uintptr_t inst) {
    if (!inst) return "";
    uintptr_t cn = RPM<uintptr_t>(inst + g_off.oClassName);
    if (!cn) return "";
    char buf[64]{};
    ReadProcessMemory(g_proc, (LPCVOID)cn, buf, 63, nullptr);
    return std::string(buf);
}

static uintptr_t FindChildByName(uintptr_t parent, const std::string& name, int depth = 2) {
    if (!parent || depth == 0) return 0;
    for (auto c : GetChildren(parent)) {
        if (!c) continue;
        if (GetName(c) == name) return c;
        if (depth > 1) {
            uintptr_t r = FindChildByName(c, name, depth - 1);
            if (r) return r;
        }
    }
    return 0;
}

static void DumpOffsets(uintptr_t rbxBase) {
    g_off.log.clear();
    g_off.log += "[v67 offset dump]\n";

    // â”€â”€ DataModel â”€â”€
    std::ostringstream ss;

    // snapshot memory (can take a moment)
    g_off.log += "scanning memory...\n";
    auto regions = SnapshotMemory(g_proc);
    ss << "regions: " << regions.size() << "\n";
    g_off.log += ss.str(); ss.str("");

    uintptr_t dm = BruteDataModel(regions);
    g_off.DataModel = dm;
    ss << "DataModel: 0x" << std::hex << dm << "\n";
    g_off.log += ss.str(); ss.str("");

    if (!dm) { g_off.log += "DataModel not found â€” retrying next tick\n"; return; }

    // â”€â”€ Workspace & Camera â”€â”€
    uintptr_t workspace = FindChildByName(dm, "Workspace", 1);
    g_off.Workspace = workspace;
    ss << "Workspace: 0x" << std::hex << workspace << "\n";
    g_off.log += ss.str(); ss.str("");

    if (workspace) {
        uintptr_t cam = FindChildByName(workspace, "Camera", 2);
        g_off.Camera = cam;
        ss << "Camera: 0x" << std::hex << cam << "\n";
        g_off.log += ss.str(); ss.str("");
    }

    // â”€â”€ Players â”€â”€
    uintptr_t players = FindChildByName(dm, "Players", 1);
    g_off.oPlayers = players;
    ss << "Players: 0x" << std::hex << players << "\n";
    g_off.log += ss.str(); ss.str("");

    // â”€â”€ LocalPlayer (first child of Players whose IsLocalPlayer flag) â”€â”€
    if (players) {
        for (auto c : GetChildren(players)) {
            std::string cn = GetClassName(c);
            if (cn == "Player") {
                g_off.oLocalPlayer = c;
                ss << "LocalPlayer: 0x" << std::hex << c << " (" << GetName(c) << ")\n";
                g_off.log += ss.str(); ss.str("");
                break;
            }
        }
    }

    g_off.log += "dump complete.\n";
    g_off.dumped = true;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  MATH
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Matrix4 { float m[4][4]; };

static Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static float len(Vec3 v) { return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); }

static Vec2 W2S(Vec3 world, const Matrix4& vp, float sw, float sh) {
    float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
    float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
    float z = world.x * vp.m[0][2] + world.y * vp.m[1][2] + world.z * vp.m[2][2] + vp.m[3][2];
    float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
    if (w < 0.01f) return {-1,-1};
    float ndcX = x / w, ndcY = y / w;
    return { (ndcX + 1.f) * sw * 0.5f, (1.f - ndcY) * sh * 0.5f };
}

// Read Roblox CFrame as position (first 3 floats after 3x3 rotation)
static Vec3 ReadPosition(uintptr_t part) {
    if (!part) return {};
    struct CF { float rot[9]; float pos[3]; } cf{};
    ReadProcessMemory(g_proc, (LPCVOID)(part + g_off.oCFrame), &cf, sizeof(cf), nullptr);
    return { cf.pos[0], cf.pos[1], cf.pos[2] };
}

static Matrix4 ReadViewMatrix(uintptr_t cam) {
    Matrix4 m{};
    if (!cam) return m;
    ReadProcessMemory(g_proc, (LPCVOID)(cam + g_off.oViewMatrix), &m, sizeof(m), nullptr);
    return m;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  PLAYER DATA
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct PlayerEntry {
    uintptr_t inst;
    std::string name;
    Vec3 headPos, rootPos, hipPos;
    float health, maxHealth;
    bool valid;
};

static std::vector<PlayerEntry> g_players;
static std::mutex               g_playerMtx;

static void UpdatePlayers() {
    if (!g_off.oPlayers) return;

    uintptr_t lp = g_off.oLocalPlayer;
    std::vector<PlayerEntry> tmp;

    for (auto playerInst : GetChildren(g_off.oPlayers)) {
        if (!playerInst) continue;
        if (GetClassName(playerInst) != "Player") continue;
        if (playerInst == lp) continue;

        PlayerEntry e{};
        e.inst  = playerInst;
        e.name  = GetName(playerInst);
        e.valid = false;

        // Character = Player.Character (ptr at fixed offset, varies; search by Name)
        uintptr_t charPtr = RPM<uintptr_t>(playerInst + 0x88); // Player::Character
        if (!charPtr) continue;
        // Verify it's a Model in the workspace
        std::string charName = GetName(charPtr);
        if (charName.empty()) continue;

        // HumanoidRootPart
        uintptr_t hrp = FindChildByName(charPtr, "HumanoidRootPart", 1);
        // Head
        uintptr_t head = FindChildByName(charPtr, "Head", 1);
        // Humanoid
        uintptr_t hum  = FindChildByName(charPtr, "Humanoid", 1);

        if (!hrp || !head) continue;

        e.rootPos = ReadPosition(hrp);
        e.headPos = ReadPosition(head);
        e.hipPos  = { e.rootPos.x, e.rootPos.y - 2.f, e.rootPos.z };

        if (hum) {
            e.health    = RPM<float>(hum + g_off.oHealth);
            e.maxHealth = RPM<float>(hum + g_off.oMaxHealth);
            if (e.maxHealth < 1.f) e.maxHealth = 100.f;
        } else { e.health = e.maxHealth = 100.f; }

        e.valid = true;
        tmp.push_back(e);
    }

    std::lock_guard<std::mutex> lk(g_playerMtx);
    g_players = std::move(tmp);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  KEYBIND CONFIG
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct KeybindCfg {
    int toggle_menu    = VK_INSERT;
    int toggle_esp     = VK_F1;
    int aimbot_hold    = VK_RBUTTON;
    int toggle_aimbot  = VK_F2;
};
static KeybindCfg g_keys;

// keybind name helper
static const std::map<int,const char*> vkNames = {
    {VK_INSERT,"INSERT"},{VK_F1,"F1"},{VK_F2,"F2"},{VK_F3,"F3"},
    {VK_F4,"F4"},{VK_F5,"F5"},{VK_F6,"F6"},{VK_F7,"F7"},{VK_F8,"F8"},
    {VK_LBUTTON,"LMB"},{VK_RBUTTON,"RMB"},{VK_MBUTTON,"MMB"},
    {VK_XBUTTON1,"X1"},{VK_XBUTTON2,"X2"},
    {VK_CAPITAL,"CAPS"},{VK_TAB,"TAB"},{VK_SHIFT,"SHIFT"},
    {0x47,"G"},{0x48,"H"},{0x4A,"J"},{0x4B,"K"},
    {0x4C,"L"},{0x4D,"M"},{0x4E,"N"},{0x4F,"O"},
};
static std::string VkToName(int vk) {
    auto it = vkNames.find(vk);
    if (it != vkNames.end()) return it->second;
    char buf[8]; sprintf_s(buf,"0x%02X",vk); return buf;
}

// listening state for rebind
static bool g_listening = false;
static int* g_listenTarget = nullptr;

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  SETTINGS
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static bool  s_esp         = true;
static bool  s_box         = true;
static bool  s_skeleton    = false;
static bool  s_names       = true;
static bool  s_health      = true;
static bool  s_tracers     = false;
static bool  s_aimbot      = false;
static float s_fov         = 120.f;
static float s_smooth      = 0.08f;
static bool  s_aim_vis_only = false;
static int   s_aim_bone    = 0; // 0=head,1=hip
static bool  s_chams       = false;
static float s_esp_col[4]  = {1,0,0,1};
static float s_aim_col[4]  = {0,1,0.4f,1};
static int   s_tab         = 0;

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  AIMBOT
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static void DoAimbot(const std::vector<PlayerEntry>& plist,
                     const Matrix4& vp, float sw, float sh,
                     uintptr_t cam) {
    if (!s_aimbot) return;
    if (!GetAsyncKeyState(g_keys.aimbot_hold)) return;

    // camera position from inverse view (last column negated)
    Vec3 camPos = {
        -(vp.m[3][0] * vp.m[0][0] + vp.m[3][1] * vp.m[0][1] + vp.m[3][2] * vp.m[0][2]),
        -(vp.m[3][0] * vp.m[1][0] + vp.m[3][1] * vp.m[1][1] + vp.m[3][2] * vp.m[1][2]),
        -(vp.m[3][0] * vp.m[2][0] + vp.m[3][1] * vp.m[2][1] + vp.m[3][2] * vp.m[2][2])
    };

    float cx = sw * 0.5f, cy = sh * 0.5f;
    float best = s_fov * s_fov;
    const PlayerEntry* tgt = nullptr;

    for (auto& p : plist) {
        if (!p.valid) continue;
        Vec3 bonePos = (s_aim_bone == 0) ? p.headPos : p.hipPos;
        Vec2 screen  = W2S(bonePos, vp, sw, sh);
        if (screen.x < 0) continue;
        float dx = screen.x - cx, dy = screen.y - cy;
        float dist = dx*dx + dy*dy;
        if (dist < best) { best = dist; tgt = &p; }
    }

    if (!tgt) return;
    Vec3 bonePos = (s_aim_bone == 0) ? tgt->headPos : tgt->hipPos;
    Vec2 screen  = W2S(bonePos, vp, sw, sh);
    if (screen.x < 0) return;

    float dx = screen.x - cx, dy = screen.y - cy;
    POINT cur; GetCursorPos(&cur);
    float tx = cur.x + dx * s_smooth;
    float ty = cur.y + dy * s_smooth;

    INPUT inp{};
    inp.type = INPUT_MOUSE;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    inp.mi.dx = (LONG)(tx * 65535.f / sw);
    inp.mi.dy = (LONG)(ty * 65535.f / sh);
    SendInput(1, &inp, sizeof(INPUT));
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  OVERLAY (DX11 transparent window)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct Overlay {
    HWND        hwnd    = nullptr;
    ID3D11Device*           dev     = nullptr;
    ID3D11DeviceContext*    ctx     = nullptr;
    IDXGISwapChain*         sc      = nullptr;
    ID3D11RenderTargetView* rtv     = nullptr;
    WNDCLASSEXW wc{};
    int W = 1920, H = 1080;

    bool Init(HWND target) {
        RECT rc; GetClientRect(target, &rc);
        W = rc.right - rc.left;
        H = rc.bottom - rc.top;
        POINT tl{0,0}; ClientToScreen(target, &tl);

        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"v67ov";
        RegisterClassExW(&wc);

        hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            L"v67ov", L"",
            WS_POPUP,
            tl.x, tl.y, W, H,
            nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd) return false;

        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        MARGINS mg{-1,-1,-1,-1};
        DwmExtendFrameIntoClientArea(hwnd, &mg);

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount       = 2;
        sd.BufferDesc.Width  = W;
        sd.BufferDesc.Height = H;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow      = hwnd;
        sd.SampleDesc.Count  = 1;
        sd.Windowed          = TRUE;
        sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL fl;
        D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);

        ID3D11Texture2D* bb = nullptr;
        sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
        dev->CreateRenderTargetView(bb, nullptr, &rtv);
        bb->Release();

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

    void BeginFrame() {
        float clear[4]{0,0,0,0};
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, clear);
    }

    void EndFrame() {
        sc->Present(0, 0);
    }
} g_ov;

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  IMGUI KEYBIND WIDGET
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static void KeybindButton(const char* label, int* key) {
    bool listening = (g_listening && g_listenTarget == key);
    std::string btnLabel = listening
        ? "[ press key ]"
        : std::string("[ ") + VkToName(*key) + " ]";
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    if (ImGui::Button((btnLabel + "##" + label).c_str())) {
        g_listening = true;
        g_listenTarget = key;
    }
    if (listening) {
        for (int vk = 1; vk < 256; ++vk) {
            if (GetAsyncKeyState(vk) & 1) {
                *key = vk;
                g_listening = false;
                g_listenTarget = nullptr;
                break;
            }
        }
    }
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  MENU GUI
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static bool g_menu = true;

static void DrawMenu() {
    if (!g_menu) return;

    ImGui::SetNextWindowPos({20,20}, ImGuiCond_Once);
    ImGui::SetNextWindowSize({320,480}, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("v67suite", &g_menu, ImGuiWindowFlags_NoCollapse);

    ImGui::BeginTabBar("tabs");

    // â”€â”€ ESP â”€â”€
    if (ImGui::BeginTabItem("ESP")) {
        ImGui::Checkbox("Enable ESP", &s_esp);
        if (s_esp) {
            ImGui::Indent();
            ImGui::Checkbox("Box",       &s_box);
            ImGui::Checkbox("Skeleton",  &s_skeleton);
            ImGui::Checkbox("Names",     &s_names);
            ImGui::Checkbox("Health",    &s_health);
            ImGui::Checkbox("Tracers",   &s_tracers);
            ImGui::ColorEdit4("ESP Color", s_esp_col);
            ImGui::Unindent();
        }
        KeybindButton("Toggle ESP", &g_keys.toggle_esp);
        ImGui::EndTabItem();
    }

    // â”€â”€ Aimbot â”€â”€
    if (ImGui::BeginTabItem("Aimbot")) {
        ImGui::Checkbox("Enable Aimbot", &s_aimbot);
        if (s_aimbot) {
            ImGui::Indent();
            ImGui::SliderFloat("FOV",    &s_fov,    10.f, 600.f);
            ImGui::SliderFloat("Smooth", &s_smooth, 0.01f, 1.f);
            const char* bones[] = {"Head","Hip"};
            ImGui::Combo("Bone", &s_aim_bone, bones, 2);
            ImGui::ColorEdit4("FOV Ring Color", s_aim_col);
            ImGui::Unindent();
        }
        KeybindButton("Hold to Aim", &g_keys.aimbot_hold);
        KeybindButton("Toggle Aimbot", &g_keys.toggle_aimbot);
        ImGui::EndTabItem();
    }

    // â”€â”€ Keybinds â”€â”€
    if (ImGui::BeginTabItem("Keybinds")) {
        KeybindButton("Toggle Menu",   &g_keys.toggle_menu);
        KeybindButton("Toggle ESP",    &g_keys.toggle_esp);
        KeybindButton("Hold to Aim",   &g_keys.aimbot_hold);
        KeybindButton("Toggle Aimbot", &g_keys.toggle_aimbot);
        ImGui::EndTabItem();
    }

    // â”€â”€ Info / Dump â”€â”€
    if (ImGui::BeginTabItem("Info")) {
        ImGui::Text("DataModel:  0x%llX", g_off.DataModel);
        ImGui::Text("Workspace:  0x%llX", g_off.Workspace);
        ImGui::Text("Camera:     0x%llX", g_off.Camera);
        ImGui::Text("Players:    0x%llX", g_off.oPlayers);
        ImGui::Text("LocalPlayer:0x%llX", g_off.oLocalPlayer);
        ImGui::Separator();
        {
            std::lock_guard<std::mutex> lk(g_playerMtx);
            ImGui::Text("Tracked Players: %d", (int)g_players.size());
        }
        ImGui::Separator();
        ImGui::TextUnformatted(g_off.log.c_str());
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  ESP DRAW
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static void DrawESP(ImDrawList* dl, const Matrix4& vp, float sw, float sh) {
    if (!s_esp) return;

    std::vector<PlayerEntry> snap;
    { std::lock_guard<std::mutex> lk(g_playerMtx); snap = g_players; }

    ImU32 boxColor  = IM_COL32(
        (int)(s_esp_col[0]*255),(int)(s_esp_col[1]*255),
        (int)(s_esp_col[2]*255),(int)(s_esp_col[3]*255));
    ImU32 nameColor = IM_COL32(255,255,255,255);
    ImU32 tracerCol = IM_COL32(255,200,0,200);

    for (auto& p : snap) {
        if (!p.valid) continue;

        Vec2 head = W2S(p.headPos, vp, sw, sh);
        Vec2 root = W2S(p.rootPos, vp, sw, sh);
        if (head.x < 0 || root.x < 0) continue;
        if (head.x < -200 || head.x > sw+200) continue;

        float h  = fabsf(root.y - head.y) * 1.2f;
        float w  = h * 0.4f;
        float cx = head.x;
        float ty = head.y - 5.f;
        float by = root.y + 5.f;

        if (s_box) {
            // corner box
            float cw = w * 0.25f, ch = h * 0.15f;
            auto draw_corner = [&](float x, float y, float sx, float sy) {
                dl->AddLine({x,y},{x+sx*cw,y},boxColor,1.5f);
                dl->AddLine({x,y},{x,y+sy*ch},boxColor,1.5f);
            };
            draw_corner(cx-w/2, ty,  1, 1);
            draw_corner(cx+w/2, ty, -1, 1);
            draw_corner(cx-w/2, by,  1,-1);
            draw_corner(cx+w/2, by, -1,-1);
        }

        if (s_names)
            dl->AddText({cx - ImGui::CalcTextSize(p.name.c_str()).x * 0.5f, ty - 15.f},
                        nameColor, p.name.c_str());

        if (s_health) {
            float pct = (p.maxHealth > 0) ? (p.health / p.maxHealth) : 1.f;
            pct = std::clamp(pct, 0.f, 1.f);
            float barH = h * 1.05f;
            float bx   = cx - w/2 - 7.f;
            // background
            dl->AddRectFilled({bx, ty},{bx+3.f, ty+barH}, IM_COL32(40,40,40,200));
            // fill (greenâ†’red)
            ImU32 hcol = IM_COL32((int)((1-pct)*255),(int)(pct*255),0,220);
            float fillH = barH * pct;
            dl->AddRectFilled({bx, ty + barH - fillH},{bx+3.f, ty+barH}, hcol);
            // text
            char hp[16]; sprintf_s(hp,"%d",(int)p.health);
            dl->AddText({bx-16.f, ty + barH*0.5f - 6.f}, IM_COL32(255,255,255,200), hp);
        }

        if (s_tracers)
            dl->AddLine({sw*0.5f, sh}, {cx, root.y}, tracerCol, 1.f);

        if (s_skeleton) {
            // head â†’ neck â†’ root simple 2-bone line
            Vec2 neck = W2S({p.headPos.x, p.headPos.y - 1.5f, p.headPos.z}, vp, sw, sh);
            if (neck.x > 0) {
                dl->AddLine({head.x,head.y},{neck.x,neck.y},boxColor,1.f);
                dl->AddLine({neck.x,neck.y},{root.x,root.y},boxColor,1.f);
            }
        }
    }

    // FOV circle (aimbot preview)
    if (s_aimbot) {
        ImU32 fovCol = IM_COL32(
            (int)(s_aim_col[0]*255),(int)(s_aim_col[1]*255),
            (int)(s_aim_col[2]*255),(int)(s_aim_col[3]*180));
        dl->AddCircle({sw*0.5f, sh*0.5f}, sqrtf(s_fov), fovCol, 64, 1.f);
    }
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  WORKER THREADS
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static std::atomic<bool> g_running{true};
static uintptr_t         g_rbxBase = 0;

static void OffsetThread() {
    while (g_running) {
        if (!g_off.dumped) {
            DumpOffsets(g_rbxBase);
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

static void PlayerThread() {
    while (g_running) {
        if (g_off.dumped && g_off.oPlayers)
            UpdatePlayers();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  MAIN
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // wait for Roblox
    while (!(g_pid = FindPID(L"RobloxPlayerBeta.exe")) &&
           !(g_pid = FindPID(L"RobloxPlayer.exe")) &&
           !(g_pid = FindPID(L"Windows10Universal.exe")))
        Sleep(1000);

    g_proc   = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, g_pid);
    g_rbxBase = GetModuleBase(g_pid, L"RobloxPlayerBeta.exe");
    if (!g_rbxBase) g_rbxBase = GetModuleBase(g_pid, L"Windows10Universal.exe");

    // find Roblox window
    HWND rbxWnd = nullptr;
    for (int i = 0; i < 60 && !rbxWnd; ++i) {
        rbxWnd = FindWindowW(nullptr, L"Roblox");
        if (!rbxWnd) Sleep(1000);
    }
    if (!rbxWnd) return 1;

    if (!g_ov.Init(rbxWnd)) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // dark sharp style
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 4.f;
    st.FrameRounding  = 3.f;
    st.Colors[ImGuiCol_WindowBg]      = {0.08f,0.08f,0.08f,0.92f};
    st.Colors[ImGuiCol_Header]        = {0.2f,0.2f,0.2f,1.f};
    st.Colors[ImGuiCol_HeaderHovered] = {0.3f,0.3f,0.3f,1.f};
    st.Colors[ImGuiCol_Button]        = {0.18f,0.18f,0.18f,1.f};
    st.Colors[ImGuiCol_ButtonHovered] = {0.28f,0.28f,0.28f,1.f};
    st.Colors[ImGuiCol_CheckMark]     = {0.f,0.9f,0.4f,1.f};
    st.Colors[ImGuiCol_SliderGrab]    = {0.f,0.9f,0.4f,1.f};
    st.Colors[ImGuiCol_FrameBg]       = {0.14f,0.14f,0.14f,1.f};
    st.Colors[ImGuiCol_TitleBgActive] = {0.12f,0.12f,0.12f,1.f};

    ImGui_ImplWin32_Init(g_ov.hwnd);
    ImGui_ImplDX11_Init(g_ov.dev, g_ov.ctx);

    // spawn workers
    std::thread(OffsetThread).detach();
    std::thread(PlayerThread).detach();

    MSG msg{};
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_running = false;
        }

        // hotkeys
        static bool menuPressed = false;
        bool mk = (GetAsyncKeyState(g_keys.toggle_menu) & 0x8000) != 0;
        if (mk && !menuPressed) { g_menu = !g_menu; menuPressed = true; }
        if (!mk) menuPressed = false;

        static bool espPressed = false;
        bool ek = (GetAsyncKeyState(g_keys.toggle_esp) & 0x8000) != 0;
        if (ek && !espPressed) { s_esp = !s_esp; espPressed = true; }
        if (!ek) espPressed = false;

        static bool abPressed = false;
        bool abk = (GetAsyncKeyState(g_keys.toggle_aimbot) & 0x8000) != 0;
        if (abk && !abPressed) { s_aimbot = !s_aimbot; abPressed = true; }
        if (!abk) abPressed = false;

        // transparent to clicks when menu closed
        LONG exStyle = GetWindowLongW(g_ov.hwnd, GWL_EXSTYLE);
        if (g_menu)
            SetWindowLongW(g_ov.hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
        else
            SetWindowLongW(g_ov.hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);

        // sync overlay pos to game window
        RECT rc; GetClientRect(rbxWnd, &rc);
        POINT tl{0,0}; ClientToScreen(rbxWnd, &tl);
        SetWindowPos(g_ov.hwnd, HWND_TOPMOST, tl.x, tl.y,
                     rc.right, rc.bottom, SWP_NOACTIVATE);

        float sw = (float)rc.right, sh = (float)rc.bottom;
        Matrix4 vp{};
        if (g_off.Camera) vp = ReadViewMatrix(g_off.Camera);

        // aimbot before render
        {
            std::lock_guard<std::mutex> lk(g_playerMtx);
            DoAimbot(g_players, vp, sw, sh, g_off.Camera);
        }

        g_ov.BeginFrame();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // full-screen transparent overlay window
        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize({sw,sh});
        ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::Begin("##ov", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);

        auto* dl = ImGui::GetWindowDrawList();
        DrawESP(dl, vp, sw, sh);

        ImGui::End();
        DrawMenu();

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_ov.EndFrame();

        // keep process alive check
        DWORD ec = STILL_ACTIVE;
        GetExitCodeProcess(g_proc, &ec);
        if (ec != STILL_ACTIVE) break;

        Sleep(1);
    }

    g_running = false;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CloseHandle(g_proc);
    return 0;
}
