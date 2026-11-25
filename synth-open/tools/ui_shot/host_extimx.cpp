// ui_shot PLATFORM=IMX BRIDGE=1 only (the plus pin, IMX port P10 H7, ref/ff-imx-port-plan.md R2.1g): the extension module of
// Tracker+ and Mini runs as Arm code under its own Unicorn instance (UC_CPU_ARM_MAX, the Cortex-M7 FPv5 set the plus harness uses).
// synth-open/ext_imx.cpp runs the loader rules and the cache steps on its slot, then calls enter() here: the slot's image and bss go to
// kRun (what a sim module is linked for, poly-devtools firmware/ext_imx), the hooks block goes to kBlock with the services as stubs in
// kSvc (one `bx lr` each, a code hook calls the image's ext_imx_svc_* natively and sets r0), and the entry runs with lr = kRet.
// update() and command() run the hooks the entry filled. Not firmware: Unicorn is GPL-2.0, never distribute a BRIDGE build.
#if defined(UISHOT_BRIDGE) && defined(UISHOT_PLUS) && defined(UISHOT_EXT_IMX)

#include "ext_imx.h"

#include <unicorn/unicorn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace
{
constexpr uint32_t kRun = 0x80F00000u;      // the slot in the IMX SDRAM window (48 KB slot in a 64 KB map)
constexpr uint32_t kRunMap = 0x10000u;
constexpr uint32_t kStack = 0x20000000u;    // DTCM: 32 KB stack, then the block and the line buffers
constexpr uint32_t kStackBytes = 0x8000u;
constexpr uint32_t kBlock = 0x20008000u;    // ext_imx_hooks_t
constexpr uint32_t kLine = 0x20008100u;     // the console line (256 B)
constexpr uint32_t kOut = 0x20008200u;      // the reply (kOutCap)
constexpr uint32_t kOutCap = 0x1000u;
constexpr uint32_t kText = 0x20009200u;     // a string a service returns (the build id)
constexpr uint32_t kSvc = 0x60100000u;      // service stubs (QSPI window), kRet in the same page
constexpr uint32_t kRet = 0x60100F00u;
constexpr uint32_t kImageId = 0x494D5831u;  // 'IMX1': the plus sim's image id, a module for the sim is packed with it

enum Svc { kLog, kMillis, kExists, kWriteText, kBuildId, kSvcCount };

uc_engine* s_uc;
uint32_t s_update, s_command;

uint32_t reg(int r)
{
    uint32_t v = 0;
    uc_reg_read(s_uc, r, &v);
    return v;
}

std::string guestStr(uint32_t a, uint32_t max = 512)
{
    std::string s;
    char c;
    while (s.size() < max && uc_mem_read(s_uc, a + uint32_t(s.size()), &c, 1) == UC_ERR_OK && c) s += c;
    return s;
}

void svcHook(uc_engine* uc, uint64_t addr, uint32_t, void*)
{
    const int i = int((addr - kSvc) / 4);
    uint32_t r0 = 0;
    switch (i)
    {
    case kLog: ext_imx_svc_log(guestStr(reg(UC_ARM_REG_R0)).c_str()); break;
    case kMillis: r0 = ext_imx_svc_millis(); break;
    case kExists: r0 = uint32_t(ext_imx_svc_exists(guestStr(reg(UC_ARM_REG_R0)).c_str())); break;
    case kWriteText:
    {
        const int n = int(reg(UC_ARM_REG_R2));
        std::string t(n > 0 && n < 4096 ? size_t(n) : 0, '\0');
        if (!t.empty()) uc_mem_read(uc, reg(UC_ARM_REG_R1), &t[0], t.size());
        r0 = uint32_t(ext_imx_svc_write_text(guestStr(reg(UC_ARM_REG_R0)).c_str(), t.data(), int(t.size())));
        break;
    }
    case kBuildId:
    {
        const char* b = ext_imx_svc_build_id();
        uc_mem_write(uc, kText, b, strnlen(b, 255) + 1);
        r0 = kText;
        break;
    }
    default: return;
    }
    uc_reg_write(uc, UC_ARM_REG_R0, &r0);
}

bool open()
{
    if (s_uc) return true;
    if (uc_open(UC_ARCH_ARM, UC_MODE_THUMB, &s_uc) != UC_ERR_OK) return false;
    uc_ctl_set_cpu_model(s_uc, UC_CPU_ARM_MAX);
    uc_mem_map(s_uc, kRun, kRunMap, UC_PROT_ALL);
    uc_mem_map(s_uc, kStack, 0x10000u, UC_PROT_READ | UC_PROT_WRITE);
    uc_mem_map(s_uc, kSvc, 0x1000u, UC_PROT_ALL);
    uint32_t cpacr = 0;
    uc_reg_read(s_uc, UC_ARM_REG_C1_C0_2, &cpacr);
    cpacr |= 0xFu << 20;
    uc_reg_write(s_uc, UC_ARM_REG_C1_C0_2, &cpacr);
    uint32_t fpexc = 0x40000000;
    uc_reg_write(s_uc, UC_ARM_REG_FPEXC, &fpexc);
    const uint16_t bxlr = 0x4770, self = 0xE7FE;
    for (int i = 0; i < kSvcCount; i++) uc_mem_write(s_uc, kSvc + 4u * i, &bxlr, 2);
    uc_mem_write(s_uc, kRet, &self, 2);
    uc_hook h;
    uc_hook_add(s_uc, &h, UC_HOOK_CODE, reinterpret_cast<void*>(svcHook), nullptr, kSvc, kSvc + 4u * kSvcCount - 1);
    return true;
}

// fn(a, b, c) in the guest, its r0. A fault or a run past the time limit: -1 (reported once per call on stderr)
uint32_t call(uint32_t fn, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0)
{
    const uint32_t sp = kStack + kStackBytes, lr = kRet | 1u;
    uc_reg_write(s_uc, UC_ARM_REG_R0, &a);
    uc_reg_write(s_uc, UC_ARM_REG_R1, &b);
    uc_reg_write(s_uc, UC_ARM_REG_R2, &c);
    uc_reg_write(s_uc, UC_ARM_REG_SP, &sp);
    uc_reg_write(s_uc, UC_ARM_REG_LR, &lr);
    const uc_err e = uc_emu_start(s_uc, fn | 1u, kRet, 1000000 /* 1 s */, 0);
    if (e != UC_ERR_OK || reg(UC_ARM_REG_PC) != kRet)
    {
        fprintf(stderr, "extimx: guest call 0x%08x stopped at 0x%08x (%s)\n", fn, reg(UC_ARM_REG_PC), uc_strerror(e));
        return 0xFFFFFFFFu;
    }
    return reg(UC_ARM_REG_R0);
}

int32_t enter(const uint8_t* slot, uint32_t bytes, uint32_t entryOffset)
{
    if (!open() || bytes > kRunMap) return -1;
    uc_mem_write(s_uc, kRun, slot, bytes);
    uc_ctl_flush_tb(s_uc);   // new code at an address that may have run before (hub lesson: flush after a host write into code)
    ext_imx_hooks_t hk;
    memset(&hk, 0, sizeof(hk));
    hk.struct_size = sizeof(hk);
    hk.svc_log = (kSvc + 4u * kLog) | 1u;
    hk.svc_millis = (kSvc + 4u * kMillis) | 1u;
    hk.svc_exists = (kSvc + 4u * kExists) | 1u;
    hk.svc_write_text = (kSvc + 4u * kWriteText) | 1u;
    hk.svc_build_id = (kSvc + 4u * kBuildId) | 1u;
    uc_mem_write(s_uc, kBlock, &hk, sizeof(hk));
    const int32_t r = int32_t(call(kRun + entryOffset, kBlock, EXT_IMX_ABI_VERSION, 1));
    uc_mem_read(s_uc, kBlock, &hk, sizeof(hk));
    s_update = r == 0 ? hk.update : 0;
    s_command = r == 0 ? hk.command : 0;
    return r;
}

void update()
{
    if (s_update) call(s_update);
}

int32_t command(const char* line, char* out, uint32_t cap)
{
    if (!cap) return -1;
    out[0] = 0;
    if (!s_command) return -1;
    const size_t n = strnlen(line, 255);
    uc_mem_write(s_uc, kLine, line, n);
    const uint8_t z = 0;
    uc_mem_write(s_uc, kLine + n, &z, 1);
    const int32_t r = int32_t(call(s_command, kLine, kOut, kOutCap));
    if (r >= 0)
    {
        const uint32_t k = uint32_t(r) < cap - 1 ? uint32_t(r) : cap - 1;
        uc_mem_read(s_uc, kOut, out, k);
        out[k] = 0;
    }
    return r;
}

const ExtImxSim s_sim{kRun, kImageId, enter, update, command};

// installed before main (the loader runs 4 s after the boot)
struct Install
{
    Install()
    {
        static ExtImxSim sim = s_sim;
        if (const char* id = getenv("UISHOT_EXT_IMAGE_ID")) sim.imageId = uint32_t(strtoul(id, nullptr, 0));   // a refusal row
        ext_imx_sim = &sim;
    }
} s_install;
}  // namespace

#endif
