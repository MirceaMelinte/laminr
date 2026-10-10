package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestStoreFollowsPortableMarker(t *testing.T) {
	dir := t.TempDir()
	self := filepath.Join(dir, "Manager.exe")
	local := filepath.Join(dir, "Local")

	store, portable := storeFor(self, local)
	if portable || store != filepath.Join(local, "laminr") {
		t.Fatalf("installed copy: got %q portable=%v", store, portable)
	}

	if err := os.Mkdir(filepath.Join(dir, "portable.txt"), 0o700); err != nil {
		t.Fatal(err)
	}
	if _, portable = storeFor(self, local); portable {
		t.Fatal("a portable.txt folder must not switch to portable mode")
	}
	os.Remove(filepath.Join(dir, "portable.txt"))

	if err := os.WriteFile(filepath.Join(dir, "portable.txt"), nil, 0o600); err != nil {
		t.Fatal(err)
	}
	store, portable = storeFor(self, local)
	if !portable || store != filepath.Join(dir, "Data") {
		t.Fatalf("portable copy: got %q portable=%v", store, portable)
	}
}
