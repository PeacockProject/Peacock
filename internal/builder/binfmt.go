package builder

import (
	"os/exec"
	"strings"

	"peacock/internal/runner"
)

// registerBinfmt writes a single binfmt_misc registration line to the kernel
// WITHOUT a shell. The line is parsed from a binfmt.conf (a distro package
// file) and can contain shell metacharacters — quotes, $, ;, backticks — so it
// must never be interpolated into `sh -c "echo '...' > register"` (that ran
// arbitrary code as root on a crafted/corrupted file). `sudo tee` with the line
// on stdin sidesteps the shell entirely.
func registerBinfmt(line string) error {
	cmd := exec.Command("sudo", "tee", "/proc/sys/fs/binfmt_misc/register")
	cmd.Stdin = strings.NewReader(line + "\n")
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	return runner.RunCmd(cmd)
}
