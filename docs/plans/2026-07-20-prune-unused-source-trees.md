# Prune Unused ATF and U-Boot Source Trees Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Keep only the ATF and U-Boot source trees used by the `dhcp` branch's supported build path.

**Architecture:** The root `build.sh` remains pinned to `atf-20240117-bacca82a8` and `uboot-mtk-20230718-09eda825`, which contain the active S20P and DHCP changes. Four unreferenced historical or alternate source snapshots are removed as tracked content, without rewriting Git history.

**Tech Stack:** Git, POSIX shell, MediaTek ATF, U-Boot

---

### Task 1: Preserve the only uncommitted file

**Files:**
- Back up: `atf-20220606-637ba581b/Makefile`

**Step 1:** Copy the modified Makefile to `/private/tmp/atf-20220606-637ba581b-Makefile.modified` before deleting its source tree.

**Step 2:** Confirm that the backup exists and is non-empty.

### Task 2: Remove unused source snapshots

**Files:**
- Delete: `atf-20220606-637ba581b/`
- Delete: `atf-20250711/`
- Delete: `uboot-mtk-20220606/`
- Delete: `uboot-mtk-20250711/`

**Step 1:** Remove the four exact tracked directories with `git rm -r`.

**Step 2:** Confirm that the active `atf-20240117-bacca82a8/` and `uboot-mtk-20230718-09eda825/` directories still exist.

### Task 3: Clean the active build selector

**Files:**
- Modify: `build.sh:3-7`

**Step 1:** Delete the two commented assignments that refer to the removed 2022 source trees.

**Step 2:** Verify that the active assignments still select `atf-20240117-bacca82a8` and `uboot-mtk-20230718-09eda825`.

### Task 4: Validate and deliver

**Step 1:** Search outside the retained source trees for references to all four removed directory names. Expected: no matches.

**Step 2:** Run `git diff --cached --check`. Expected: no whitespace errors.

**Step 3:** Review `git status`, the staged summary, and the final commit contents. Expected: only the four removals, `build.sh`, and this plan.

**Step 4:** Commit the cleanup with a detailed message and push the `dhcp` branch to `origin`.
