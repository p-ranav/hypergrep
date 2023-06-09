#pragma once

/// Returns true if AVX2 support is discovered
bool has_avx2_support();

/// Returns true if AVX512 support is discovered
bool has_avx512_support();

/// Return true if AVX512VBMI support is discovered
bool has_avx512vbmi_support();
