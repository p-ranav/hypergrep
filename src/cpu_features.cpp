#include <cpuid.h>
#include <hypergrep/cpu_features.hpp>

bool has_avx2_support() {
  unsigned int eax, ebx, ecx, edx;
  if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
    if (ecx & bit_AVX2) {
      return true; // AVX2 is supported
    }
  }
  return false; // AVX2 is not supported
}

// Function to check if the CPU supports AVX-512
bool has_avx512_support() {
  unsigned int eax, ebx, ecx, edx;
  if (__get_cpuid(0x00000007, &eax, &ebx, &ecx, &edx)) {
    if (ebx & bit_AVX512F && ebx & bit_AVX512DQ) {
      return true; // AVX-512 is supported
    }
  }
  return false; // AVX-512 is not supported
}

// Function to check if the CPU supports AVX-512VBMI
bool has_avx512vbmi_support() {
  unsigned int eax, ebx, ecx, edx;
  if (__get_cpuid(0x00000007, &eax, &ebx, &ecx, &edx)) {
    if (ebx & bit_AVX512VBMI) {
      return 1; // AVX-512VBMI is supported
    }
  }
  return 0; // AVX-512VBMI is not supported
}