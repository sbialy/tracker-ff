/**************************************************************************/ /**
                                                                              * @file cmsis_gcc.h
                                                                              * @brief    CMSIS
                                                                              *Cortex-M Core
                                                                              *Function/Instruction
                                                                              *Header File
                                                                              * @version  V5.00
                                                                              * @date     13.
                                                                              *September 2016
                                                                              ******************************************************************************/
/*
 * Copyright (c) 2009-2016 ARM Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __CMSIS_GCC_H
#define __CMSIS_GCC_H

/* ignore some GCC warnings */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"

/* CMSIS compiler specific defines */
#ifndef __ASM
#define __ASM __asm
#endif
#ifndef __INLINE
#define __INLINE inline
#endif
#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#ifndef __NO_RETURN
#define __NO_RETURN __attribute__((noreturn))
#endif
#ifndef __USED
#define __USED __attribute__((used))
#endif
#ifndef _WEAK
#define __WEAK __attribute__((weak))
#endif
#ifndef __UNALIGNED_UINT32
struct __attribute__((packed)) T_UINT32
{
    uint32_t v;
};
#define __UNALIGNED_UINT32(x) (((struct T_UINT32*)(x))->v)
#endif
#ifndef __ALIGNED
#define __ALIGNED(x) __attribute__((aligned(x)))
#endif
#ifndef __PACKED
#define __PACKED __attribute__((packed, aligned(1)))
#endif

/* ###########################  Core Function Access  ########################### */
/** \ingroup  CMSIS_Core_FunctionInterface
    \defgroup CMSIS_Core_RegAccFunctions CMSIS Core Register Access Functions
  @{
 */
// following are macros in kinetis.h
#undef __enable_irq
#undef __disable_irq

/**
  \brief   Enable IRQ Interrupts
  \details Enables IRQ interrupts by clearing the I-bit in the CPSR.
           Can only be executed in Privileged modes.
 */
__attribute__((always_inline)) __STATIC_INLINE void __enable_irq(void)
{
    __ASM volatile("cpsie i" : : : "memory");
}

/**
  \brief   Disable IRQ Interrupts
  \details Disables IRQ interrupts by setting the I-bit in the CPSR.
           Can only be executed in Privileged modes.
 */
__attribute__((always_inline)) __STATIC_INLINE void __disable_irq(void)
{
    __ASM volatile("cpsid i" : : : "memory");
}

/**
  \brief   Get Control Register
  \details Returns the content of the Control Register.
  \return               Control Register value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __get_CONTROL(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, control" : "=r"(result));
    return (result);
}

#if (defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3))
/**
  \brief   Get Control Register (non-secure)
  \details Returns the content of the non-secure Control Register when in secure mode.
  \return               non-secure Control Register value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __TZ_get_CONTROL_NS(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, control_ns" : "=r"(result));
    return (result);
}
#endif

/**
  \brief   Set Control Register
  \details Writes the given value to the Control Register.
  \param [in]    control  Control Register value to set
 */
__attribute__((always_inline)) __STATIC_INLINE void __set_CONTROL(uint32_t control)
{
    __ASM volatile("MSR control, %0" : : "r"(control) : "memory");
}

#if (defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3))
/**
  \brief   Set Control Register (non-secure)
  \details Writes the given value to the non-secure Control Register when in secure state.
  \param [in]    control  Control Register value to set
 */
__attribute__((always_inline)) __STATIC_INLINE void __TZ_set_CONTROL_NS(uint32_t control)
{
    __ASM volatile("MSR control_ns, %0" : : "r"(control) : "memory");
}
#endif

/**
  \brief   Get IPSR Register
  \details Returns the content of the IPSR Register.
  \return               IPSR Register value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __get_IPSR(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, ipsr" : "=r"(result));
    return (result);
}

/**
  \brief   Get APSR Register
  \details Returns the content of the APSR Register.
  \return               APSR Register value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __get_APSR(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, apsr" : "=r"(result));
    return (result);
}

/**
  \brief   Get xPSR Register
  \details Returns the content of the xPSR Register.
  \return               xPSR Register value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __get_xPSR(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, xpsr" : "=r"(result));
    return (result);
}

/**
  \brief   Get Process Stack Pointer
  \details Returns the current value of the Process Stack Pointer (PSP).
  \return               PSP Register value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __get_PSP(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, psp" : "=r"(result));
    return (result);
}

#if (defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3))
/**
  \brief   Get Process Stack Pointer (non-secure)
  \details Returns the current value of the non-secure Process Stack Pointer (PSP) when in secure
  state. \return               PSP Register value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __TZ_get_PSP_NS(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, psp_ns" : "=r"(result));
    return (result);
}
#endif

/**
  \brief   Set Process Stack Pointer
  \details Assigns the given value to the Process Stack Pointer (PSP).
  \param [in]    topOfProcStack  Process Stack Pointer value to set
 */
__attribute__((always_inline)) __STATIC_INLINE void __set_PSP(uint32_t topOfProcStack)
{
    __ASM volatile("MSR psp, %0" : : "r"(topOfProcStack) : "sp");
}

#if (defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3))
/**
  \brief   Set Process Stack Pointer (non-secure)
  \details Assigns the given value to the non-secure Process Stack Pointer (PSP) when in secure
  state. \param [in]    topOfProcStack  Process Stack Pointer value to set
 */
__attribute__((always_inline)) __STATIC_INLINE void __TZ_set_PSP_NS(uint32_t topOfProcStack)
{
    __ASM volatile("MSR psp_ns, %0" : : "r"(topOfProcStack) : "sp");
}
#endif

/**
  \brief   Get Main Stack Pointer
  \details Returns the current value of the Main Stack Pointer (MSP).
  \return               MSP Register value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __get_MSP(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, msp" : "=r"(result));
    return (result);
}

#if (defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3))
/**
  \brief   Get Main Stack Pointer (non-secure)
  \details Returns the current value of the non-secure Main Stack Pointer (MSP) when in secure
  state. \return               MSP Register value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __TZ_get_MSP_NS(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, msp_ns" : "=r"(result));
    return (result);
}
#endif

/**
  \brief   Set Main Stack Pointer
  \details Assigns the given value to the Main Stack Pointer (MSP).
  \param [in]    topOfMainStack  Main Stack Pointer value to set
 */
__attribute__((always_inline)) __STATIC_INLINE void __set_MSP(uint32_t topOfMainStack)
{
    __ASM volatile("MSR msp, %0" : : "r"(topOfMainStack) : "sp");
}

#if (defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3))
/**
  \brief   Set Main Stack Pointer (non-secure)
  \details Assigns the given value to the non-secure Main Stack Pointer (MSP) when in secure state.
  \param [in]    topOfMainStack  Main Stack Pointer value to set
 */
__attribute__((always_inline)) __STATIC_INLINE void __TZ_set_MSP_NS(uint32_t topOfMainStack)
{
    __ASM volatile("MSR msp_ns, %0" : : "r"(topOfMainStack) : "sp");
}
#endif

/**
  \brief   Get Priority Mask
  \details Returns the current state of the priority mask bit from the Priority Mask Register.
  \return               Priority Mask value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __get_PRIMASK(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, primask" : "=r"(result));
    return (result);
}

#if (defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3))
/**
  \brief   Get Priority Mask (non-secure)
  \details Returns the current state of the non-secure priority mask bit from the Priority Mask
  Register when in secure state. \return               Priority Mask value
 */
__attribute__((always_inline)) __STATIC_INLINE uint32_t __TZ_get_PRIMASK_NS(void)
{
    uint32_t result;

    __ASM volatile("MRS %0, primask_ns" : "=r"(result));
    return (result);
}
#endif

/**
  \brief   Set Priority Mask
  \details Assigns the given value to the Priority Mask Register.
  \param [in]    priMask  Priority Mask
 */
__attribute__((always_inline)) __STATIC_INLINE void __set_PRIMASK(uint32_t priMask)
{
    __ASM volatile("MSR primask, %0" : : "r"(priMask) : "memory");
}

#pragma GCC diagnostic pop

#endif /* __CMSIS_GCC_H */
