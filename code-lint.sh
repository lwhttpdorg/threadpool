#!/bin/bash

# Color definitions
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

PROJ_DIR=$(realpath .)
LINT_REPORT="code-lint.log"
LINT_DIR="meson-build-lint"

dependencies=(
    "meson"
    "clang-tidy"
)

echo -e "⏳ ${YELLOW}>>> [1/4] Checking for required dependencies...${NC}"
missing_deps=()
for dep in "${dependencies[@]}"; do
    if ! command -v "$dep" &>/dev/null; then
        missing_deps+=("$dep")
    fi
done

# run-clang-tidy might be a Python script or a wrapper
RUN_CLANG_TIDY=""
if command -v run-clang-tidy &>/dev/null; then
    RUN_CLANG_TIDY="run-clang-tidy"
elif command -v run-clang-tidy.py &>/dev/null; then
    RUN_CLANG_TIDY="run-clang-tidy.py"
else
    missing_deps+=("run-clang-tidy (or run-clang-tidy.py)")
fi

if [ ${#missing_deps[@]} -ne 0 ]; then
    echo -e "${RED}☹️ Missing dependencies detected:${NC}"
    for dep in "${missing_deps[@]}"; do
        echo "  [x] $dep"
    done
    echo -e "${YELLOW}Please install the missing dependencies and try again.${NC}"
    echo -e "${BLUE}  sudo apt install meson clang-tidy clang-tools\n${NC}"
    exit 1
fi
echo -e "✔️ ${GREEN}Dependencies check passed.\n${NC}"

echo -e "⏳ ${YELLOW}>>> [2/4] Cleaning up old files...${NC}"
rm -rf "${LINT_REPORT}" "${LINT_DIR}"
echo -e "✔️ ${GREEN}Cleanup complete.\n${NC}"

echo -e "⏳ ${YELLOW}>>> [3/4] Configuring Meson...${NC}"
meson setup "${LINT_DIR}" -Dbuild_tests=true

# Check if Meson configuration succeeded
if [ $? -ne 0 ]; then
    echo -e "❌ ${RED}ERROR: Meson configuration failed!${NC}"
    exit 1
fi

if [ ! -f "${LINT_DIR}/compile_commands.json" ]; then
    echo -e "❌ ${RED}ERROR: compile_commands.json was not generated!${NC}"
    exit 1
fi

echo -e "✔️ ${GREEN}Meson configuration successful. compile_commands.json generated.\n${NC}"

echo -e "⏳ ${YELLOW}>>> [4/4] Running clang-tidy and saving output to ${LINT_REPORT}...${NC}"
echo -e "${BLUE}Note: This may take a while depending on the project size...${NC}"

start_time=$(date +%s)

# Execute clang-tidy and capture both stdout and stderr.
# -source-filter limits analysis to project source files (skipping fetched deps).
# -header-filter ensures project headers in include/ are also checked.
${RUN_CLANG_TIDY} -p "${LINT_DIR}" -quiet \
    -source-filter="^${PROJ_DIR}/(tests|src|examples)/.*\.(cpp|c|cc|cxx)$" \
    -header-filter="^${PROJ_DIR}/include/.*" \
    > "${LINT_REPORT}" 2>&1

# Calculate duration
end_time=$(date +%s)
duration=$((end_time - start_time))

# Check for counts in the log file
errors=$(grep -c "error:" "${LINT_REPORT}" || true)
warnings=$(grep -c "warning:" "${LINT_REPORT}" || true)
notes=$(grep -c "note:" "${LINT_REPORT}" || true)

echo -e "${GREEN}--------------------------------------------------${NC}"
echo -e "✔️ Code lint complete! Time elapsed: ${duration} seconds."
echo -e "Summary:"
echo -e "  ${RED}error:   $errors${NC}"
echo -e "  ${YELLOW}warning: $warnings${NC}"
echo -e "  ${BLUE}note:    $notes${NC}"
echo -e "${GREEN}--------------------------------------------------\n${NC}"

# If any count is non-zero, print in RED. Otherwise, print in GREEN.
if [ "$errors" -gt 0 ] || [ "$warnings" -gt 0 ] || [ "$notes" -gt 0 ]; then
    echo -e "☹️ ${RED}STATUS: code lint NOT PASSED.${NC}"
    echo -e "${YELLOW}Check '${LINT_REPORT}' for details.${NC}"
else
    echo -e "✔️ ${GREEN}STATUS: code lint PASSED.${NC}"
    rm -f "${LINT_REPORT}"
fi
rm -rf "${LINT_DIR}"
