// Self-extracting entry point of the manager EXE.
//
// The EXE carries the manager scripts, the setup host and the game payload. It
// unpacks them into a private session folder in the store, runs the host from
// there and removes the session afterwards.
//
// Command lines:
//
//	(no arguments)                   open the manager
//	--verify-package <folder>        extract and verify the embedded package only
package main

import (
	"bytes"
	"embed"
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// bundle is everything a session folder needs, one area per line: the manager
// window, installation records and recovery; presets and menu settings; NVIDIA
// files and the neural rendering model; and the setup host plus the game
// payload (all: keeps dot files such as Cache/.keep).
//
//go:embed manager.ps1 manager.xaml dialog-theme.xaml app.ico app-logo.png library.ps1 ownership.ps1 store-paths.ps1 json-state.ps1 compatibility.ps1 consent.ps1 recovery.ps1 uninstall-options.ps1
//go:embed import-effects.ps1 EffectPackages.ini menu-settings.ps1
//go:embed nvidia.ps1 nvidia-files.json streamline-files.json neural-download.ps1
//go:embed FusionSetup.exe all:PayloadFusion
var bundle embed.FS

const (
	hostExe     = "FusionSetup.exe"
	dialogTitle = "Manager setup"
	// Headroom on top of the bundle size when checking free space.
	extractMargin = 64 << 20
)

func main() {
	if len(os.Args) == 3 && os.Args[1] == "--verify-package" {
		if err := extractVerifiedPackage(os.Args[2]); err != nil {
			os.Exit(1)
		}
		return
	}
	os.Exit(run())
}

// invocation describes one start of the EXE.
type invocation struct {
	self     string // this EXE; the host receives it as the installer path
	store    string // manager data folder shared with the scripts
	portable bool
}

func newInvocation(self, localAppData string) invocation {
	inv := invocation{self: self}
	inv.store, inv.portable = storeFor(self, localAppData)
	return inv
}

// storeFor picks the manager data folder: a portable copy (portable.txt beside
// the EXE) keeps it in Data next to the EXE, otherwise it lives in LOCALAPPDATA.
func storeFor(self, localAppData string) (store string, portable bool) {
	dir := filepath.Dir(self)
	if info, err := os.Stat(filepath.Join(dir, "portable.txt")); err == nil && info.Mode().IsRegular() {
		return filepath.Join(dir, "Data"), true
	}
	return filepath.Join(localAppData, "laminr"), false
}

func run() int {
	self, err := os.Executable()
	if err != nil {
		return 1
	}
	inv := newInvocation(self, os.Getenv("LOCALAPPDATA"))
	inv.exportEnvironment()
	if err = inv.launch(); err != nil {
		showError(dialogTitle, "The manager could not start: "+err.Error())
		return 1
	}
	return 0
}

// exportEnvironment tells the scripts where the store is and whether this is
// a portable copy.
func (inv invocation) exportEnvironment() {
	os.Setenv("LAMINR_STORE", inv.store)
	if inv.portable {
		os.Setenv("LAMINR_PORTABLE", "1")
	} else {
		os.Unsetenv("LAMINR_PORTABLE")
	}
}

func (inv invocation) launch() error {
	sessions := filepath.Join(inv.store, "Sessions")
	if err := os.MkdirAll(sessions, 0o700); err != nil {
		return err
	}
	if err := ensureSpace(sessions); err != nil {
		return err
	}
	session, err := os.MkdirTemp(sessions, "setup-")
	if err != nil {
		return err
	}
	defer func() {
		os.RemoveAll(session)
		// These only succeed when nothing else is left in them.
		os.Remove(sessions)
		os.Remove(inv.store)
	}()
	if err = unpack(session); err != nil {
		return err
	}
	return inv.startHost(session)
}

func ensureSpace(dir string) error {
	need := uint64(extractMargin)
	err := fs.WalkDir(bundle, ".", func(name string, entry fs.DirEntry, err error) error {
		if err != nil || entry.IsDir() {
			return err
		}
		info, err := entry.Info()
		if err == nil {
			need += uint64(info.Size())
		}
		return err
	})
	if err != nil {
		return err
	}
	free, err := freeBytes(dir)
	if err != nil {
		return fmt.Errorf("cannot check free disk space for unpacking the manager: %w", err)
	}
	if free < need {
		return fmt.Errorf("not enough disk space to unpack the manager: %d MB needed, %d MB free. Free up space and try again; no game files were changed", need>>20, free>>20)
	}
	return nil
}

func unpack(dest string) error {
	return fs.WalkDir(bundle, ".", func(name string, entry fs.DirEntry, err error) error {
		if err != nil || name == "." {
			return err
		}
		target := filepath.Join(dest, filepath.FromSlash(name))
		if entry.IsDir() {
			return os.MkdirAll(target, 0o700)
		}
		data, err := bundle.ReadFile(name)
		if err != nil {
			return err
		}
		return os.WriteFile(target, data, 0o600)
	})
}

func (inv invocation) startHost(session string) error {
	var output bytes.Buffer
	cmd := exec.Command(filepath.Join(session, hostExe), inv.self)
	cmd.Stdout, cmd.Stderr = &output, &output
	hideConsole(cmd)
	if err := cmd.Run(); err != nil {
		if text := strings.TrimSpace(output.String()); text != "" {
			return fmt.Errorf("the setup host failed: %v\n%s", err, text)
		}
		return fmt.Errorf("the setup host failed: %v", err)
	}
	return nil
}
