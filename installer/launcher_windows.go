//go:build windows

package main

import (
	"os/exec"
	"strings"
	"syscall"
	"unsafe"
)

const (
	createNoWindow = 0x08000000
	mbIconError    = 0x10
)

var (
	procGetDiskFreeSpaceExW = syscall.NewLazyDLL("kernel32.dll").NewProc("GetDiskFreeSpaceExW")
	procMessageBoxW         = syscall.NewLazyDLL("user32.dll").NewProc("MessageBoxW")
)

// freeBytes returns the space available to the current user on dir's volume.
func freeBytes(dir string) (uint64, error) {
	path, err := syscall.UTF16PtrFromString(dir)
	if err != nil {
		return 0, err
	}
	var available uint64
	if ok, _, callErr := procGetDiskFreeSpaceExW.Call(uintptr(unsafe.Pointer(path)), uintptr(unsafe.Pointer(&available)), 0, 0); ok == 0 {
		return 0, callErr
	}
	return available, nil
}

// hideConsole stops a console window from flashing up for the host.
func hideConsole(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{CreationFlags: createNoWindow}
}

func showError(title, text string) {
	t, _ := syscall.UTF16PtrFromString(strings.ReplaceAll(title, "\x00", ""))
	m, _ := syscall.UTF16PtrFromString(strings.ReplaceAll(text, "\x00", ""))
	procMessageBoxW.Call(0, uintptr(unsafe.Pointer(m)), uintptr(unsafe.Pointer(t)), mbIconError)
}
