#pragma once
// The repository's root, found at run time (Stage 68): CHEM_REPO_ROOT in the environment, else from where the binary
// is, <root>/native/<tree>/<binary>. The tests and chem_tool are compiled with CHEM_REPO_ROOT defined as this call (a
// forced include), so no build's flags name its worktree: every worktree compiles the same commands (a shared ccache)
// and links the same binaries (a shared result cache, tools/gate_cache.py).
const char *chem_repo_root();
// the directory the binary is in (its build tree; Stage 68: where its ctest fixtures are kept)
const char *chem_binary_dir();
