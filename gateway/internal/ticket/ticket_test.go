package ticket

import (
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

type vectors struct {
	Key    string `json:"key"`
	Device string `json:"device"`
	Epoch  int64  `json:"epoch"`
	Now    int64  `json:"now"`
	Valid  []struct {
		Name   string `json:"name"`
		Claims Claims `json:"claims"`
		Ticket string `json:"ticket"`
	} `json:"valid"`
	Invalid []struct {
		Name   string `json:"name"`
		Ticket string `json:"ticket"`
		Reason string `json:"reason"`
	} `json:"invalid"`
}

func load(t *testing.T) (vectors, []byte) {
	t.Helper()
	raw, err := os.ReadFile(filepath.Join("..", "..", "..", "protocol", "fixtures", "tickets.json"))
	if err != nil {
		t.Fatal(err)
	}
	var v vectors
	if err := json.Unmarshal(raw, &v); err != nil {
		t.Fatal(err)
	}
	key, err := hex.DecodeString(v.Key)
	if err != nil {
		t.Fatal(err)
	}
	return v, key
}

func TestProtocolVectorsValid(t *testing.T) {
	v, key := load(t)
	for _, c := range v.Valid {
		got, err := Verify(c.Ticket, key, v.Device, v.Epoch, v.Now)
		if err != nil {
			t.Errorf("%s: rejected: %v", c.Name, err)
			continue
		}
		if got != c.Claims {
			t.Errorf("%s: claims %+v, want %+v", c.Name, got, c.Claims)
		}
		// Minting the same claims must give the same bytes as the reference.
		minted, err := Mint(key, c.Claims)
		if err != nil || minted != c.Ticket {
			t.Errorf("%s: minted %q, want %q (%v)", c.Name, minted, c.Ticket, err)
		}
	}
}

func TestProtocolVectorsInvalid(t *testing.T) {
	v, key := load(t)
	for _, c := range v.Invalid {
		_, err := Verify(c.Ticket, key, v.Device, v.Epoch, v.Now)
		if err == nil {
			t.Errorf("%s: accepted", c.Name)
			continue
		}
		if r := Reason(err); r != c.Reason {
			t.Errorf("%s: reason %q, want %q", c.Name, r, c.Reason)
		}
	}
}

func TestFloatClaimsRejected(t *testing.T) {
	v, key := load(t)
	// A claims body with a float iat is well-formed JSON but not a valid ticket.
	body := prefix + "." + enc.EncodeToString([]byte(`{"sub":"u:0193f6c2-7a10-7d11-b222-000000000001","dev":"`+v.Device+`","scp":"control","iat":1.5,"exp":1795800000,"ep":3}`))
	tk := body + "." + enc.EncodeToString(mac(key, body))
	if _, err := Verify(tk, key, v.Device, v.Epoch, v.Now); Reason(err) != "claims" {
		t.Fatalf("got %v, want claims", err)
	}
}
