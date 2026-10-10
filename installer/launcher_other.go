//go:build !windows

// The launcher only runs on Windows. These stand-ins let go vet and go test
// run on other development machines.

package main

import (
	"fmt"
	"math"
	"os"
	"os/exec"
)

func freeBytes(string) (uint64, error) { return math.MaxUint64, nil }

func hideConsole(*exec.Cmd) {}

func showError(title, text string) { fmt.Fprintf(os.Stderr, "%s: %s\n", title, text) }
