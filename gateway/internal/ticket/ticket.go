// Package ticket mints and verifies Lumen local tickets (docs/pro/PROTOCOL.md §6).
//
// A ticket is "LT1.<b64url(claims)>.<b64url(HMAC-SHA256(key, "LT1."+b64url(claims)))>".
// Verification checks, in this fixed order so every implementation reports
// the same reason: format, mac, claims, device, epoch, lifetime,
// not_yet_valid, expired. The MAC covers the received text; claims are never
// re-serialised before checking. Vectors: protocol/fixtures/tickets.json.
package ticket

import (
	"bytes"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"regexp"
	"strings"
)

// Limits shared with the firmware and the cloud.
const (
	MaxLength        = 512
	MaxControlLife   = 7 * 24 * 3600
	MaxAdminLife     = 12 * 3600
	ClockSkewSeconds = 300
	prefix           = "LT1"
)

// Scopes.
const (
	ScopeControl = "control"
	ScopeAdmin   = "admin"
)

// Claims is the ticket body. Field order here is the minting order.
type Claims struct {
	Sub string `json:"sub"`
	Dev string `json:"dev"`
	Scp string `json:"scp"`
	Iat int64  `json:"iat"`
	Exp int64  `json:"exp"`
	Ep  int64  `json:"ep"`
}

// Error carries one of the protocol's reason codes.
type Error struct{ Reason string }

func (e *Error) Error() string { return "ticket rejected: " + e.Reason }

// Reason returns the protocol reason of a verification error ("" if err is nil
// or not a ticket error).
func Reason(err error) string {
	var te *Error
	if errors.As(err, &te) {
		return te.Reason
	}
	return ""
}

func reject(reason string) error { return &Error{Reason: reason} }

var (
	b64Chars = regexp.MustCompile(`^[A-Za-z0-9_-]+$`)
	uuidRe   = regexp.MustCompile(`^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$`)
	enc      = base64.RawURLEncoding
)

func mac(key []byte, body string) []byte {
	h := hmac.New(sha256.New, key)
	h.Write([]byte(body))
	return h.Sum(nil)
}

// Mint signs claims with key. It does not validate the claims; callers mint
// only what they would accept (see Verify).
func Mint(key []byte, c Claims) (string, error) {
	raw, err := json.Marshal(c)
	if err != nil {
		return "", err
	}
	body := prefix + "." + enc.EncodeToString(raw)
	return body + "." + enc.EncodeToString(mac(key, body)), nil
}

// Verify checks ticket for device at epoch, with now in Unix seconds.
func Verify(ticket string, key []byte, device string, epoch, now int64) (Claims, error) {
	var c Claims
	if len(ticket) > MaxLength {
		return c, reject("format")
	}
	parts := strings.Split(ticket, ".")
	if len(parts) != 3 || parts[0] != prefix || !b64Chars.MatchString(parts[1]) || !b64Chars.MatchString(parts[2]) {
		return c, reject("format")
	}
	raw, err1 := enc.DecodeString(parts[1])
	got, err2 := enc.DecodeString(parts[2])
	if err1 != nil || err2 != nil {
		return c, reject("format")
	}
	if !hmac.Equal(got, mac(key, parts[0]+"."+parts[1])) {
		return c, reject("mac")
	}
	if err := decodeClaims(raw, &c); err != nil {
		return Claims{}, reject("claims")
	}
	if c.Dev != device {
		return Claims{}, reject("device")
	}
	if c.Ep != epoch {
		return Claims{}, reject("epoch")
	}
	life := int64(MaxControlLife)
	if c.Scp == ScopeAdmin {
		life = MaxAdminLife
	}
	if c.Exp <= c.Iat || c.Exp-c.Iat > life {
		return Claims{}, reject("lifetime")
	}
	if c.Iat > now+ClockSkewSeconds {
		return Claims{}, reject("not_yet_valid")
	}
	if c.Exp <= now {
		return Claims{}, reject("expired")
	}
	return c, nil
}

// decodeClaims requires exactly the six keys with the right types.
func decodeClaims(raw []byte, c *Claims) error {
	var keys map[string]json.RawMessage
	if err := json.Unmarshal(raw, &keys); err != nil || len(keys) != 6 {
		return errors.New("claims")
	}
	for _, k := range []string{"sub", "dev", "scp", "iat", "exp", "ep"} {
		if _, ok := keys[k]; !ok {
			return errors.New("claims")
		}
	}
	d := json.NewDecoder(bytes.NewReader(raw))
	d.DisallowUnknownFields()
	if err := d.Decode(c); err != nil {
		return err // also rejects floats for the integer fields
	}
	if c.Scp != ScopeControl && c.Scp != ScopeAdmin {
		return errors.New("claims")
	}
	if len(c.Sub) < 3 || (c.Sub[:2] != "u:" && c.Sub[:2] != "g:") || !uuidRe.MatchString(c.Sub[2:]) {
		return errors.New("claims")
	}
	if !uuidRe.MatchString(c.Dev) || c.Iat < 0 || c.Exp < 0 || c.Ep < 0 {
		return errors.New("claims")
	}
	return nil
}
