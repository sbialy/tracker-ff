/* BRIDGE=1 smoke (make bridge-check): uc_open, map, run two Thumb instructions, uc_close. A .c file, so the HOST_CPP glob does not link it into ui_shot. */
#include <stdio.h>
#include <unicorn/unicorn.h>

int main(void) {
    uc_engine *uc = 0;
    uint32_t maj = 0, min = 0, ver = uc_version(&maj, &min);
    if (uc_open(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS, &uc) != UC_ERR_OK) { puts("uc_open failed"); return 1; }
    const uint32_t base = 0x10000000;
    const uint8_t code[] = { 0x2a, 0x20, 0x01, 0x30 };   /* movs r0,#42 ; adds r0,#1 */
    uint32_t r0 = 0;
    int rc = 1;
    if (uc_mem_map(uc, base, 0x1000, UC_PROT_ALL) == UC_ERR_OK && uc_mem_write(uc, base, code, sizeof code) == UC_ERR_OK &&
        uc_emu_start(uc, base | 1, base + sizeof code, 0, 0) == UC_ERR_OK && uc_reg_read(uc, UC_ARM_REG_R0, &r0) == UC_ERR_OK && r0 == 43)
        rc = 0;
    uc_close(uc);
    printf("unicorn %u.%u (0x%x): r0=%u %s\n", maj, min, ver, r0, rc ? "FAIL" : "ok");
    return rc;
}
