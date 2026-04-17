import sys
import re

# Define colors
RED = "\033[1;31m"
GREEN = "\033[1;32m"
YEL = "\033[0;33m"
BLU = "\033[1;34m"
MAGENTA = "\033[1;35m"
CYAN = "\033[1;36m"
RESET = "\033[0m"

# Combined regex to match severity levels
test_result_pattern = re.compile(r"(PASS)|(FAIL)|(IGNORE)|([\d]+:)|(^test/[\w_\-\.]+:)")

def colorize(match):
    if match.group(1):  # Matches "PASS"
        return f"{GREEN}{match.group(1)}{RESET}"
    elif match.group(2):  # Matches "FAIL"
        return f"{RED}{match.group(2)}{RESET}"
    elif match.group(3):  # Matches "IGNORE"
        return f"{BLU}{match.group(3)}{RESET}"
    elif match.group(4):  # Matches "Line number"
        return f"{CYAN}{match.group(4)}{RESET}"
    elif match.group(5):  # Matches "Filename"
        return f"{MAGENTA}{match.group(5)}{RESET}"

# Check for input
if sys.stdin.isatty() and sys.stderr.isatty():
    print("No input provided. Please pipe Unity output into this script.", file=sys.stderr)
    sys.exit(1)

# Process input line by line from stderr
for line in sys.stdin if not sys.stdin.isatty() else sys.stderr:
    colored_line = test_result_pattern.sub(colorize, line)
    print(colored_line, end="")